# Mock IR sensor device

A stand-in for the device's REST API (`spec.md` §8.2), so the tablet app can be
developed without hardware. It is plain Python 3 using only the standard library,
and it doubles as a runnable copy of the API spec.

It follows the firmware's core library (`firmware/lib/irsensor_core`):

- JSON shapes from `api_model.cpp` and `settings.cpp`.
- 409 gating from `api::route_allowed`.
- Validation and error strings from `recording::validate`.
- The IREC header layout from `irec.cpp`.
- Request parsing and error precedence from the handlers in `firmware/src/api.cpp`.

If either side changes, update the other.

## Run

```bash
python3 tools/mock_device/server.py --port 8080
```

Then poll `http://127.0.0.1:8080/api/status`.

| Option | Effect |
| :--- | :--- |
| `--host 0.0.0.0` | Listen on all interfaces, e.g. so a real tablet on the LAN can reach it. The default is `127.0.0.1`. |
| `--port N` | Port to listen on. Default 8080. |
| `--seed N` | Start with N finished recordings (5/10/30 s at 10 kHz), so the app has something to list and graph straight away. |
| `--data-dir DIR` | Persist recordings (`DIR/00042.irec`, the same names as on the device), the ID counter and the settings (`DIR/device.json`) across restarts. Without it, all state is in memory. |
| `--time-scale X` | Run device time X× faster: recording progress, saving and compression. Handy for quick demos. |
| `--capacity BYTES` | Storage size used for `free_bytes` and the space check. Default 25 MiB. Make it small to test `insufficient_space`. |
| `--overrun-every N` | Mark every Nth recording ID as overrun: flags bit 1 is set, `"overrun": true` appears in the list, and 1–3 gaps of about 50 ms of samples are dropped. Use it to exercise the app's overrun warning. |
| `--unsynced-clock` | Start the device clock at the Unix epoch, as after a full power loss, until `POST /api/time` is called. By default the mock uses the host clock. |
| `--no-calibration` | Answer `GET /api/calibration` with `503 calibration_unavailable`, like a chip without eFuse calibration data. |
| `--wake-after S` | After `POST /api/sleep`, wake again after S seconds of real time. By default the mock stays asleep until it gets `SIGUSR1` or is restarted. |
| `--no-cors` | Leave out the `Access-Control-Allow-Origin: *` header. |
| `--quiet` | Don't log requests. |

Example session:

```bash
curl -s localhost:8080/api/status
curl -s -XPOST localhost:8080/api/record -d '{"duration_s":10,"sample_rate_hz":10000}'
curl -s localhost:8080/api/recordings
curl -s -o 00001.irec localhost:8080/api/recordings/1
curl -s -XDELETE localhost:8080/api/recordings -d '{"confirm":"DELETE_ALL"}'
```

## Test

```bash
python3 tools/mock_device/test_mock.py -v
```

The tests run with a 10× time scale and take about 10 s. They cover:

- record → status progress → saving → download → CRC check → compress → same CRC
  after decompression
- stopping early
- every 409 case during recording and saving
- validation, including the 300 s and 120 s caps and free space
- delete-all confirmation, single delete and 404s
- the response shapes and error codes in §8.2 "Response Formats", and the body
  parsing rules of `api.cpp`
- overrun recordings
- compression skipping files that don't shrink
- deep sleep: 409 while busy, unreachable while asleep, waking by call and by
  `wake_after`
- IDs never reused
- time sync, settings, calibration and persistence

## Behaviour

**State machine.**

- `POST /api/record` moves `idle` to `recording`. Status reports `recording.elapsed_s`
  counting up in real time.
- The recording ends when `total_s` is reached or on `POST /api/record/stop`. The
  state is then `saving` for about 1 s, plus about 1 s per 6 MB, and then `idle`.
- `POST /api/compress` moves `idle` to `compressing`. Status reports
  `compress.done`/`total`. Each file takes about 0.5 s plus about 1 s per MB.
  With nothing to compress it returns `{"queued": 0}` and stays `idle`.
- A recording that doesn't get smaller is left uncompressed, with its flag unset.
  As in `compressor.cpp`, it's queued again on the next compress run.
- `POST /api/sleep` (idle only) answers `{"sleeping": true}`. About 0.5 s later the
  device "sleeps": the listening socket closes, so new connections are refused,
  and kept-alive connections are dropped without a reply. That is how the app
  sees WiFi go away. It wakes on `SIGUSR1` (`kill -USR1 <pid>`, the pid is in the
  startup line), after `--wake-after S`, or on a restart, back on the same port.
  Recordings, settings and the clock survive sleep, as they do on the device
  (flash, NVS, RTC).

**Recordings.** These are valid IREC v1 files:

- A 32-byte little-endian header that stores the actual rate in mHz. Until rates
  are measured on hardware, it equals the nominal rate, as in `recording.cpp`.
- `zlib.crc32` of the uncompressed `uint16_le` samples.
- Compressing a file sets flag bit 0 and zlib-compresses the payload. The CRC
  doesn't change.
- Flag bit 1 (overrun) is 0 unless `--overrun-every` selects the recording.
  Compression keeps it.
- Byte 0x1A holds the ADC attenuation in tenths of a dB. It is always 120
  (12 dB), the same value the firmware writes. Bytes 0x1B–0x1F are reserved and
  zero. In the list, this byte becomes `"attenuation_db": 12`, written without a
  trailing `.0` as `JsonWriter::num_tenths` does, so 25 would be `2.5`.
- `firmware/tools/check_irec.py` accepts mock files. It reports a deliberate
  overrun as FAIL.

The samples are synthetic 12-bit data:

- Counts are scaled for 12 dB, at about 0.8 mV per count.
- A dark baseline of about 15–45 mV (about 20–55 counts), with slow drift and
  Gaussian noise (σ ≈ 2 counts).
- IR pulses of 2–400 ms and about 0.65–1.95 V (about 800–2400 counts), with
  about 150 µs edges, a small overshoot and ripple, every 0.3–2.5 s.
- Values are clipped at the OPT101's ~2.15 V ceiling (about 2670 counts).
- The data is deterministic per recording ID.

A stopped recording holds `floor(elapsed × rate)` samples.

**Downloads.** These are sent chunked in 4 KB pieces with no `Content-Length`, as the device does with `httpd_resp_send_chunk`.

**Delete-all.** It runs within the request and the device stays `idle`.

**Calibration.** The table is roughly linear, 0 → 3300 mV, with a slight bow (at
most 12 mV). It is monotonic, with 4096 entries at `attenuation_db: 12`.

**Status extras.**

- `battery` drains slowly from 82 %.
- `wifi.ssid` is `IRSensor-MOCK`.
- `client_connected` is always `true`.

## Request parsing (as in `firmware/src/api.cpp`)

- **409 first.** The 409 rule is checked before the body or ID is read, so a bad
  request during a recording gets 409, not 400 or 404.
- **JSON bodies.** A body must be a JSON object of 1–512 bytes. An empty or larger
  body gets `invalid_body`. Anything that isn't a JSON object, including malformed
  JSON, an array, or `NaN`, gets `invalid_json`.
- **Numbers.** These must be whole and non-negative. A float such as `30.0` is
  accepted, as cJSON does.
- **`POST /api/record`.** Both fields are required. If `sample_rate_hz` is missing
  or isn't a valid number, the result is `invalid_rate`, and that is checked
  first. Then the same applies to `duration_s` with `invalid_duration`. After that,
  `recording::validate` runs: an unsupported rate gives `invalid_rate`, a duration
  over the cap or equal to 0 gives `invalid_duration`, and lack of space gives
  `507 insufficient_space`.
- **`POST /api/time`.** A missing, non-integer or implausible `utc_us` (outside
  2024–2100) gets `invalid_time`.
- **`DELETE /api/recordings`.** The body must be an object with exactly one key,
  `"confirm": "DELETE_ALL"`. Any other object gets `confirmation_required`.
- **`POST /api/settings`.** The first bad key, in body order, is reported as
  `invalid_setting` or `unknown_setting`, with `"field"`.
- **Malformed recording IDs.** A segment that isn't 1–5 digits gets `404 not_found`.

## Ambiguities in the spec, and what the mock does

These are still open in `spec.md` and the firmware. The firmware should settle
each one, and the spec should record the decision.

1. **Unknown paths and wrong methods.** The spec's error table doesn't cover them.
   The mock answers `404 not_found` and `405 {"error":"method_not_allowed"}`.
   `esp_http_server`'s built-in 404 and 405 responses are plain text unless the
   firmware overrides them.
2. **`POST /api/record/stop` while idle.** §8.2 only defines the 409 rules for
   non-idle states. `api::route_allowed` allows stop only while recording, so when
   idle the mock answers `409 {"error":"busy","state":"idle"}`. The same applies
   while saving or compressing.
3. **Time between `POST /api/sleep` and sleeping.** Requests in the ~0.5 s before
   the device sleeps are still served. If one of them starts a recording, the
   mock cancels the sleep. The spec doesn't say what the firmware does.
4. **When the recording ID is allocated.** The status shows `recording.id` during
   capture, so the ID is taken from the counter at start. A recording that never
   gets saved would leave a gap, which §4.6 allows.
5. **Very short or immediately stopped recordings.** The mock saves them even if
   they have 0 samples. The firmware might discard them instead.
6. **WiFi SSID and password.** App feature §10.3.6 lists them as remote settings,
   but they aren't in `settings.cpp` or the settings shape in §8.2. The mock
   rejects them as `unknown_setting`.
7. **CORS.** The spec doesn't mention it. A fetch from the Tauri webview to
   `192.168.4.1` is cross-origin unless the app goes through the Tauri HTTP
   plugin (Rust side). The mock sends `Access-Control-Allow-Origin: *` and answers
   `OPTIONS` preflights. The real firmware sends neither.
8. **Time before sync.** The real clock is wrong until the first
   `POST /api/time`. The mock uses the host clock unless you pass
   `--unsynced-clock`.
9. **Recording ID limit.** Filenames have 5 digits, so the highest ID is 99999
   (`recording::MAX_ID`). The spec doesn't say what happens after that, and the
   mock doesn't enforce it.
10. **Old `--data-dir` folders.** Files written by earlier versions of the mock
    have 0 in byte 0x1A, so they are listed with `attenuation_db: 0`. Delete the
    folder to regenerate it.
11. **What `free_bytes` counts.** The mock subtracts only the file sizes from the
    capacity. It doesn't model LittleFS overhead, and it doesn't reserve space for
    a recording in progress.
12. **Codes the mock never returns.** It never produces `500 storage_error`.
    `503 calibration_unavailable` only appears with `--no-calibration`.
