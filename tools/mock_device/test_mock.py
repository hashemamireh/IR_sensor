#!/usr/bin/env python3
"""Tests for the mock device server. Standard library only.

    python3 tools/mock_device/test_mock.py
"""

import json
import os
import shutil
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import server  # noqa: E402

TIME_SCALE = 10.0  # device seconds per wall second


class MockDeviceTest(unittest.TestCase):
    def setUp(self):
        self.start_server(server.Device(time_scale=TIME_SCALE))

    def tearDown(self):
        self.server.stop()

    def start_server(self, device, **kw):
        self.device = device
        self.server = server.DeviceServer(device, "127.0.0.1", 0, quiet=True, announce=False,
                                          **kw)
        self.server.start()
        self.base = "http://127.0.0.1:%d" % self.server.port

    def request(self, method, path, body=None, raw=False):
        """Returns (status code, parsed JSON or raw bytes)."""
        data = None
        if body is not None:
            data = body if isinstance(body, bytes) else json.dumps(body).encode()
        req = urllib.request.Request(self.base + path, data=data, method=method,
                                     headers={"Content-Type": "application/json"})
        try:
            with urllib.request.urlopen(req, timeout=10) as resp:
                code, payload = resp.status, resp.read()
        except urllib.error.HTTPError as e:
            code, payload = e.code, e.read()
        return code, payload if raw else json.loads(payload)

    def state(self):
        return self.request("GET", "/api/status")[1]["state"]

    def wait_idle(self, timeout=10):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.state() == "idle":
                return
            time.sleep(0.02)
        self.fail("device did not return to idle")

    def record(self, duration_s=3, rate_hz=1000):
        code, body = self.request("POST", "/api/record",
                                  {"duration_s": duration_s, "sample_rate_hz": rate_hz})
        self.assertEqual(code, 200, body)
        self.assertEqual(body, {"id": body["id"], "duration_s": duration_s,
                                "sample_rate_hz": rate_hz})
        return body["id"]

    def recordings(self):
        code, body = self.request("GET", "/api/recordings")
        self.assertEqual(code, 200)
        return body["recordings"]

    def check_irec(self, data):
        """Asserts a valid IREC file whose CRC matches; returns (header, samples)."""
        h = server.decode_header(data)
        self.assertIsNotNone(h, "bad magic or version")
        self.assertEqual(h["atten_tenths_db"], 120)
        self.assertEqual(data[0x1B:0x20], b"\0" * 5, "reserved bytes not zero")
        samples = server.payload_samples(data)
        self.assertEqual(len(samples), h["sample_count"] * 2)
        self.assertEqual(zlib.crc32(samples) & 0xFFFFFFFF, h["crc32"])
        return h, samples

    # -- main flow

    def test_record_download_compress(self):
        code, status = self.request("GET", "/api/status")
        self.assertEqual(code, 200)
        self.assertEqual(status["state"], "idle")
        self.assertNotIn("recording", status)
        free_before = status["free_bytes"]

        rec_id = self.record(duration_s=3, rate_hz=1000)
        status = self.request("GET", "/api/status")[1]
        self.assertEqual(status["state"], "recording")
        self.assertEqual(status["recording"]["id"], rec_id)
        self.assertEqual(status["recording"]["total_s"], 3)

        # progress advances with (scaled) time
        time.sleep(1.5 / TIME_SCALE)
        status = self.request("GET", "/api/status")[1]
        if status["state"] == "recording":
            self.assertGreaterEqual(status["recording"]["elapsed_s"], 1)
        self.wait_idle()

        recs = self.recordings()
        self.assertEqual(len(recs), 1)
        entry = recs[0]
        self.assertEqual(entry["id"], rec_id)
        self.assertEqual(entry["sample_rate_mhz"], 1000000)
        self.assertEqual(entry["sample_count"], 3000)
        self.assertEqual(entry["duration_ms"], 3000)
        self.assertEqual(entry["size_bytes"], 32 + 3000 * 2)
        self.assertFalse(entry["compressed"])
        self.assertFalse(entry["overrun"])
        self.assertEqual(entry["attenuation_db"], 12)
        self.assertEqual(list(entry)[-3:], ["compressed", "overrun", "attenuation_db"])
        status = self.request("GET", "/api/status")[1]
        self.assertEqual(status["recording_count"], 1)
        self.assertEqual(status["free_bytes"], free_before - entry["size_bytes"])

        code, data = self.request("GET", "/api/recordings/%d" % rec_id, raw=True)
        self.assertEqual(code, 200)
        h, samples = self.check_irec(data)
        self.assertEqual(h["flags"], 0)
        self.assertEqual(h["start_utc_us"], entry["start_utc_us"])
        values = memoryview(samples).cast("H")
        self.assertLessEqual(max(values), 2668, "above the OPT101's ~2.15 V ceiling")
        self.assertGreater(max(values), 700, "expected at least one IR pulse")

        code, body = self.request("POST", "/api/compress")
        self.assertEqual((code, body), (200, {"queued": 1}))
        status = self.request("GET", "/api/status")[1]
        if status["state"] == "compressing":
            self.assertEqual(status["compress"]["total"], 1)
        self.wait_idle()

        code, data2 = self.request("GET", "/api/recordings/%d" % rec_id, raw=True)
        h2, samples2 = self.check_irec(data2)
        self.assertEqual(h2["flags"] & server.FLAG_COMPRESSED, 1)
        self.assertEqual(h2["crc32"], h["crc32"])
        self.assertEqual(samples2, samples)
        self.assertLess(len(data2), len(data))
        self.assertTrue(self.recordings()[0]["compressed"])

        # nothing left to compress
        self.assertEqual(self.request("POST", "/api/compress"), (200, {"queued": 0}))
        self.assertEqual(self.state(), "idle")

    def test_stop_early_saves_partial(self):
        rec_id = self.record(duration_s=60, rate_hz=5000)
        time.sleep(2 / TIME_SCALE)
        self.assertEqual(self.request("POST", "/api/record/stop"), (200, {"id": rec_id}))
        self.assertEqual(self.state(), "saving")
        self.wait_idle()
        entry = self.recordings()[0]
        self.assertGreater(entry["sample_count"], 0)
        self.assertLess(entry["sample_count"], 60 * 5000)
        self.check_irec(self.request("GET", "/api/recordings/%d" % rec_id, raw=True)[1])

    # -- 409 rules

    def test_busy_during_recording(self):
        self.record(duration_s=60)
        busy = {"error": "busy", "state": "recording"}
        for method, path, body in [
            ("POST", "/api/record", {"duration_s": 5, "sample_rate_hz": 1000}),
            ("GET", "/api/recordings", None),
            ("GET", "/api/recordings/1", None),
            ("DELETE", "/api/recordings/1", None),
            ("DELETE", "/api/recordings", {"confirm": "DELETE_ALL"}),
            ("POST", "/api/time", {"utc_us": 1790000000000000}),
            ("POST", "/api/compress", None),
            ("POST", "/api/settings", {"brightness": "low"}),
            ("GET", "/api/calibration", None),
            ("POST", "/api/sleep", None),
            ("POST", "/api/live/start", None),
            ("POST", "/api/live/stop", None),
            ("GET", "/api/live", None),
        ]:
            self.assertEqual(self.request(method, path, body), (409, busy), path)

        self.assertEqual(self.request("GET", "/api/status")[0], 200)
        self.assertEqual(self.request("GET", "/api/settings")[0], 200)

        self.assertEqual(self.request("POST", "/api/record/stop")[0], 200)
        # saving: still busy, and stop is no longer allowed
        self.assertEqual(self.request("GET", "/api/recordings"),
                         (409, {"error": "busy", "state": "saving"}))
        self.assertEqual(self.request("POST", "/api/record/stop")[0], 409)
        self.wait_idle()
        self.assertEqual(self.request("GET", "/api/recordings")[0], 200)

    def test_stop_when_idle_is_busy(self):
        # api::route_allowed: RecordStop is only allowed while recording
        self.assertEqual(self.request("POST", "/api/record/stop"),
                         (409, {"error": "busy", "state": "idle"}))

    # -- validation

    def test_record_validation(self):
        cases = [
            ({"duration_s": 10, "sample_rate_hz": 7000}, "invalid_rate"),
            ({"duration_s": -5, "sample_rate_hz": 1000}, "invalid_duration"),
            ({"duration_s": 0, "sample_rate_hz": 1000}, "invalid_duration"),
            ({"duration_s": 301, "sample_rate_hz": 10000}, "invalid_duration"),
            ({"duration_s": 121, "sample_rate_hz": 20000}, "invalid_duration"),
            ({"duration_s": 301, "sample_rate_hz": 2000}, "invalid_duration"),
            ({"duration_s": 10, "sample_rate_hz": 2500}, "invalid_rate"),
            ({"duration_s": 1.5, "sample_rate_hz": 1000}, "invalid_duration"),
            # both fields required; rate is checked first (api.cpp handle_record_start)
            ({"duration_s": 10}, "invalid_rate"),
            ({"sample_rate_hz": 1000}, "invalid_duration"),
            ({"duration_s": "10", "sample_rate_hz": 1000}, "invalid_duration"),
            ({"duration_s": 10, "sample_rate_hz": "1000"}, "invalid_rate"),
            ({}, "invalid_rate"),
        ]
        for body, error in cases:
            self.assertEqual(self.request("POST", "/api/record", body),
                             (400, {"error": error}), body)
        for raw, error in [(b"{nope", "invalid_json"), (b"[1, 2]", "invalid_json"),
                           (b'{"duration_s": NaN, "sample_rate_hz": 1000}', "invalid_json"),
                           (b"", "invalid_body"), (b" " * 513, "invalid_body")]:
            self.assertEqual(self.request("POST", "/api/record", raw),
                             (400, {"error": error}), raw)
        self.assertEqual(self.state(), "idle")

        # whole-number floats are accepted, like cJSON numbers in the firmware
        code, body = self.request("POST", "/api/record",
                                  {"duration_s": 5.0, "sample_rate_hz": 1000.0})
        self.assertEqual((code, body["duration_s"], body["sample_rate_hz"]), (200, 5, 1000))
        self.request("POST", "/api/record/stop")
        self.wait_idle()

        # the caps themselves are allowed
        self.record(duration_s=120, rate_hz=20000)
        self.request("POST", "/api/record/stop")
        self.wait_idle()
        self.record(duration_s=300, rate_hz=10000)
        self.request("POST", "/api/record/stop")
        self.wait_idle()
        self.record(duration_s=300, rate_hz=2000)  # 2 kHz is capped at 300 s, like 1/5/10 kHz
        self.request("POST", "/api/record/stop")
        self.wait_idle()
        self.assertEqual([server.max_duration_s(r) for r in server.SAMPLE_RATES_HZ],
                         [300, 300, 300, 300, 120])

    def test_insufficient_space(self):
        self.tearDown()
        # 60 s at 10 kHz needs 1.2 MB + 64 KiB margin
        self.start_server(server.Device(capacity_bytes=1200000, time_scale=TIME_SCALE))
        self.assertEqual(
            self.request("POST", "/api/record", {"duration_s": 60, "sample_rate_hz": 10000}),
            (507, {"error": "insufficient_space"}))

    # -- recordings management

    def test_delete_all_requires_confirm(self):
        self.device.seed(3)
        for body, error in [
            (None, "invalid_body"),
            (b"not json", "invalid_json"),
            (["DELETE_ALL"], "invalid_json"),
            ({}, "confirmation_required"),
            ({"confirm": "yes"}, "confirmation_required"),
            ({"confirm": "DELETE_ALL", "x": 1}, "confirmation_required"),
        ]:
            self.assertEqual(self.request("DELETE", "/api/recordings", body),
                             (400, {"error": error}), body)
        self.assertEqual(len(self.recordings()), 3)

        self.assertEqual(self.request("DELETE", "/api/recordings", {"confirm": "DELETE_ALL"}),
                         (200, {"deleted": 3}))
        self.assertEqual(self.request("GET", "/api/recordings"), (200, {"recordings": []}))

        # IDs are never reused (§4.6)
        self.assertEqual(self.record(duration_s=1), 4)

    def test_delete_one_and_not_found(self):
        self.device.seed(2)
        self.assertEqual(self.request("DELETE", "/api/recordings/1"), (200, {"deleted": 1}))
        self.assertEqual(self.request("DELETE", "/api/recordings/1"), (404, {"error": "not_found"}))
        self.assertEqual(self.request("GET", "/api/recordings/00001")[0], 404)
        self.assertEqual(self.request("GET", "/api/recordings/00002", raw=True)[0], 200)
        self.assertEqual(self.request("GET", "/api/recordings/abc"), (404, {"error": "not_found"}))
        self.assertEqual([r["id"] for r in self.recordings()], [2])

    def test_overrun(self):
        self.tearDown()
        self.start_server(server.Device(time_scale=TIME_SCALE, overrun_every=2))
        self.device.seed(2)
        rec3 = self.record(duration_s=2, rate_hz=1000)
        rec4 = self.record_after_idle(duration_s=2, rate_hz=1000)
        self.wait_idle()
        recs = {r["id"]: r for r in self.recordings()}
        self.assertEqual({i: r["overrun"] for i, r in recs.items()},
                         {1: False, 2: True, 3: False, 4: True})
        # an overrun recording lost samples, so it's shorter than the time it covered
        self.assertEqual(recs[rec3]["sample_count"], 2000)
        self.assertLess(recs[rec4]["sample_count"], 2000)
        data = self.request("GET", "/api/recordings/%d" % rec4, raw=True)[1]
        h, _ = self.check_irec(data)
        self.assertEqual(h["flags"], server.FLAG_OVERRUN)

        # compression keeps the overrun flag
        self.request("POST", "/api/compress")
        self.wait_idle()
        h, _ = self.check_irec(self.request("GET", "/api/recordings/%d" % rec4, raw=True)[1])
        self.assertEqual(h["flags"], server.FLAG_OVERRUN | server.FLAG_COMPRESSED)
        self.assertTrue(all(r["compressed"] for r in self.recordings()))
        self.assertTrue(self.recordings()[1]["overrun"])

    def record_after_idle(self, **kw):
        self.wait_idle()
        return self.record(**kw)

    def test_two_khz_recording(self):
        rec_id = self.record(duration_s=2, rate_hz=2000)
        self.wait_idle()
        entry = self.recordings()[0]
        self.assertEqual((entry["id"], entry["sample_rate_mhz"], entry["sample_count"],
                          entry["duration_ms"]), (rec_id, 2000000, 4000, 2000))
        self.check_irec(self.request("GET", "/api/recordings/%d" % rec_id, raw=True)[1])

    def test_seed_covers_every_rate(self):
        self.device.seed(6)
        recs = self.recordings()
        self.assertEqual({r["sample_rate_mhz"] // 1000 for r in recs},
                         set(server.SAMPLE_RATES_HZ))
        for r in recs:
            self.check_irec(self.request("GET", "/api/recordings/%d" % r["id"], raw=True)[1])

    def test_header_attenuation(self):
        self.assertEqual([server.num_tenths(t) for t in (0, 25, 60, 120)], [0, 2.5, 6, 12])
        self.assertEqual(json.dumps(server.num_tenths(120)), "12")
        payload = b"\0\0" * 10
        data = server.encode_header(1000000, 10, 0, zlib.crc32(payload), 0, 25) + payload
        self.assertEqual(data[0x1A], 25)
        self.assertEqual(server.recording_entry(1, data)["attenuation_db"], 2.5)
        self.assertEqual(server.decode_header(server.compress_irec(data))["atten_tenths_db"], 25)

    def test_compress_skips_files_that_dont_shrink(self):
        # white noise in both bytes of each sample won't compress
        payload = os.urandom(20000)
        noise = server.encode_header(1000000, 10000, 0, zlib.crc32(payload), 0) + payload
        self.assertIs(server.compress_irec(noise), noise)

        self.device.seed(1)
        with self.device.lock:
            self.device.recordings[2] = noise
        self.assertEqual(self.request("POST", "/api/compress"), (200, {"queued": 2}))
        self.wait_idle()
        flags = {r["id"]: r["compressed"] for r in self.recordings()}
        self.assertEqual(flags, {1: True, 2: False})
        self.assertEqual(self.request("GET", "/api/recordings/2", raw=True)[1], noise)

    # -- deep sleep

    def test_sleep_drops_off_the_network_until_woken(self):
        self.record(duration_s=60)
        self.assertEqual(self.request("POST", "/api/sleep"),
                         (409, {"error": "busy", "state": "recording"}))
        self.request("POST", "/api/record/stop")
        self.wait_idle()

        self.assertEqual(self.request("POST", "/api/sleep"), (200, {"sleeping": True}))
        deadline = time.monotonic() + 5
        while not self.device.asleep and time.monotonic() < deadline:
            time.sleep(0.01)
        self.assertTrue(self.device.asleep)
        time.sleep(0.05)
        with self.assertRaises(OSError):  # refused or reset
            self.request("GET", "/api/status")

        self.server.wake()
        code, status = self.request("GET", "/api/status")
        self.assertEqual((code, status["state"], status["recording_count"]), (200, "idle", 1))

    def test_sleep_wake_after(self):
        self.tearDown()
        self.start_server(self.device, wake_after=0.3)
        self.assertEqual(self.request("POST", "/api/sleep")[0], 200)
        time.sleep(0.5 / TIME_SCALE + 0.1)
        with self.assertRaises(OSError):  # refused or reset
            self.request("GET", "/api/status")
        time.sleep(0.4)
        self.assertEqual(self.request("GET", "/api/status")[0], 200)

    def test_idle_sleep(self):
        # 1000x: the 300 s timeout takes 0.3 s
        self.tearDown()
        self.start_server(server.Device(time_scale=1000, idle_sleep=True))
        self.request("POST", "/api/settings", {"sleep_timeout_s": 300})

        # status polling alone doesn't keep it awake
        deadline = time.monotonic() + 3
        with self.assertRaises(OSError):
            while time.monotonic() < deadline:
                self.request("GET", "/api/status")
                time.sleep(0.02)
        self.assertTrue(self.device.asleep)
        self.server.wake()

        # other requests do
        for _ in range(10):
            self.assertEqual(self.request("GET", "/api/settings")[0], 200)
            time.sleep(0.1)
        self.assertFalse(self.device.asleep)

        # 0 = never
        self.request("POST", "/api/settings", {"sleep_timeout_s": 0})
        time.sleep(0.6)
        self.assertEqual(self.request("GET", "/api/status")[0], 200)

    def test_idle_sleep_off_by_default(self):
        self.tearDown()
        self.start_server(server.Device(time_scale=1000))
        self.request("POST", "/api/settings", {"sleep_timeout_s": 300})
        time.sleep(0.6)
        self.assertEqual(self.request("GET", "/api/status")[0], 200)

    # -- live mode (§3.6)

    def live(self, since=None):
        path = "/api/live" if since is None else "/api/live?since=%s" % since
        code, body = self.request("GET", path)
        self.assertEqual(code, 200, body)
        return body

    def test_live_mode(self):
        self.assertEqual(self.request("GET", "/api/live"), (409, {"error": "busy", "state": "idle"}))
        self.assertEqual(self.request("POST", "/api/live/start"),
                         (200, {"sample_rate_hz": 10000, "bucket_ms": 20}))
        status = self.request("GET", "/api/status")[1]
        self.assertEqual(status["state"], "live")
        self.assertNotIn("recording", status)

        time.sleep(0.3)  # 3 s of device time = 150 buckets
        body = self.live(0)
        self.assertEqual(list(body), ["bucket_ms", "first_seq", "next_seq", "overruns", "buckets"])
        self.assertEqual((body["bucket_ms"], body["first_seq"], body["overruns"]), (20, 0, 0))
        n = len(body["buckets"])
        self.assertGreater(n, 50)
        self.assertEqual(body["next_seq"], body["first_seq"] + n)
        for lo, hi, mean in body["buckets"]:
            self.assertLessEqual(lo, mean)
            self.assertLessEqual(mean, hi)
        # baseline ~200, and the 5 ms pulses every 250 ms show up as spikes in max
        maxes = [b[1] for b in body["buckets"]]
        self.assertTrue(all(190 <= b[0] <= 210 for b in body["buckets"]))
        self.assertGreater(sum(1 for m in maxes if m > 2400), n // 15)
        self.assertGreater(sum(1 for m in maxes if m < 300), n // 2)

        # polling with since=next_seq continues without gaps
        time.sleep(0.05)
        nxt = body["next_seq"]
        body = self.live(nxt)
        self.assertEqual(body["first_seq"], nxt)
        # nothing new yet: an empty list, not the whole ring again
        body = self.live(body["next_seq"])
        self.assertLessEqual(len(body["buckets"]), 30)

        # gating during live mode
        busy = {"error": "busy", "state": "live"}
        for method, path in [("POST", "/api/compress"), ("POST", "/api/live/start"),
                             ("POST", "/api/record/stop")]:
            self.assertEqual(self.request(method, path), (409, busy), path)
        self.assertEqual(self.request("GET", "/api/recordings")[0], 200)
        self.assertEqual(self.request("POST", "/api/settings", {"brightness": "low"})[0], 200)
        self.assertEqual(self.request("POST", "/api/time", {"utc_us": 1790000000000000})[0], 200)
        self.assertEqual(self.request("GET", "/api/calibration")[0], 200)

        self.assertEqual(self.request("POST", "/api/live/stop"), (200, {"stopped": True}))
        self.assertEqual(self.state(), "idle")
        self.assertEqual(self.request("POST", "/api/live/stop"),
                         (409, {"error": "busy", "state": "idle"}))
        self.assertEqual(self.request("GET", "/api/live")[0], 409)

    def test_live_ring_and_since(self):
        self.request("POST", "/api/live/start")
        time.sleep(1.6)  # 16 s of device time: more than the 640-bucket ring holds
        # The ring keeps moving between requests (500 buckets/s here), so compare
        # first_seq with the next_seq of the same response
        def oldest_held(body):
            return body["next_seq"] - server.LIVE_RING_BUCKETS

        body = self.live()
        self.assertEqual(len(body["buckets"]), 250)  # at most 250 per response
        self.assertGreater(body["first_seq"], 0)  # the oldest buckets fell out of the ring
        self.assertEqual(body["first_seq"], oldest_held(body))  # omitted: oldest held
        body = self.live(0)  # older than the ring
        self.assertEqual(body["first_seq"], oldest_held(body))
        body = self.live(10 ** 30)  # newer than next_seq
        self.assertEqual(body["first_seq"], oldest_held(body))
        recent = body["next_seq"] - 100
        body = self.live(recent)
        self.assertEqual(body["first_seq"], recent)
        self.assertEqual(body["buckets"][:100], [server.live_bucket(k) for k in range(
            recent - self.device.live_start_seq, recent - self.device.live_start_seq + 100)])
        for bad in ("abc", "", "-1", "1.5", "12x"):
            self.assertEqual(self.request("GET", "/api/live?since=" + bad),
                             (400, {"error": "invalid_query", "field": "since"}), bad)

        # sequence numbers continue across sessions; the ring is cleared at start
        self.request("POST", "/api/live/stop")
        end = self.device.live_next_seq
        self.request("POST", "/api/live/start")
        body = self.live(0)
        self.assertEqual(body["first_seq"], end)
        self.assertLess(len(body["buckets"]), 50)
        self.request("POST", "/api/live/stop")

    def test_record_and_sleep_end_live_mode(self):
        self.request("POST", "/api/live/start")
        rec_id = self.record(duration_s=1)
        self.assertEqual(self.state(), "recording")
        self.wait_idle()
        self.assertEqual(self.recordings()[0]["id"], rec_id)

        # As in the firmware: a body that doesn't parse is rejected by the handler and
        # leaves live mode running; recorder_start ends live mode before validating,
        # so a recording refused by recording::validate still ends it
        self.request("POST", "/api/live/start")
        self.assertEqual(self.request("POST", "/api/record", {"duration_s": 5}),
                         (400, {"error": "invalid_rate"}))
        self.assertEqual(self.state(), "live")
        self.assertEqual(self.request("POST", "/api/record",
                                      {"duration_s": 999, "sample_rate_hz": 1000}),
                         (400, {"error": "invalid_duration"}))
        self.assertEqual(self.state(), "idle")
        self.request("POST", "/api/live/start")

        self.assertEqual(self.request("POST", "/api/sleep"), (200, {"sleeping": True}))
        deadline = time.monotonic() + 5
        while not self.device.asleep and time.monotonic() < deadline:
            time.sleep(0.01)
        self.assertTrue(self.device.asleep)
        self.server.wake()
        self.assertEqual(self.state(), "idle")

    def test_live_timeout(self):
        self.tearDown()
        # 100x: the 60 s timeout takes 0.6 s
        self.start_server(server.Device(time_scale=100, live_timeout=True))
        self.request("POST", "/api/settings", {"live_timeout_s": 60})

        # polling status and live data doesn't keep it running
        self.request("POST", "/api/live/start")
        deadline = time.monotonic() + 3
        while self.state() == "live" and time.monotonic() < deadline:
            self.request("GET", "/api/live")
            time.sleep(0.05)
        self.assertEqual(self.state(), "idle")

        # other requests do
        self.request("POST", "/api/live/start")
        for _ in range(12):
            self.assertEqual(self.request("GET", "/api/settings")[0], 200)
            time.sleep(0.1)
        self.assertEqual(self.state(), "live")
        self.request("POST", "/api/live/stop")

    def test_live_timeout_off_by_default(self):
        self.tearDown()
        self.start_server(server.Device(time_scale=100))
        self.request("POST", "/api/settings", {"live_timeout_s": 60})
        self.request("POST", "/api/live/start")
        time.sleep(1.0)
        self.assertEqual(self.state(), "live")
        self.request("POST", "/api/live/stop")

    def test_live_suspends_idle_sleep(self):
        self.tearDown()
        # 1000x: the 300 s sleep timeout takes 0.3 s
        self.start_server(server.Device(time_scale=1000, idle_sleep=True))
        self.request("POST", "/api/settings", {"sleep_timeout_s": 300})
        self.request("POST", "/api/live/start")
        time.sleep(0.8)
        self.assertFalse(self.device.asleep)
        self.assertEqual(self.state(), "live")
        self.request("POST", "/api/live/stop")

    # -- time, settings, calibration

    def test_time_sync(self):
        utc_us = 1790000000 * 1000000
        code, body = self.request("POST", "/api/time", {"utc_us": utc_us})
        self.assertEqual(code, 200)
        self.assertAlmostEqual(body["utc_us"], utc_us, delta=1000000)
        rec_id = self.record(duration_s=1)
        self.wait_idle()
        entry = self.recordings()[0]
        self.assertEqual(entry["id"], rec_id)
        self.assertAlmostEqual(entry["start_utc_us"], utc_us, delta=5000000)
        for implausible in ("now", None, -1, 0, 1000000, 4102444800 * 1000000):
            self.assertEqual(self.request("POST", "/api/time", {"utc_us": implausible}),
                             (400, {"error": "invalid_time"}))

    def test_settings(self):
        self.assertEqual(self.request("GET", "/api/settings"), (200, server.DEFAULT_SETTINGS))
        code, body = self.request("POST", "/api/settings",
                                  {"brightness": "high", "default_duration_s": 10})
        self.assertEqual(code, 200)
        self.assertEqual(body["brightness"], "high")
        self.assertEqual(body["default_duration_s"], 10)
        self.assertEqual(self.request("GET", "/api/settings")[1], body)

        self.assertEqual(self.request("POST", "/api/settings", {"screen_timeout_s": 300.0})[1]
                         ["screen_timeout_s"], 300)
        self.assertEqual(self.request("POST", "/api/settings", {"screen_timeout_s": 45}),
                         (400, {"error": "invalid_setting", "field": "screen_timeout_s"}))
        self.assertEqual(self.request("POST", "/api/settings", {"wifi_ssid": "x"}),
                         (400, {"error": "unknown_setting", "field": "wifi_ssid"}))
        self.assertEqual(server.DEFAULT_SETTINGS["sleep_timeout_s"], 900)
        self.assertEqual(server.DEFAULT_SETTINGS["live_timeout_s"], 300)
        self.assertEqual(list(server.DEFAULT_SETTINGS)[-2:],  # settings.cpp order
                         ["sleep_timeout_s", "live_timeout_s"])
        for key, good, bad in [("sleep_timeout_s", (300, 900, 1800, 0), (600, -1, "900", None)),
                               ("live_timeout_s", (60, 300, 600, 1800), (0, 120, 3600, "60"))]:
            current = self.request("GET", "/api/settings")[1]
            for v in good:
                self.assertEqual(self.request("POST", "/api/settings", {key: v}),
                                 (200, dict(current, **{key: v})))
            for v in bad:
                self.assertEqual(self.request("POST", "/api/settings", {key: v}),
                                 (400, {"error": "invalid_setting", "field": key}), (key, v))
        self.assertEqual(self.request("POST", "/api/settings", {"default_sample_rate_hz": 2000})
                         [1]["default_sample_rate_hz"], 2000)
        # all-or-nothing
        self.request("POST", "/api/settings", {"brightness": "low", "default_sample_rate_hz": 3})
        self.assertEqual(self.request("GET", "/api/settings")[1]["brightness"], "high")

    def test_calibration(self):
        code, body = self.request("GET", "/api/calibration")
        self.assertEqual(code, 200)
        self.assertEqual(body["attenuation_db"], 12)
        mv = body["mv"]
        self.assertEqual(len(mv), 4096)
        self.assertEqual(mv[0], 0)
        self.assertEqual(mv[4095], 3300)
        self.assertTrue(all(a <= b for a, b in zip(mv, mv[1:])))

    def test_calibration_unavailable(self):
        self.tearDown()
        self.start_server(self.device, calibration=False)
        self.assertEqual(self.request("GET", "/api/calibration"),
                         (503, {"error": "calibration_unavailable"}))

    def test_unknown_route_and_method(self):
        self.assertEqual(self.request("GET", "/api/nope"), (404, {"error": "not_found"}))
        self.assertEqual(self.request("DELETE", "/api/status"),
                         (405, {"error": "method_not_allowed"}))

    # -- persistence

    def test_persistence(self):
        tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, tmp)
        self.tearDown()
        self.start_server(server.Device(data_dir=tmp, time_scale=TIME_SCALE))
        self.device.seed(2)
        self.request("DELETE", "/api/recordings/2")
        self.request("POST", "/api/settings", {"brightness": "low"})
        self.assertEqual(sorted(os.listdir(tmp)), ["00001.irec", "device.json"])

        self.tearDown()
        self.start_server(server.Device(data_dir=tmp, time_scale=TIME_SCALE))
        self.assertEqual([r["id"] for r in self.recordings()], [1])
        self.assertEqual(self.request("GET", "/api/settings")[1]["brightness"], "low")
        self.assertEqual(self.record(duration_s=1), 3)
        self.wait_idle()
        self.assertIn("00003.irec", os.listdir(tmp))


if __name__ == "__main__":
    unittest.main()
