# Board Bring-Up Checklist

The firmware was written and built without the board, so nothing here has run on hardware yet.
Work through this top to bottom: each phase only relies on what the previous one proved. Tick
items off as you go and fill in the [results table](#results) at the end — several numbers feed
straight back into the code.

- **Phase 1** uses the `irsensor_synthetic` build: the recorder generates a fake signal (5 ms
  pulse every 250 ms), so everything except the ADC can be checked without the OPT101.
- **Phase 2** switches to the real build with the OPT101 connected.
- **Phase 3** is power measurement.

All commands run from `firmware/`. Helper scripts need only Python 3's standard library.

---

## Phase 0 — Before Power

- [ ] Wiring matches [`wiring_diagram.md`](../wiring_diagram.md). In particular:
  - Nothing on `N/15` (GPIO15 is PSRAM chip-select on this board).
  - OLED and MAX17043 share SDA = GPIO23 / SCL = GPIO24.
  - Encoder SW on GPIO5 (the deep-sleep wake pin), CLK on GPIO8, DT on GPIO10.
  - MAX17043 CELL goes to VBAT, not 3V3; its VCC goes to 3V3 (VCC–+ link on the module cut).
  - OLED VCC goes to the 5V pin (Left Pin 14). Only safe for a module whose SDA/SCL pull-ups go
    to its own 3.3V regulator — see `wiring_diagram.md` before swapping in a different module.
- [ ] The I2C bus has external pull-ups (OLED and MAX17043 breakouts usually include 4.7–10 kΩ).
  The ESP32's internal ones (~45 kΩ) are too weak for 400 kHz on their own.
- [ ] OPT101 board has its bypass capacitor (0.01–0.1 µF on the supply pin) — many breakouts
  already include one.
- [ ] Use the **right-hand "UART" USB-C port** (CH343 bridge). The firmware's log console is
  UART0, and this port keeps working while the ESP32 is in deep sleep.

## Phase 1 — Synthetic Build

### 1.1 Flash and boot

- [ ] Find the board's serial port:

```bash
pio device list
```

- [ ] Erase the whole flash once, so NVS and the storage partition start clean:

```bash
pio run -e irsensor_synthetic -t erase
```

- [ ] Flash and open the monitor:

```bash
pio run -e irsensor_synthetic -t upload -t monitor
```

- [ ] The boot log shows, in this order (timestamps and some values will differ):

```
I (...) irsensor: === IR Sensor Device starting ===
I (...) irsensor: NVS initialized
I (...) storage: LittleFS mounted: ... KB used / ~29600 KB total
I (...) settings: Loaded: brightness=medium timeout=60s rate=10000Hz duration=30s
I (...) recordings: 0 recordings, next ID 1
I (...) calibration: Curve-fitting calibration ready (12 dB attenuation)
I (...) irsensor: I2C bus initialized
I (...) battery: MAX17043 ready (0x36)
I (...) led: RGB LED initialized
I (...) encoder: Encoder ready (CLK=8, DT=10, SW=5)
I (...) wifi: Generated new SoftAP password
I (...) wifi: SoftAP started — SSID: IRSensor-XXXX, password: xxxxxxxxxx
W (...) recorder: SYNTHETIC build — recordings contain generated samples, not ADC data
I (...) recorder: Reserved 5859 KB of PSRAM for recordings
I (...) compressor: Ready (... KB PSRAM)
I (...) api: HTTP server started — 13 endpoints
I (...) display: SSD1306 OLED ready (0x3C)
I (...) ui: UI started
I (...) irsensor: === All subsystems initialized ===
```

| If you see | Likely cause |
| :--- | :--- |
| Boot loop / no PSRAM / `Can't reserve ... PSRAM` | PSRAM config — check `CONFIG_SPIRAM_MODE_QUAD` in `sdkconfig.irsensor_synthetic` |
| `Curve-fitting calibration unavailable` | Chip has no ADC calibration in eFuse — `/api/calibration` will return 503; recordings still work |
| `MAX17043 not responding` | Wiring, address, missing pull-ups, or gauge not powered |
| `OLED not responding` | Same as above for the OLED (0x3C; some modules are 0x3D — see `pins.h`) |
| `PCNT setup failed` | Encoder pins — rotation disabled, button still works |

### 1.2 Display, encoder, button, LED

- [ ] On first boot the OLED shows the **WiFi** screen with SSID and password (matching the log).
  Press to dismiss → home screen ("Ready", "0 recordings", "Press for menu").
- [ ] With the yellow strip at the top, text reads the right way up and the status bar sits in
  the yellow strip. If instead the image is upside down (or the status bar is in the blue part
  while the yellow strip is at the bottom), toggle `OLED_FLIP` in `include/config.h`.
- [ ] Status bar shows a battery reading after ~1 s (`--` before that), free space (~29 MB) and `AP`.
- [ ] One encoder click moves exactly one menu item. If it moves two or needs two clicks, change
  `ENCODER_COUNTS_PER_DETENT` (try 2). If clockwise moves *up*, set `ENCODER_DIRECTION = -1`.
- [ ] Short press selects; holding ~1 s goes back (long press fires while still held).
- [ ] LED is green.
- [ ] Settings → Brightness: Low / Medium / High visibly change the OLED.
- [ ] Settings → Timeout → 30s: screen turns off after 30 s idle; any click turns it back on
  (that click is otherwise ignored).

### 1.3 WiFi and API

- [ ] Join `IRSensor-XXXX` from your Mac with the password from the OLED. The log shows
  `wifi: Client connected`, the status bar `AP` turns inverted, and the LED turns cyan.
- [ ] Status:

```bash
curl -s http://192.168.4.1/api/status
```

  Expect `{"state":"idle","battery":{"mv":...,"pct":...},"free_bytes":...,"recording_count":0,"wifi":{"ssid":"IRSensor-XXXX","client_connected":true}}`.
- [ ] Sync the clock (log: `api: Clock set from tablet`):

```bash
curl -s -X POST http://192.168.4.1/api/time -d "{\"utc_us\": $(python3 -c 'import time; print(int(time.time() * 1e6))')}"
```

- [ ] Settings round-trip — the response is the full settings object with `"brightness":"high"`,
  and the OLED gets brighter:

```bash
curl -s -X POST http://192.168.4.1/api/settings -d '{"brightness":"high"}'
```

- [ ] An invalid setting is refused with `400 {"error":"invalid_setting","field":"brightness"}`:

```bash
curl -s -i -X POST http://192.168.4.1/api/settings -d '{"brightness":"max"}'
```

- [ ] Calibration table: 200, `"attenuation_db":12`, 4096 values rising from ~0 to over 3000 mV
  (prints the first/last few):

```bash
curl -s http://192.168.4.1/api/calibration | python3 -c 'import json,sys; t=json.load(sys.stdin)["mv"]; print(len(t), t[:3], t[-3:])'
```

### 1.4 Recording via the API

- [ ] Start a 30 s recording at 10 kHz → `{"id":1,"duration_s":30,"sample_rate_hz":10000}`:

```bash
curl -s -X POST http://192.168.4.1/api/record -d '{"duration_s":30,"sample_rate_hz":10000}'
```

- [ ] While it runs:
  - OLED is frozen on "Recording #1", LED pulses red.
  - `/api/status` shows `"state":"recording"` with `elapsed_s` counting up.
  - Other requests get `409 {"error":"busy","state":"recording"}`, e.g.:

```bash
curl -s -i http://192.168.4.1/api/recordings
```

- [ ] Log at the end (note the timestamps between the last two lines — that's the save time):

```
I (...) recorder: Recording 1: 30 s at 10000 Hz
I (...) recorder: Captured 300000 samples in 30.0xx s — ~99xx.x Hz (nominal 10000 Hz)
I (...) recorder: Saved /data/recordings/00001.irec — 300000 samples, CRC xxxxxxxx
```

- [ ] OLED shows "Saving #1" briefly, then home with "Last #1 30.0s 10kHz / Saved"; LED back to cyan.
- [ ] List and download:

```bash
curl -s http://192.168.4.1/api/recordings
```

```bash
curl -s -o 00001.irec http://192.168.4.1/api/recordings/1
```

- [ ] Check the file — every line `PASS`, 119 pulses 5 ms wide (the one at t = 0 isn't counted),
  every period 250 ms:

```bash
python3 tools/check_irec.py 00001.irec --period-ms 250
```

  A failing period check here means samples were dropped or duplicated between capture and file.

### 1.5 Longest recording

- [ ] 120 s at 20 kHz (the largest buffer, 4.8 MB). Note the save time from the log timestamps
  and check the file as above:

```bash
curl -s -X POST http://192.168.4.1/api/record -d '{"duration_s":120,"sample_rate_hz":20000}'
```

- [ ] During the save, `/api/status` keeps answering with `"state":"saving"`.
- [ ] Over the cap is refused with `400 invalid_duration`:

```bash
curl -s -i -X POST http://192.168.4.1/api/record -d '{"duration_s":121,"sample_rate_hz":20000}'
```

### 1.6 Recording from the device, stopping early

- [ ] Menu → Record → Start: recording screen appears; press once → it stops early and saves a
  shorter recording.
- [ ] Start one from the API, then stop it from the API → `{"id":N}`:

```bash
curl -s -X POST http://192.168.4.1/api/record/stop
```

- [ ] Menu → Recordings lists them newest first with UTC times; View Info shows the details.

### 1.7 Persistence

- [ ] Press the board's RST button. After boot: `recordings: N recordings, next ID N+1`, settings
  kept, no new password generated, and recording timestamps are still sensible (the clock
  survives a reset but not a power loss).

### 1.8 Delete

- [ ] Delete one from the menu (Recordings → #N → Delete → Yes); the list and count update.
- [ ] Delete via the API → `{"deleted":1}`, then a second time → `404 not_found`:

```bash
curl -s -i -X DELETE http://192.168.4.1/api/recordings/2
```

- [ ] Delete-all with the wrong confirmation is refused (`400 confirmation_required`; with no
  body at all it's `400 invalid_body`):

```bash
curl -s -i -X DELETE http://192.168.4.1/api/recordings -d '{"confirm":"yes"}'
```

  (Keep at least two recordings for the compression test before running the real delete-all
  later: `curl -s -X DELETE http://192.168.4.1/api/recordings -d '{"confirm":"DELETE_ALL"}'`.)

### 1.9 Compression

- [ ] Menu → Storage → Compress All → Yes (or `curl -s -X POST http://192.168.4.1/api/compress`).
  Home shows "Compressing x/y"; the log shows per-file sizes and a summary:

```
I (...) compressor: /data/recordings/00001.irec: 600000 → ... bytes
I (...) compressor: Done: N compressed, 0 skipped, 0 failed
```

  Note the time per MB and the ratio in the results table.
- [ ] Download a recording that `/api/recordings` now lists with `"compressed":true` (here ID 1)
  and check it — `(compressed)` in the output, CRC still `PASS`:

```bash
curl -s -o 00001z.irec http://192.168.4.1/api/recordings/1
```

```bash
python3 tools/check_irec.py 00001z.irec --period-ms 250
```

- [ ] Running Compress All again → `{"queued":0}` and the state stays `idle`.

### 1.10 Deep sleep and wake

- [ ] Menu → Deep Sleep → Yes: OLED and LED go dark; log shows
  `power: Entering deep sleep — hold the button ~2 s to wake`; WiFi disappears.
- [ ] A short tap: the chip wakes briefly and logs
  `power: Button released before 2000 ms — back to sleep`.
- [ ] Holding ~2 s: `power: Woken by a long press`, then a normal boot. The status bar battery
  reading appears after ~1 s.
- [ ] `/api/sleep` does the same from the Mac (`{"sleeping":true}`, then WiFi drops):

```bash
curl -s -X POST http://192.168.4.1/api/sleep
```

- [ ] After waking, `/api/status` time is still right: start a short recording and check its
  `start_utc_us` in `/api/recordings` (the clock runs through deep sleep).

## Phase 2 — Real Build With the OPT101

- [ ] Flash the real build (no erase — recordings and settings are kept). The log must **not**
  show the `SYNTHETIC build` warning:

```bash
pio run -e irsensor -t upload -t monitor
```

### 2.1 ADC range

The firmware uses 12 dB attenuation, which covers the full 0–3.3 V, so the OPT101's output
(at most ~2.15 V on a 3.3 V supply) should never clip. Confirm it, and check the calibrated
readings against a meter:

- [ ] Cover the sensor and record 5 s → values near the bottom (tens of counts at most).
- [ ] Hold a steady, bright IR source on the sensor (an IR LED on a resistor, not a remote) and
  record 5 s. `check_irec.py` must not print the "samples at 4095" note; the brightest
  OPT101 output should land around 2000 counts.
- [ ] While it's lit, measure the OPT101's output pin with a multimeter and compare with the
  device's reading: take the mean raw value from `check_irec.py` and look it up in the
  calibration table (replace `1234` with that value):

```bash
curl -s http://192.168.4.1/api/calibration | python3 -c 'import json,sys; t=json.load(sys.stdin)["mv"]; print(t[int(sys.argv[1])])' 1234
```

  They should agree to within ~20–40 mV. A larger gap points at the calibration
  (`/api/calibration` returning 503 means the chip has none) or the wiring.

### 2.2 Actual sample rates (spec §3.5)

- [ ] For each rate, record the longest allowed duration (300 s; 120 s at 20 kHz) and copy the
  measured rate from the log line `Captured ... samples in ... s — ~XXXX.X Hz`. Run each twice;
  the two should agree to within ~0.1 Hz.
- [ ] Put the averages into `ACTUAL_RATES` in `lib/irsensor_core/recording.cpp`, in millihertz
  (e.g. 10004.3 Hz → `10004300`), then rerun `pio test -e native` and reflash.

  The log measures the ADC against the same crystal as the system timer, which is exactly the
  divider error §3.5 is about. The crystal's own ±10–20 ppm tolerance is separate and can't be
  measured without an external reference.

### 2.3 Lost samples under WiFi load

- [ ] Start a 120 s / 20 kHz recording, and meanwhile hammer the status endpoint from the Mac
  (stop it with Ctrl-C after the recording ends):

```bash
while true; do curl -s -o /dev/null http://192.168.4.1/api/status; done
```

- [ ] Download it: `check_irec.py` must report `PASS  no overrun`. If it reports OVERRUN, double
  `ADC_POOL_BYTES` in `include/config.h` (16 KB → 32 KB) and repeat.

### 2.4 Timing sanity check (optional)

- [ ] Record 5 s at 20 kHz while pressing any button on a TV remote pointed at the sensor. Most
  remotes use the NEC protocol, which starts every command with a **9 ms burst and a 4.5 ms
  gap**. The OPT101's bandwidth smooths the 38 kHz carrier into its envelope, so:

```bash
python3 tools/check_irec.py remote.irec --list-pulses
```

  should show a pulse of ~9.0 ms followed ~13.5 ms later (9 + 4.5) by the shorter data pulses.
  This checks the whole chain against an independent timing source, to within the remote's own
  resonator tolerance (~0.5%).

### 2.5 Battery gauge

- [ ] `/api/status` battery mV matches a multimeter across the cell to within ~20 mV.
- [ ] SoC % is plausible for the cell's state (it settles over the first minutes after power-up).

## Phase 3 — Power

- [ ] Unplug USB so the board runs from the battery only (USB would power it otherwise).
- [ ] Put a multimeter on its µA range in series with the battery's positive lead.
- [ ] Measure: **idle** (screen on, WiFi on), **screen off** (after the timeout), **recording**,
  and **deep sleep**.
- [ ] Deep sleep: expect ≈ 150 µA plus whatever the board itself draws (regulator, power LED).
  If the board is low but the total isn't, the OPT101 (≈ 120 µA) is the next target — see spec
  §7.1 on powering it from a GPIO.

Tip: a meter on its µA range can drop enough voltage to brown the board out when it wakes
(WiFi draws 100+ mA). Short the meter while waking it, then remove the short once it's asleep.

---

## Results

Fill in as you go; numbers marked → feed back into code or the spec.

| Item | Value |
| :--- | :--- |
| → OLED orientation (`OLED_FLIP`) | |
| → Encoder counts per click / direction | |
| Brightness levels look right? | |
| Brightest OPT101 output (counts / mV), any clipping? | |
| Device mV vs multimeter on the OPT101 output | |
| → Actual rate @ 1 kHz (Hz) | |
| → Actual rate @ 2 kHz (Hz) | |
| → Actual rate @ 5 kHz (Hz) | |
| → Actual rate @ 10 kHz (Hz) | |
| → Actual rate @ 20 kHz (Hz) | |
| Overrun under WiFi load at 20 kHz? (→ `ADC_POOL_BYTES`) | |
| Save time, 30 s @ 10 kHz (0.6 MB) | |
| Save time, 120 s @ 20 kHz (4.8 MB) | |
| Compression speed (s per MB) and ratio | |
| NEC remote leader burst measured (ms) | |
| Battery mV vs multimeter | |
| Current: idle / screen off / recording | |
| Current: deep sleep | |
