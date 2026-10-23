#!/usr/bin/env python3
"""Mock of the IR sensor device's REST API (spec.md §8.2).

Standard library only. Mirrors firmware/lib/irsensor_core (the JSON shapes of
api_model.cpp and settings.cpp, api::route_allowed, recording::validate, the
IREC format of irec.cpp) and the request parsing of firmware/src/api.cpp.

    python3 tools/mock_device/server.py --port 8080
"""

import argparse
import json
import math
import os
import random
import re
import signal
import struct
import sys
import threading
import time
import zlib
from array import array
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# --- IREC format (spec §4.2, irec.h) ---------------------------------------

HEADER_SIZE = 32
MAGIC = b"IREC"
VERSION = 0x01
FLAG_COMPRESSED = 0x01
FLAG_OVERRUN = 0x02  # samples were lost during capture — timing unreliable
# magic, version, rate (mHz), sample count, start (UTC µs), CRC-32, flags,
# attenuation (0.1 dB), reserved
HEADER_STRUCT = struct.Struct("<4sBIIQIBB5x")
assert HEADER_STRUCT.size == HEADER_SIZE

ATTEN_TENTHS_DB = 120  # 12 dB, fixed in firmware (spec §3.2)


def encode_header(rate_mhz, sample_count, start_utc_us, crc32, flags,
                  atten_tenths_db=ATTEN_TENTHS_DB):
    return HEADER_STRUCT.pack(MAGIC, VERSION, rate_mhz, sample_count, start_utc_us, crc32, flags,
                              atten_tenths_db)


def decode_header(data):
    """Returns a dict, or None on bad magic / unsupported version."""
    if len(data) < HEADER_SIZE:
        return None
    magic, version, rate, count, start, crc, flags, atten = HEADER_STRUCT.unpack_from(data)
    if magic != MAGIC or version != VERSION:
        return None
    return {
        "sample_rate_mhz": rate,
        "sample_count": count,
        "start_utc_us": start,
        "crc32": crc,
        "flags": flags,
        "atten_tenths_db": atten,
    }


def payload_samples(data):
    """Uncompressed uint16_le sample bytes of an IREC file (decompressing if flagged)."""
    h = decode_header(data)
    payload = data[HEADER_SIZE:]
    if h["flags"] & FLAG_COMPRESSED:
        payload = zlib.decompress(payload)
    return payload


def duration_ms(h):
    if h["sample_rate_mhz"] == 0:
        return 0
    return h["sample_count"] * 1000000 // h["sample_rate_mhz"]


# --- Recording limits (recording.h / recording.cpp) ------------------------

SAMPLE_RATES_HZ = (1000, 2000, 5000, 10000, 20000)
DEFAULT_SAMPLE_RATE_HZ = 10000
MAX_DURATION_S = 300
MAX_DURATION_20KHZ_S = 120
BYTES_PER_SAMPLE = 2
FS_MARGIN_BYTES = 64 * 1024
MAX_ID = 99999

# §3.5: actual rate per nominal rate. Nominal until measured on hardware.
ACTUAL_RATES_MHZ = {1000: 1000000, 2000: 2000000, 5000: 5000000, 10000: 10000000,
                    20000: 20000000}


def is_valid_rate(rate_hz):
    return rate_hz in SAMPLE_RATES_HZ


def max_duration_s(rate_hz):
    if not is_valid_rate(rate_hz):
        return 0
    return MAX_DURATION_20KHZ_S if rate_hz == 20000 else MAX_DURATION_S


def required_bytes(rate_hz, duration_s):
    return HEADER_SIZE + rate_hz * duration_s * BYTES_PER_SAMPLE


def validate(rate_hz, duration_s, free_bytes):
    """recording::validate — returns None or the recording::error_str name."""
    if not is_valid_rate(rate_hz):
        return "invalid_rate"
    if duration_s <= 0 or duration_s > max_duration_s(rate_hz):
        return "invalid_duration"
    if required_bytes(rate_hz, duration_s) + FS_MARGIN_BYTES > free_bytes:
        return "insufficient_space"
    return None


def parse_id(s):
    """recording::parse_id — 1–5 decimal digits."""
    if not re.fullmatch(r"[0-9]{1,5}", s):
        return None
    return int(s)


def filename_for_id(rec_id):
    return "%05d.irec" % rec_id


# --- Settings (settings.h / settings.cpp) ----------------------------------

BRIGHTNESS = ("low", "medium", "high")
SCREEN_TIMEOUTS_S = (30, 60, 300, 0)  # 0 = never
DEFAULT_DURATIONS_S = (5, 10, 30, 60)

DEFAULT_SETTINGS = {
    "brightness": "medium",
    "screen_timeout_s": 60,
    "default_sample_rate_hz": DEFAULT_SAMPLE_RATE_HZ,
    "default_duration_s": 30,
}

# key -> parser returning the normalised value, or None if invalid
SETTING_PARSERS = {
    "brightness": lambda v: v if isinstance(v, str) and v in BRIGHTNESS else None,
    "screen_timeout_s": lambda v: _uint_in(v, SCREEN_TIMEOUTS_S),
    "default_sample_rate_hz": lambda v: _uint_in(v, SAMPLE_RATES_HZ),
    "default_duration_s": lambda v: _uint_in(v, DEFAULT_DURATIONS_S),
}


def _uint_in(v, allowed):
    n = get_uint(v)
    return n if n in allowed else None


UINT32_MAX = 0xFFFFFFFF
INT64_MAX = 0x7FFFFFFFFFFFFFFF


def get_uint(v, max_value=UINT32_MAX):
    """api.cpp get_uint: a JSON number that's a whole value in [0, max] (30.0 counts).

    Returns the int, or None.
    """
    if isinstance(v, bool) or not isinstance(v, (int, float)):
        return None
    if isinstance(v, float) and (not math.isfinite(v) or v != math.floor(v)):
        return None
    if v < 0 or v > max_value:
        return None
    return int(v)


# --- API model (api_model.h / api_model.cpp) -------------------------------

IDLE, RECORDING, SAVING, COMPRESSING = "idle", "recording", "saving", "compressing"

# Route names follow the api::Route enum
ROUTE_STATUS = "Status"
ROUTE_RECORD_STOP = "RecordStop"
ROUTE_SETTINGS_GET = "SettingsGet"


def route_allowed(state, route):
    """§8.2 "Requests During Recording": False means respond 409 Conflict."""
    if route in (ROUTE_STATUS, ROUTE_SETTINGS_GET):
        return True  # served from RAM
    if route == ROUTE_RECORD_STOP:
        return state == RECORDING
    return state == IDLE


MIN_UTC_US = 1704067200 * 1000000  # 2024-01-01T00:00:00Z
MAX_UTC_US = 4102444800 * 1000000  # 2100-01-01T00:00:00Z


def is_plausible_utc_us(utc_us):
    """POST /api/time sanity check (api::is_plausible_utc_us)."""
    return MIN_UTC_US <= utc_us < MAX_UTC_US


def recording_entry(rec_id, data):
    h = decode_header(data)
    return {
        "id": rec_id,
        "start_utc_us": h["start_utc_us"],
        "duration_ms": duration_ms(h),
        "sample_rate_mhz": h["sample_rate_mhz"],
        "sample_count": h["sample_count"],
        "size_bytes": len(data),
        "compressed": bool(h["flags"] & FLAG_COMPRESSED),
        "overrun": bool(h["flags"] & FLAG_OVERRUN),
        "attenuation_db": num_tenths(h["atten_tenths_db"]),
    }


def num_tenths(tenths):
    """JsonWriter::num_tenths: 120 → 12, 25 → 2.5 (no trailing ".0")."""
    return tenths // 10 if tenths % 10 == 0 else tenths / 10


# --- Calibration (§3.4) ----------------------------------------------------

ATTENUATION_DB = ATTEN_TENTHS_DB // 10
FULL_SCALE_MV = 3300  # 12 dB covers the full 0–3.3 V input (§3.2)
COUNTS_PER_MV = 4095 / FULL_SCALE_MV
OPT101_MAX_MV = 2150  # output ceiling at a 3.3 V supply (§3.1)


def calibration_table():
    """4096 raw → mV entries: ~linear 0–3300 mV with a slight bow, monotonic."""
    return [round(i * FULL_SCALE_MV / 4095 + 12 * math.sin(math.pi * i / 4095))
            for i in range(4096)]


# --- Synthetic samples -----------------------------------------------------


def synth_samples(rec_id, rate_mhz, count):
    """Plausible 12-bit OPT101 trace: dark baseline with drift and noise, plus IR pulses.

    Deterministic per recording ID. Returns uint16_le bytes.
    """
    rnd = random.Random(rec_id)
    rate_hz = rate_mhz / 1000.0
    baseline = rnd.uniform(15, 45) * COUNTS_PER_MV  # ambient
    drift_amp = rnd.uniform(3, 10)
    drift_period = rnd.uniform(4, 12) * rate_hz
    gauss = rnd.gauss
    sin = math.sin
    two_pi = 2 * math.pi
    samples = array("d", (
        baseline + drift_amp * sin(two_pi * i / drift_period) + gauss(0, 2)
        for i in range(count)
    ))

    # IR pulses: fast OPT101 rise/fall, a short overshoot, then a plateau with ripple
    t = rnd.uniform(0.2, 0.8)
    duration_s = count / rate_hz if rate_hz else 0
    while t < duration_s:
        width_s = rnd.choice((0.002, 0.005, 0.010, 0.050, 0.120, 0.400))
        amp = rnd.uniform(650, 1950) * COUNTS_PER_MV
        start = int(t * rate_hz)
        width = max(1, int(width_s * rate_hz))
        tau = max(1.0, 0.00015 * rate_hz)  # ~150 µs edges
        for k in range(min(width + int(6 * tau), count - start)):
            if k < width:
                level = amp * (1 - math.exp(-(k + 1) / tau))
                level *= 1 + 0.04 * math.exp(-k / (3 * tau))  # overshoot
                level += 0.01 * amp * sin(two_pi * k / (0.01 * rate_hz))  # ripple
            else:
                level = amp * math.exp(-(k - width + 1) / tau)
            samples[start + k] += level
        t += width_s + rnd.uniform(0.3, 2.5)

    ceiling = int(OPT101_MAX_MV * COUNTS_PER_MV)  # OPT101 saturates before the ADC does
    out = array("H", (min(ceiling, max(0, int(round(v)))) for v in samples))
    if sys.byteorder == "big":
        out.byteswap()
    return out.tobytes()


def build_irec(rec_id, rate_mhz, count, start_utc_us, overrun=False):
    payload = synth_samples(rec_id, rate_mhz, count)
    flags = 0
    if overrun:
        # Drop 1–3 DMA-buffer-sized gaps (~50 ms each), as a drain-task overrun would
        rnd = random.Random(-rec_id)
        gap = max(1, int(0.05 * rate_mhz / 1000)) * BYTES_PER_SAMPLE
        for _ in range(rnd.randint(1, 3)):
            if len(payload) <= gap:
                break
            at = rnd.randrange(0, len(payload) - gap, BYTES_PER_SAMPLE)
            payload = payload[:at] + payload[at + gap:]
        count = len(payload) // BYTES_PER_SAMPLE
        flags |= FLAG_OVERRUN
    crc = zlib.crc32(payload) & 0xFFFFFFFF
    return encode_header(rate_mhz, count, start_utc_us, crc, flags) + payload


def compress_irec(data):
    """Sets flag bit 0 and zlib-compresses the payload; the CRC is unchanged.

    A file that doesn't get smaller is returned unchanged, flag unset (§4.4).
    """
    h = decode_header(data)
    if h["flags"] & FLAG_COMPRESSED:
        return data
    packed = zlib.compress(data[HEADER_SIZE:], 6)
    if len(packed) >= len(data) - HEADER_SIZE:
        return data
    header = encode_header(h["sample_rate_mhz"], h["sample_count"], h["start_utc_us"],
                           h["crc32"], h["flags"] | FLAG_COMPRESSED, h["atten_tenths_db"])
    return header + packed


# --- Device model ----------------------------------------------------------


class Busy(Exception):
    """The device left idle between the route_allowed check and the action."""

    def __init__(self, state):
        super().__init__(state)
        self.state = state


class Device:
    """In-memory device state. All public methods are thread-safe.

    time_scale > 1 runs device time faster than wall time (recording progress,
    saving and compression delays), for tests and quick demos.
    """

    def __init__(self, data_dir=None, capacity_bytes=25 * 1024 * 1024, time_scale=1.0,
                 unsynced_clock=False, ssid="IRSensor-MOCK", overrun_every=0):
        self.lock = threading.Lock()
        self.data_dir = data_dir
        self.capacity_bytes = capacity_bytes
        self.time_scale = time_scale
        self.ssid = ssid
        self.overrun_every = overrun_every
        self.boot_mono = time.monotonic()
        # Device clock = host clock + offset. An unsynced device starts at the epoch,
        # as after full power loss (§7.3).
        self.clock_offset_us = -int(time.time() * 1e6) if unsynced_clock else 0

        self.state = IDLE
        self.recordings = {}  # id -> IREC bytes
        self.settings = dict(DEFAULT_SETTINGS)
        self.next_id = 1
        self.rec = None  # active recording while RECORDING
        self.rec_timer = None
        self.compress_done = 0
        self.compress_total = 0
        self.asleep = False
        self.on_sleep = []  # callbacks run when the device falls asleep

        if data_dir:
            os.makedirs(data_dir, exist_ok=True)
            self._load()

    def _is_overrun(self, rec_id):
        """--overrun-every N marks every Nth recording ID as overrun."""
        return bool(self.overrun_every) and rec_id % self.overrun_every == 0

    # -- clocks

    def now_utc_us(self):
        return int(time.time() * 1e6) + self.clock_offset_us

    def set_time(self, utc_us):
        with self.lock:
            self.clock_offset_us = utc_us - int(time.time() * 1e6)

    def _elapsed_s(self):
        return (time.monotonic() - self.rec["start_mono"]) * self.time_scale

    # -- status

    def free_bytes(self):
        used = sum(len(d) for d in self.recordings.values())
        return max(0, self.capacity_bytes - used)

    def status(self):
        with self.lock:
            uptime_min = (time.monotonic() - self.boot_mono) / 60
            pct = max(5, int(round(82 - uptime_min / 6)))  # drains ~10 %/h
            s = {
                "state": self.state,
                "battery": {"mv": 3500 + pct * 7, "pct": pct},
                "free_bytes": self.free_bytes(),
                "recording_count": len(self.recordings),
                "wifi": {"ssid": self.ssid, "client_connected": True},
            }
            if self.state == RECORDING:
                total = self.rec["duration_s"]
                s["recording"] = {
                    "id": self.rec["id"],
                    "elapsed_s": min(total, int(self._elapsed_s())),
                    "total_s": total,
                }
            elif self.state == COMPRESSING:
                s["compress"] = {"done": self.compress_done, "total": self.compress_total}
            return s

    # -- recording

    def start_recording(self, rate_hz, duration_s):
        """Returns (recording id, None) or (None, error name)."""
        with self.lock:
            if self.state != IDLE:
                raise Busy(self.state)
            err = validate(rate_hz, duration_s, self.free_bytes())
            if err:
                return None, err
            rec_id = self.next_id
            self.next_id += 1
            self._save_meta()
            self.rec = {
                "id": rec_id,
                "rate_hz": rate_hz,
                "rate_mhz": ACTUAL_RATES_MHZ[rate_hz],
                "duration_s": duration_s,
                "start_mono": time.monotonic(),
                "start_utc_us": self.now_utc_us(),
            }
            self.state = RECORDING
            self.rec_timer = threading.Timer(duration_s / self.time_scale, self._auto_stop,
                                             args=(rec_id,))
            self.rec_timer.daemon = True
            self.rec_timer.start()
            return rec_id, None

    def stop_recording(self):
        """POST /api/record/stop. Returns the recording id, or None if not recording."""
        with self.lock:
            if self.state != RECORDING:
                return None
            rec_id = self.rec["id"]
            self._begin_save(min(self._elapsed_s(), self.rec["duration_s"]))
            return rec_id

    def _auto_stop(self, rec_id):
        with self.lock:
            if self.state == RECORDING and self.rec["id"] == rec_id:
                self._begin_save(self.rec["duration_s"])

    def _begin_save(self, elapsed_s):
        # lock held
        if self.rec_timer:
            self.rec_timer.cancel()
            self.rec_timer = None
        rec = self.rec
        self.rec = None
        rec["count"] = int(elapsed_s * rec["rate_mhz"] // 1000)
        self.state = SAVING
        threading.Thread(target=self._save, args=(rec,), daemon=True).start()

    def _save(self, rec):
        t0 = time.monotonic()
        data = build_irec(rec["id"], rec["rate_mhz"], rec["count"], rec["start_utc_us"],
                          self._is_overrun(rec["id"]))
        # ~1 s plus ~1 s per 6 MB written, like a LittleFS write of the PSRAM buffer
        wait = (1.0 + len(data) / 6e6) / self.time_scale - (time.monotonic() - t0)
        if wait > 0:
            time.sleep(wait)
        with self.lock:
            self.recordings[rec["id"]] = data
            self._write_file(rec["id"], data)
            self.state = IDLE

    # -- recordings

    def list_recordings(self):
        with self.lock:
            return [recording_entry(i, d) for i, d in sorted(self.recordings.items())]

    def get_recording(self, rec_id):
        with self.lock:
            return self.recordings.get(rec_id)

    def delete_recording(self, rec_id):
        with self.lock:
            if self.recordings.pop(rec_id, None) is None:
                return False
            self._remove_file(rec_id)
            return True

    def delete_all(self):
        with self.lock:
            n = len(self.recordings)
            for rec_id in list(self.recordings):
                self._remove_file(rec_id)
            self.recordings.clear()
            return n

    def start_compress(self):
        """Returns the number of recordings queued for compression."""
        with self.lock:
            if self.state != IDLE:
                raise Busy(self.state)
            todo = [i for i, d in sorted(self.recordings.items())
                    if not decode_header(d)["flags"] & FLAG_COMPRESSED]
            if not todo:
                return 0
            self.state = COMPRESSING
            self.compress_done = 0
            self.compress_total = len(todo)
            threading.Thread(target=self._compress, args=(todo,), daemon=True).start()
            return len(todo)

    def _compress(self, todo):
        for rec_id in todo:
            with self.lock:
                data = self.recordings[rec_id]
            t0 = time.monotonic()
            packed = compress_irec(data)
            # ~0.5 s plus ~1 s per MB on the device
            wait = (0.5 + len(data) / 1e6) / self.time_scale - (time.monotonic() - t0)
            if wait > 0:
                time.sleep(wait)
            with self.lock:
                if packed is not data:
                    self.recordings[rec_id] = packed
                    self._write_file(rec_id, packed)
                self.compress_done += 1
        with self.lock:
            self.state = IDLE

    # -- deep sleep (§7.1)

    def request_sleep(self):
        """POST /api/sleep: the response goes out first, sleep follows ~0.5 s later."""
        with self.lock:
            if self.state != IDLE:
                raise Busy(self.state)
        t = threading.Timer(0.5 / self.time_scale, self._fall_asleep)
        t.daemon = True
        t.start()

    def _fall_asleep(self):
        with self.lock:
            if self.state != IDLE or self.asleep:
                return
            self.asleep = True
        for cb in list(self.on_sleep):
            cb()

    def wake(self):
        """Long press on the encoder. RAM state is kept: the mock's recordings stand
        in for flash, and the clock keeps running through deep sleep (§7.3)."""
        with self.lock:
            self.asleep = False

    # -- settings

    def get_settings(self):
        with self.lock:
            return dict(self.settings)

    def update_settings(self, changes):
        """Returns (settings, None) or (None, (error, key)). All-or-nothing; the first
        bad key in body order is reported, as in api.cpp."""
        parsed = {}
        for key, value in changes.items():
            if key not in SETTING_PARSERS:
                return None, ("unknown_setting", key)
            parsed[key] = SETTING_PARSERS[key](value)
            if parsed[key] is None:
                return None, ("invalid_setting", key)
        with self.lock:
            self.settings.update(parsed)
            self._save_meta()
            return dict(self.settings), None

    # -- persistence (optional --data-dir)

    def _load(self):
        meta_path = os.path.join(self.data_dir, "device.json")
        if os.path.exists(meta_path):
            with open(meta_path) as f:
                meta = json.load(f)
            self.next_id = meta.get("next_id", 1)
            self.settings.update(meta.get("settings", {}))
        for name in os.listdir(self.data_dir):
            m = re.fullmatch(r"([0-9]{5})\.irec", name)
            if not m:
                continue
            with open(os.path.join(self.data_dir, name), "rb") as f:
                data = f.read()
            if decode_header(data):
                rec_id = int(m.group(1))
                self.recordings[rec_id] = data
                self.next_id = max(self.next_id, rec_id + 1)

    def _save_meta(self):
        if not self.data_dir:
            return
        path = os.path.join(self.data_dir, "device.json")
        with open(path + ".tmp", "w") as f:
            json.dump({"next_id": self.next_id, "settings": self.settings}, f, indent=2)
        os.replace(path + ".tmp", path)

    def _write_file(self, rec_id, data):
        if not self.data_dir:
            return
        path = os.path.join(self.data_dir, filename_for_id(rec_id))
        with open(path + ".tmp", "wb") as f:
            f.write(data)
        os.replace(path + ".tmp", path)  # temp file + rename, as on the device (§9.4)

    def _remove_file(self, rec_id):
        if self.data_dir:
            try:
                os.remove(os.path.join(self.data_dir, filename_for_id(rec_id)))
            except FileNotFoundError:
                pass

    # (duration_s, rate_hz) for --seed, cycled; six covers every rate
    SEED_RECORDINGS = ((5, 10000), (10, 2000), (30, 10000), (10, 1000), (5, 20000), (30, 5000))

    def seed(self, n):
        """Adds n finished recordings (SEED_RECORDINGS) so the app has data at startup."""
        with self.lock:
            for k in range(n):
                rec_id = self.next_id
                self.next_id += 1
                duration_s, rate_hz = self.SEED_RECORDINGS[k % len(self.SEED_RECORDINGS)]
                rate_mhz = ACTUAL_RATES_MHZ[rate_hz]
                start = self.now_utc_us() - (n - k) * 3600 * 1000000
                data = build_irec(rec_id, rate_mhz, duration_s * rate_mhz // 1000, start,
                                  self._is_overrun(rec_id))
                self.recordings[rec_id] = data
                self._write_file(rec_id, data)
            self._save_meta()


# --- HTTP layer ------------------------------------------------------------

DOWNLOAD_CHUNK = 4096
MAX_BODY_BYTES = 512


def _reject_constant(name):
    raise ValueError(name)  # NaN / Infinity aren't JSON (cJSON rejects them)

RECORDING_PATH = re.compile(r"/api/recordings/([^/]+)")

# (method, path) -> (route name, handler method name)
ROUTES = {
    ("GET", "/api/status"): (ROUTE_STATUS, "h_status"),
    ("POST", "/api/record"): ("RecordStart", "h_record_start"),
    ("POST", "/api/record/stop"): (ROUTE_RECORD_STOP, "h_record_stop"),
    ("GET", "/api/recordings"): ("RecordingsList", "h_recordings_list"),
    ("DELETE", "/api/recordings"): ("RecordingsDeleteAll", "h_recordings_delete_all"),
    ("GET", "/api/recordings/{id}"): ("RecordingGet", "h_recording_get"),
    ("DELETE", "/api/recordings/{id}"): ("RecordingDelete", "h_recording_delete"),
    ("POST", "/api/time"): ("Time", "h_time"),
    ("POST", "/api/compress"): ("Compress", "h_compress"),
    ("GET", "/api/settings"): (ROUTE_SETTINGS_GET, "h_settings_get"),
    ("POST", "/api/settings"): ("SettingsSet", "h_settings_set"),
    ("GET", "/api/calibration"): ("Calibration", "h_calibration"),
    ("POST", "/api/sleep"): ("Sleep", "h_sleep"),
}
KNOWN_PATHS = {p for _, p in ROUTES}


class ApiError(Exception):
    """Error response: {"error": "<code>"} plus extras such as "field" (spec §8.2)."""

    def __init__(self, error, status=400, **extra):
        super().__init__(error)
        self.status = status
        self.body = dict(error=error, **extra)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    server_version = "IRSensorMock/1.0"
    device = None  # set by make_server
    cors = True
    quiet = False

    # -- plumbing

    def log_message(self, fmt, *args):
        if not self.quiet:
            super().log_message(fmt, *args)

    def send_json(self, code, obj):
        self.send_bytes(code, json.dumps(obj, separators=(",", ":")).encode(), "application/json")

    def send_bytes(self, code, body, content_type, extra_headers=()):
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        if self.cors:
            self.send_header("Access-Control-Allow-Origin", "*")
        for k, v in extra_headers:
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def json_body(self):
        """api.cpp read_json_body: a JSON object of 1–512 bytes."""
        if not 0 < len(self.body) <= MAX_BODY_BYTES:
            raise ApiError("invalid_body")
        try:
            body = json.loads(self.body.decode("utf-8"), parse_constant=_reject_constant)
        except (UnicodeDecodeError, ValueError):
            body = None
        if not isinstance(body, dict):
            raise ApiError("invalid_json")
        return body

    def dropped_while_asleep(self):
        """WiFi is off while asleep: close kept-alive connections without a response."""
        if self.device.asleep:
            self.close_connection = True
            return True
        return False

    def dispatch(self):
        if self.dropped_while_asleep():
            return
        length = int(self.headers.get("Content-Length") or 0)
        self.body = self.rfile.read(length) if length else b""
        path = self.path.split("?", 1)[0].rstrip("/") or "/"

        self.rec_id = None
        key_path = path
        m = RECORDING_PATH.fullmatch(path)
        if m:
            key_path = "/api/recordings/{id}"
        if key_path not in KNOWN_PATHS:
            return self.send_json(404, {"error": "not_found"})
        route = ROUTES.get((self.command, key_path))
        if route is None:
            return self.send_json(405, {"error": "method_not_allowed"})

        route_name, handler = route
        state = self.device.status()["state"]
        if not route_allowed(state, route_name):
            return self.send_json(409, {"error": "busy", "state": state})

        try:
            if m:
                self.rec_id = parse_id(m.group(1))
                if self.rec_id is None:
                    raise ApiError("not_found", 404)
            getattr(self, handler)()
        except ApiError as e:
            self.send_json(e.status, e.body)
        except Busy as e:
            self.send_json(409, {"error": "busy", "state": e.state})

    do_GET = do_POST = do_DELETE = dispatch

    def do_OPTIONS(self):
        # CORS preflight for browser-based app development
        if self.dropped_while_asleep():
            return
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Content-Length", "0")
        self.end_headers()

    # -- handlers

    def h_status(self):
        self.send_json(200, self.device.status())

    def h_record_start(self):
        body = self.json_body()
        rate_hz = get_uint(body.get("sample_rate_hz"))
        duration_s = get_uint(body.get("duration_s"))
        if rate_hz is None:
            raise ApiError("invalid_rate")
        if duration_s is None:
            raise ApiError("invalid_duration")
        rec_id, err = self.device.start_recording(rate_hz, duration_s)
        if err:
            raise ApiError(err, 507 if err == "insufficient_space" else 400)
        self.send_json(200, {"id": rec_id, "duration_s": duration_s, "sample_rate_hz": rate_hz})

    def h_record_stop(self):
        rec_id = self.device.stop_recording()
        if rec_id is None:  # finished on its own between the state check and now
            raise Busy(self.device.status()["state"])
        self.send_json(200, {"id": rec_id})

    def h_recordings_list(self):
        self.send_json(200, {"recordings": self.device.list_recordings()})

    def h_recording_get(self):
        data = self.device.get_recording(self.rec_id)
        if data is None:
            return self.send_json(404, {"error": "not_found"})
        # Streamed in 4 KB chunks with no Content-Length, like httpd_resp_send_chunk
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Disposition",
                         'attachment; filename="%s"' % filename_for_id(self.rec_id))
        self.send_header("Transfer-Encoding", "chunked")
        if self.cors:
            self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        for i in range(0, len(data), DOWNLOAD_CHUNK):
            chunk = data[i:i + DOWNLOAD_CHUNK]
            self.wfile.write(b"%x\r\n%s\r\n" % (len(chunk), chunk))
        self.wfile.write(b"0\r\n\r\n")

    def h_recording_delete(self):
        if not self.device.delete_recording(self.rec_id):
            return self.send_json(404, {"error": "not_found"})
        self.send_json(200, {"deleted": 1})

    def h_recordings_delete_all(self):
        body = self.json_body()
        if body != {"confirm": "DELETE_ALL"}:
            raise ApiError("confirmation_required")
        self.send_json(200, {"deleted": self.device.delete_all()})

    def h_time(self):
        utc_us = get_uint(self.json_body().get("utc_us"), INT64_MAX)
        if utc_us is None or not is_plausible_utc_us(utc_us):
            raise ApiError("invalid_time")
        self.device.set_time(utc_us)
        self.send_json(200, {"utc_us": self.device.now_utc_us()})

    def h_compress(self):
        self.send_json(200, {"queued": self.device.start_compress()})

    def h_settings_get(self):
        self.send_json(200, self.device.get_settings())

    def h_settings_set(self):
        settings, err = self.device.update_settings(self.json_body())
        if err:
            raise ApiError(err[0], field=err[1])
        self.send_json(200, settings)

    def h_sleep(self):
        self.device.request_sleep()
        self.send_json(200, {"sleeping": True})

    def h_calibration(self):
        if self.server.calibration is None:
            raise ApiError("calibration_unavailable", 503)
        self.send_json(200, {"attenuation_db": ATTENUATION_DB, "mv": self.server.calibration})


def make_server(host="127.0.0.1", port=8080, device=None, cors=True, quiet=False,
                calibration=True):
    """Builds (not starts) the HTTP server. port=0 picks a free port."""
    handler = type("BoundHandler", (Handler,), {
        "device": device or Device(),
        "cors": cors,
        "quiet": quiet,
    })
    httpd = ThreadingHTTPServer((host, port), handler)
    httpd.daemon_threads = True
    httpd.calibration = calibration_table() if calibration else None
    return httpd


class DeviceServer:
    """Runs the HTTP server and takes it down while the device sleeps.

    Asleep, the listening socket is closed (connection refused) and kept-alive
    connections are dropped. wake() — or wake_after seconds — brings it back on
    the same port.
    """

    def __init__(self, device, host="127.0.0.1", port=8080, wake_after=None, **server_kw):
        self.device = device
        self.host = host
        self.port = port
        self.wake_after = wake_after
        self.server_kw = server_kw
        self.httpd = None
        self.lock = threading.Lock()
        device.on_sleep.append(self._sleep)

    def start(self):
        with self.lock:
            self.httpd = make_server(self.host, self.port, self.device, **self.server_kw)
            self.port = self.httpd.server_address[1]  # resolve port 0 once, keep it on wake
            threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    def stop(self):
        with self.lock:
            if self.httpd:
                self.httpd.shutdown()
                self.httpd.server_close()
                self.httpd = None

    def _sleep(self):
        self.stop()
        self._log("asleep — %s" % ("waking in %g s" % self.wake_after if self.wake_after
                             else "send SIGUSR1 or restart to wake"))
        if self.wake_after:
            t = threading.Timer(self.wake_after, self.wake)
            t.daemon = True
            t.start()

    def wake(self):
        if not self.device.asleep:
            return
        self.device.wake()
        self.start()
        self._log("awake on port %d" % self.port)


    def _log(self, msg):
        if not self.server_kw.get("quiet"):
            log(msg)


def log(msg):
    print(time.strftime("[%H:%M:%S] ") + msg, flush=True)


def main(argv=None):
    p = argparse.ArgumentParser(description="Mock IR sensor device REST API (spec.md §8.2)")
    p.add_argument("--host", default="127.0.0.1",
                   help="bind address (0.0.0.0 to reach it from another machine)")
    p.add_argument("--port", type=int, default=8080)
    p.add_argument("--data-dir", help="persist recordings, ID counter and settings here")
    p.add_argument("--seed", type=int, default=0, metavar="N",
                   help="add N finished recordings at startup")
    p.add_argument("--capacity", type=int, default=25 * 1024 * 1024, metavar="BYTES",
                   help="storage capacity for free_bytes (default 25 MiB)")
    p.add_argument("--time-scale", type=float, default=1.0, metavar="X",
                   help="run device time X times faster than real time")
    p.add_argument("--overrun-every", type=int, default=0, metavar="N",
                   help="mark every Nth recording ID as overrun (flags bit 1, samples dropped)")
    p.add_argument("--unsynced-clock", action="store_true",
                   help="start the device clock at the epoch until POST /api/time")
    p.add_argument("--no-calibration", action="store_true",
                   help="answer GET /api/calibration with 503, as a chip without eFuse data")
    p.add_argument("--wake-after", type=float, metavar="S",
                   help="after POST /api/sleep, wake again after S seconds "
                        "(default: stay asleep until SIGUSR1 or restart)")
    p.add_argument("--no-cors", action="store_true", help="omit CORS headers")
    p.add_argument("--quiet", action="store_true", help="no request log")
    args = p.parse_args(argv)

    device = Device(data_dir=args.data_dir, capacity_bytes=args.capacity,
                    time_scale=args.time_scale, unsynced_clock=args.unsynced_clock,
                    overrun_every=args.overrun_every)
    if args.seed:
        device.seed(args.seed)
    server = DeviceServer(device, args.host, args.port, wake_after=args.wake_after,
                          cors=not args.no_cors, quiet=args.quiet,
                          calibration=not args.no_calibration)
    server.start()
    if hasattr(signal, "SIGUSR1"):
        # Handlers run on the main thread, which mustn't block in httpd.shutdown()
        signal.signal(signal.SIGUSR1,
                      lambda *_: threading.Thread(target=server.wake, daemon=True).start())
    log("IR sensor mock on http://%s:%d/api/status (%d recordings, pid %d)"
        % (args.host, server.port, len(device.recordings), os.getpid()))
    try:
        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        pass
    finally:
        server.stop()


if __name__ == "__main__":
    main()
