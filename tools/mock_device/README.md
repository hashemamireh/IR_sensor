# Mock IR sensor device

A stand-in for the device's REST API (`spec.md` §8.2), so the tablet app can be
developed without hardware. It is plain Python 3 using only the standard library,
and it doubles as a runnable copy of the API spec.

It follows the firmware's core library (`firmware/lib/irsensor_core`):

- JSON shapes from `api_model.cpp` and `settings.cpp`.
- 409 gating from `api::route_allowed`.
- Validation and error strings from `recording::validate`.
- The IREC header layout from `irec.cpp`.

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
| `--unsynced-clock` | Start the device clock at the Unix epoch, as after a full power loss, until `POST /api/time` is called. By default the mock uses the host clock. |
| `--no-calibration` | Answer `GET /api/calibration` with `503 calibration_unavailable`, like a chip without eFuse calibration data. |
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
- the response shapes and error codes in §8.2 "Response Formats"
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

**Recordings.** These are valid IREC v1 files:

- A 32-byte little-endian header that stores the actual rate in mHz. Until rates
  are measured on hardware, it equals the nominal rate, as in `recording.cpp`.
- `zlib.crc32` of the uncompressed `uint16_le` samples.
- Compressing a file sets flag bit 0 and zlib-compresses the payload. The CRC
  doesn't change.

The samples are synthetic 12-bit data:

- A dark baseline of about 30–80 counts, with slow drift and Gaussian noise
  (σ ≈ 3 counts).
- IR pulses of 2–400 ms and 1200–3600 counts, with about 150 µs edges, a small
  overshoot and ripple, every 0.3–2.5 s.
- The data is deterministic per recording ID.

A stopped recording holds `floor(elapsed × rate)` samples.

**Downloads.** These are sent chunked in 4 KB pieces with no `Content-Length`, as the device does with `httpd_resp_send_chunk`.

**Delete-all.** It runs within the request and the device stays `idle`.

**Calibration.** The table is roughly linear, 0 → 2200 mV, with a slight bow (at
most 8 mV). It is monotonic, with 4096 entries at `attenuation_db: 6`.

**Status extras.**

- `battery` drains slowly from 82 %.
- `wifi.ssid` is `IRSensor-MOCK`.
- `client_connected` is always `true`.

## Ambiguities in the spec, and what the mock does

Response bodies, error codes and status codes follow the "Response Formats" part of
`spec.md` §8.2. That part was added while this mock was being written, and the
mock tracks the working-tree version of the spec. The points below are still open
in the spec and the firmware sources. The firmware should settle each one, and the
spec should record the decision.

1. **`POST /api/record` fields.** Both `duration_s` and `sample_rate_hz` are required.
   If either is missing or isn't an integer, the mock returns `invalid_body`. It
   doesn't fall back to the `default_*` settings, though the device's "Quick Start"
   does. A zero or negative duration returns `invalid_duration`.
2. **`POST /api/record/stop` while idle.** §8.2 only defines the 409 rules for
   non-idle states. `api::route_allowed` allows stop only while recording, so when
   idle the mock answers `409 {"error":"busy","state":"idle"}`. The same applies
   while saving or compressing.
3. **Order of checks.** The mock checks the 409 rule before parsing the body or ID.
   So a bad ID or body during a recording gets 409, not 400 or 404.
4. **Malformed recording IDs.** The error table has no code for a path segment that
   isn't 1–5 digits (`recording::parse_id` fails). The mock answers
   `404 {"error":"not_found"}`.
5. **Unknown paths and wrong methods.** These aren't in the error table. The mock
   answers `404 not_found` and `405 {"error":"method_not_allowed"}`.
   `esp_http_server`'s built-in 404 and 405 responses are plain text unless the
   firmware overrides them.
6. **What "exactly" means for the delete-all body.** The mock compares the parsed
   JSON: it must equal `{"confirm":"DELETE_ALL"}` with no extra keys. Whitespace
   doesn't matter. It doesn't compare bytes. Anything else, including an empty or
   non-JSON body, gets `400 confirmation_required`.
7. **`POST /api/compress` with nothing to compress.** The mock returns
   `{"queued": 0}` and stays `idle`.
8. **`POST /api/time` errors.** A missing or non-integer `utc_us` gets
   `invalid_body`. A value outside 2024–2100 (`api::is_plausible_utc_us`) gets
   `invalid_time`.
9. **When the recording ID is allocated.** The status shows `recording.id` during
   capture, so the ID is taken from the counter at start. A recording that never
   gets saved would leave a gap, which §4.6 allows.
10. **Very short or immediately stopped recordings.** The mock saves them even if
    they have 0 samples. The firmware might discard them instead.
11. **WiFi SSID and password.** App feature §10.3.6 lists them as remote settings,
    but they aren't in `settings.cpp` or the settings shape in §8.2. The mock
    rejects them as `unknown_setting`.
12. **CORS.** The spec doesn't mention it. A fetch from the Tauri webview to
    `192.168.4.1` is cross-origin unless the app goes through the Tauri HTTP
    plugin (Rust side). The mock sends `Access-Control-Allow-Origin: *` and answers
    `OPTIONS` preflights, so browser-based development works. The real firmware
    sends neither.
13. **Time before sync.** The real clock is wrong until the first
    `POST /api/time`. The mock uses the host clock unless you pass
    `--unsynced-clock`.
14. **Recording ID limit.** Filenames have 5 digits, so the highest ID is 99999
    (`recording::MAX_ID`). The spec doesn't say what happens after that, and the
    mock doesn't enforce it.
15. **What `free_bytes` counts.** The mock subtracts only the file sizes from the
    capacity. It doesn't model LittleFS overhead, and it doesn't reserve space for
    a recording in progress.
16. **Codes the mock never returns.** It never produces `500 storage_error`.
    `503 calibration_unavailable` only appears with `--no-calibration`.
