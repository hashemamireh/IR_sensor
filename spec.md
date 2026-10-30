# IR Sensor Device — Project Overview & Architecture

> **Document Purpose**: Single source of truth for all architectural decisions, hardware specs,
> software design, and communication protocols for the IR sensor recording device and its
> companion tablet app.
>
> **Pin assignments** are intentionally omitted here — see the pin allocation document (TBD) for
> GPIO mapping. Hardware reference: [`ESP32-C5-WIFI6-KIT-N32R8.md`](./docs/external/ESP32-C5-WIFI6-KIT-N32R8.md)

---

## 1. Project Goal

Build a battery-powered, handheld IR sensor recording device that:

1. Captures analog readings from an **OPT101 photodiode** (with IR bandpass filter) at
   **1–20 kHz** (default **10 kHz**, 0.1 ms per sample) to achieve **≤1 ms measurement
   precision**.
2. Stores recordings locally on flash storage.
3. Transfers recorded data wirelessly to a **companion tablet app** for visualization, overlay
   comparison, and export.

---

## 2. Hardware Bill of Materials

| Component | Details |
| :--- | :--- |
| **Microcontroller** | Waveshare ESP32-C5-WIFI6-KIT-N32R8 |
| **SoC** | Espressif ESP32-C5 — single-core 32-bit RISC-V @ 240 MHz |
| **Memory** | 384 KB SRAM, 320 KB ROM, 8 MB PSRAM |
| **Flash** | 32 MB SPI NOR Flash |
| **Wireless** | Dual-band Wi-Fi 6 (2.4 + 5 GHz), Bluetooth 5 LE, 802.15.4 |
| **Sensor** | OPT101 monolithic photodiode + transimpedance amplifier, with IR bandpass filter |
| **Fuel Gauge** | MAX17043 I2C LiPo battery fuel gauge (I2C address 0x36) — voltage + state-of-charge via ModelGauge™ |
| **Display** | 0.96" I2C OLED 128×64 (top 16 px yellow, bottom 48 px blue) |
| **Input** | Rotary encoder with push button (rotate left/right, short press, long press) |
| **Status LED** | Onboard WS2812B addressable RGB LED (GPIO27) |
| **Power** | 3.7V LiPo battery via MX1.25 connector; onboard charge management via USB-C |

---

## 3. Sensor & ADC Design

### 3.1. OPT101 Characteristics

| Parameter | Value |
| :--- | :--- |
| Supply voltage | 3.3V (from ESP32 3V3 rail) |
| Bandwidth | 14 kHz (with internal 1 MΩ feedback resistor) |
| Output voltage range | 0 V to ~2.0–2.15 V (Vcc − 1.15 V) |
| Dark output | ~0 mV (millivolt-level offset) |
| Responsivity | 0.45 A/W @ 650 nm |

### 3.2. ADC Configuration

| Parameter | Value |
| :--- | :--- |
| ADC unit | ADC1 (single unit on ESP32-C5, 6 channels) |
| Resolution | 12-bit (0–4095) |
| Attenuation | **12 dB** — covers the full 0–3.3 V input range, which the ESP32-C5 converts accurately end to end. 6 dB was rejected: its full scale is V<sub>ref</sub> ÷ 0.5 and V<sub>ref</sub> varies 1.0–1.2 V between chips, so it tops out anywhere from 2.0 to 2.4 V and could clip the OPT101's ~2.15 V peak on some chips. Cost: ~1.1 mV per count instead of ~0.55 mV (the OPT101's range spans ~2000 counts), irrelevant for timing. Fixed in firmware; recorded in every file header (§4.2) |
| Sampling mode | **Continuous mode with DMA** (GDMA controller) |
| Configurable rates | 1 kHz, 2 kHz, 5 kHz, **10 kHz** (default), 20 kHz (nominal — see §3.5) |
| Channel during recording | OPT101 channel only |

### 3.3. DMA Strategy

The ESP32-C5's GDMA controller offloads ADC sampling entirely to hardware:

- ADC conversions are triggered automatically at the configured sample rate.
- Results are deposited into linked-list descriptor buffers in internal SRAM via DMA — **no CPU
  intervention required per sample**.
- The firmware drains DMA buffers in a high-priority FreeRTOS task, converting each 4-byte DMA
  result (`adc_continuous_parse_data()`) to a `uint16` sample and appending it to a **PSRAM
  recording buffer that holds the entire recording**.
- **Flash is not accessed during a recording — neither written nor read.** On the ESP32-C5,
  flash and PSRAM share the SPI cache, so a flash operation stalls PSRAM access. (Storage usage
  for `/api/status` and the status bar is therefore cached in RAM and refreshed only after files
  are written or deleted.) Buffering the whole recording in PSRAM
  and saving it after capture stops removes that contention entirely (see §4.4 for the resulting
  duration limits).
- The ESP-IDF WiFi driver task runs at priority 23 — above all application tasks — and can
  briefly preempt the drain task on the single core. The internal-SRAM DMA buffers are therefore
  sized to hold **≥50 ms of samples** at the highest rate (~4 KB at 20 kHz × 4 B/result). The size
  is a tunable constant so it can be adjusted on hardware.
- At 10 kHz × 2 bytes/sample = **20 KB/s**, this is trivially within the GDMA's capability
  (hardware supports up to ~250–350 kS/s).

### 3.4. Calibration

- Recordings store **raw ADC counts** (0–4095), not millivolts — full resolution, no per-sample
  conversion cost.
- Raw → mV conversion uses ESP-IDF's **ADC Curve Fitting calibration scheme**
  (`ADC_CALI_SCHEME_VER_CURVE_FITTING`), which is driven by per-chip correction values burned into
  eFuse at the factory. There is no runtime calibration procedure to run.
- Because those eFuse values only exist on the chip, the device exposes a **raw → mV lookup
  table** via `GET /api/calibration` (4096 entries, one per raw count, for the attenuation in
  use). Each recording's header stores which attenuation it used (§4.2), not the table. The
  app stores the table in each downloaded recording's `.json` sidecar (§10.4), so every
  recording can be converted to millivolts offline. The app only applies a table whose
  attenuation matches the file's header.

### 3.5. Actual Sample Rate

- The ADC controller derives its sample clock from the crystal through integer dividers, so the
  actual rate can differ slightly from the requested (nominal) rate. Timing is still
  crystal-accurate (~10–20 ppm); only the label would be wrong — e.g. a true 10 004 Hz labelled as
  10 000 Hz misplaces an event at 60 s by ~24 ms.
- Following DAQ practice (record the coerced/actual rate, not the requested one), the **actual
  rate for each nominal setting is measured on hardware** and stored in firmware as a constant
  table; recordings store the actual rate in **millihertz** (§4.2).
- Until measured on hardware, the actual rate is assumed equal to the nominal rate. The firmware
  logs a rough measured rate (samples ÷ elapsed time) after every recording to help fill the
  table.

---

## 4. Recording & Storage

### 4.1. Storage Backend

- **Filesystem**: LittleFS on a dedicated flash partition.
- **Flash capacity**: ~25 MB usable for recordings (after firmware, partition table, and
  filesystem overhead).
- **Wear considerations**: LittleFS provides built-in wear leveling. At moderate recording
  frequency (tens of recordings per day), flash endurance is not a concern.

### 4.2. Recording File Format (Binary)

Each recording is stored as a single binary file on LittleFS:

```
Header (32 bytes):
  Offset  Size  Field
  0x00    4B    Magic bytes: "IREC" (0x49 0x52 0x45 0x43)
  0x04    1B    Format version (currently 0x01)
  0x05    4B    Actual sample rate in millihertz (uint32_le), e.g. 10 004.3 Hz → 10004300
  0x09    4B    Sample count (uint32_le)
  0x0D    8B    Start timestamp — UTC microseconds since epoch (uint64_le)
  0x15    4B    CRC-32 of the uncompressed sample data (uint32_le) — see §4.5
  0x19    1B    Flags: bit 0 = compressed (zlib)
                       bit 1 = overrun — samples were lost during capture (DMA pool
                               overflow); the timeline is unreliable
  0x1A    1B    ADC attenuation in tenths of a dB (uint8): 0, 25, 60 or 120 — currently always
                120 (12 dB, §3.2); lets a file be converted to mV unambiguously
  0x1B    5B    Reserved (zero-filled)

Data payload:
  If uncompressed: uint16_le[] — raw ADC values, 2 bytes per sample
  If compressed:   zlib-compressed stream of the above uint16_le[]
```

### 4.3. Storage Budget

| Duration | Sample Rate | Raw Size | Max Recordings (in 25 MB) |
| :--- | :--- | :--- | :--- |
| 5 s | 10 kHz | 100 KB | ~250 |
| 30 s | 10 kHz | 600 KB | ~41 |
| 60 s | 10 kHz | 1.2 MB | ~20 |
| 120 s | 10 kHz | 2.4 MB | ~10 |
| 300 s | 10 kHz | 6 MB | ~4 |
| 120 s | 20 kHz | 4.8 MB | ~5 |

20 kHz recordings are capped at 120 s (§4.4).

### 4.4. Recording Constraints

- **Maximum duration**: **300 s** at 1, 2, 5 and 10 kHz; **120 s** at 20 kHz. The whole recording is
  held in PSRAM until capture ends (§3.3); ~6 MB of PSRAM is reserved for this at boot, and 300 s
  at 20 kHz (12 MB) would not fit.
- **Saving**: after capture stops, the PSRAM buffer is written to LittleFS. This takes a few
  seconds for long recordings; the device reports `state: "saving"` and the OLED shows
  "Saving…". A recording interrupted by power loss before it is saved is lost.
- **Pre-recording check**: Before starting, the firmware calculates the required storage
  (`sample_rate × duration × 2 bytes + 32 byte header`) and compares against free space. If
  insufficient, recording is **refused with a warning** (displayed on OLED and returned to the
  app).
- **Compression**: Optional. A manual "Compress All Saved" action is available from the device
  menu (and `POST /api/compress`). Compression uses zlib (the ESP32-C5 ROM's miniz) on stored
  files, in a lowest-priority background task; the device reports `state: "compressing"` with
  progress. Each compressed copy is decompressed and CRC-checked before it replaces the
  original, and a recording that doesn't get smaller is left uncompressed. The app handles both
  compressed and uncompressed files transparently (checks the flags byte in the header).

### 4.5. Data Integrity

- Each recording includes a **CRC-32** checksum (the standard IEEE/zlib polynomial — identical to
  zlib's `crc32()`) — a 4-byte fingerprint used to detect corruption.
- The CRC always covers the **uncompressed** `uint16_le[]` sample data, whether or not the file is
  compressed. The app verifies it after decompressing, so one check covers flash storage,
  transfer and decompression. Compressing a file does not change its CRC. (The zlib stream also
  carries its own Adler-32 checksum over the compressed bytes.)

### 4.6. Recording IDs & Filenames

- Each recording gets a numeric ID from a monotonically increasing counter stored in NVS. IDs
  are never reused, even after deletion.
- Files are stored as `/data/recordings/<id>.irec`, with the ID zero-padded to 5 digits (e.g.
  `/data/recordings/00042.irec`).
- The same ID is used in the REST API (`/api/recordings/{id}`).
- **Index in RAM.** The firmware reads every recording's header from flash once, at boot, and
  keeps them in memory (≈ 40 bytes each), updating the index whenever a recording is saved,
  deleted or compressed. Listing recordings — `GET /api/recordings`, the device's Recordings
  menu, Compress All — never reads flash, so it stays fast as the directory grows (LittleFS
  lookups get slower with more files).

---

## 5. Battery Monitoring

### 5.1. Hardware: MAX17043 Fuel Gauge

Battery monitoring uses an external **MAX17043** I2C fuel gauge IC instead of the ESP32's
internal ADC. This eliminates ADC channel conflicts with the OPT101 and provides significantly
more accurate state-of-charge estimation.

| Parameter | Value |
| :--- | :--- |
| IC | MAX17043 (Maxim/Analog Devices) |
| Interface | I2C, address **0x36** (shared bus with OLED at 0x3C) |
| Inputs | CELL pin → VBAT (battery positive), GND → ground |
| Power | VCC → 3V3 (ESP32 regulated rail) |
| Voltage accuracy | ±7.5 mV |
| SoC algorithm | ModelGauge™ — no coulomb counter, no sense resistor, no calibration needed |
| SoC resolution | 1/256% |
| Quiescent current | ~50 µA (from CELL/VBAT) |
| I2C speed | Standard (100 kHz) or Fast (400 kHz) |

### 5.2. Wiring (5 Pins)

| MAX17043 Pin | Connects To | Purpose |
| :--- | :--- | :--- |
| SDA | I2C SDA (shared with OLED) | I2C data |
| SCL | I2C SCL (shared with OLED) | I2C clock |
| GND | GND | Ground reference |
| CELL | VBAT (battery rail) | Direct battery voltage measurement input |
| VCC | 3V3 (ESP32 rail) | IC logic supply |

> **Note:** The MAX17043's CELL pin connects **directly** to the battery positive terminal (VBAT
> rail) — no voltage divider is needed. The IC handles the full LiPo voltage range internally.

### 5.3. Sampling Strategy

| Parameter | Value |
| :--- | :--- |
| Read frequency | Every **30–60 seconds** when awake |
| Technique | Single I2C register read — the MAX17043 continuously tracks voltage and SoC internally |
| During recording | **Safe to read** — uses I2C, not ADC; no conflict with OPT101 DMA continuous mode. However, reads are deferred during recording to minimize I2C bus activity (the OLED is also frozen). A read is taken immediately before and after each recording. |
| Data returned | Voltage (mV) from VCELL register; SoC (%) from SOC register |
| Deep sleep | Put into its < 1 µA sleep mode (CONFIG.SLEEP) right before the ESP32 sleeps; cleared on every boot. ModelGauge™ is voltage-based, so SoC re-converges after waking; the first read is delayed ~1 s. |

### 5.4. Advantages Over ADC Voltage Divider

- **No ADC channel consumed** — all ADC1 channels remain available for the OPT101.
- **No voltage divider circuit** — fewer external components, no resistor tolerance errors.
- **Accurate SoC** — the ModelGauge™ algorithm accounts for load, temperature, and battery aging;
  a simple voltage-to-percentage lookup table is much less accurate under varying loads.
- **Can read during recording** — since it uses I2C (not ADC), there's no conflict with the
  OPT101's ADC DMA continuous mode.
- **Low power** — ~50 µA quiescent from the battery, negligible impact on battery life.

---

## 6. Display & UI (OLED + Rotary Encoder)

### 6.1. Screen Layout

The 128×64 OLED has a fixed hardware color split:

- **Yellow zone** (top 16 pixels): Persistent status bar — always visible.
- **Blue zone** (bottom 48 pixels): Context-dependent content area.

#### Status Bar (Yellow Zone) — Always Shown

```
┌────────────────────────────────┐
│ 🔋 78% 3.9V  │ 15.2MB │ 📶   │  (yellow, 16px tall)
└────────────────────────────────┘
```

Contents:
- **Battery**: Percentage + voltage (e.g., `78% 3.9V`)
- **Free storage**: In MB (e.g., `15.2MB`)
- **Connection indicator**: `AP` while the access point is on; shown inverted (filled box) when
  a tablet is connected

#### Content Area (Blue Zone)

Shows the current screen context: status screen, menu, or recording feedback.

### 6.2. Behavior During Recording

- The **OLED is frozen** to a static "Recording..." screen. No redraws are performed to avoid I2C
  bus contention and CPU time competition with the DMA buffer drain task.
- **Battery reads** (MAX17043 via I2C) are deferred during recording to minimize I2C bus activity.
  A reading is taken immediately before and after each recording session.
- The **RGB LED** provides visual feedback instead:
  - 🔴 **Red pulsing** = recording in progress
- The menu is locked during recording; a **short press stops the recording** early.
- After capture ends, the OLED shows "Saving…" while the recording is written to flash, then
  refreshes with the result summary.

### 6.3. RGB LED Status Scheme

| Color | Pattern | Meaning |
| :--- | :--- | :--- |
| 🟢 Green | Steady | Idle, ready |
| 🔴 Red | Pulsing | Recording in progress (also while saving) |
| 🔵 Blue | Steady | Tablet connected (overlays with green → cyan) |
| 🟡 Amber | Steady | Low battery warning (≤ 15%) — takes precedence over cyan/green |
| Off | — | Deep sleep |

### 6.4. Menu Structure

Navigation model:
- **Rotate** = move up/down in current menu level
- **Short press** = select / enter submenu / confirm
- **Long press (~1 s)** = go back one level (or return to status screen from top menu)
- **Long press (~2 s) from deep sleep** = wake the device

From the **Status Screen** (shown on wake), a **short press** opens the **Main Menu**:

```
[Status Screen] (default — shown on wake)
├── Yellow bar: 🔋 78% 3.9V │ 15.2MB │ 📶
├── Blue area:  "Ready" / last recording summary
│
[Main Menu] (short press from status screen)
│
├── 🔴 Record
│   ├── Quick Start (uses last-used duration + sample rate)
│   ├── Set Duration → [5s │ 10s │ 30s │ 60s │ 120s │ 300s]  (300s hidden at 20 kHz)
│   └── Set Sample Rate → [1 kHz │ 2 kHz │ 5 kHz │ 10 kHz │ 20 kHz]
│
├── 📂 Recordings
│   ├── [#42 10-01 14:23 30s]  (newest first; times in UTC)
│   │   ├── View Info (duration, size, sample rate, timestamp)
│   │   └── Delete → [Confirm? Yes / No]
│   ├── [Recording 2 — ...]
│   │   └── ...
│   └── (scrollable list of all recordings)
│
├── 📡 WiFi
│   ├── Enable / Disable AP
│   └── Show SSID & IP
│
├── 💾 Storage
│   ├── Used: 9.8 MB / 25 MB
│   ├── Recordings: 12
│   ├── Compress All Saved → [Confirm? Yes / No]
│   └── Clear All Recordings → [Confirm? Yes / No]
│
├── ⚙️ Settings
│   ├── Screen Brightness → [Low │ Medium │ High]
│   ├── Screen Timeout → [30s │ 1 min │ 5 min │ Never]
│   ├── Default Sample Rate → [1 kHz │ 2 kHz │ 5 kHz │ 10 kHz │ 20 kHz]
│   ├── Default Duration → [5s │ 10s │ 30s │ 60s]
│   └── Sleep After → [5 min │ 15 min │ 30 min │ Never]
│
└── 💤 Deep Sleep → [Confirm? Yes / No]
```

### 6.5. Settings Persistence

- User settings (brightness, screen timeout, default sample rate, default duration, sleep
  timeout) are stored in
  **ESP-IDF NVS (Non-Volatile Storage)** — the platform's built-in key-value store.
- NVS provides power-loss-safe transactional writes and built-in wear leveling, with no need for
  a custom file format or serialization code.
- Settings survive deep sleep and power cycles.
- NVS uses its own dedicated flash partition, separate from the LittleFS data partition.

---

## 7. Power Management

### 7.1. Deep Sleep

- **Entry**: Via the device menu ("Deep Sleep" → confirm) or `POST /api/sleep` — only while
  idle. The OLED and LED are turned off and WiFi is stopped first; the firmware waits for the
  button to be released so it doesn't wake straight away.
- **Idle timeout**: the device also sleeps by itself after **Sleep After** (Settings; 5, 15 or
  30 min, or Never; default **15 min**) without activity. Awake, the WiFi access point alone
  draws ~80–120 mA — a full 1000 mAh cell would be flat in ~8–12 h if the device were left on.
  - **Activity**: encoder turns and presses; any API request **except** `GET /api/status`; and
    any recording, saving or compressing (the countdown only runs while idle). Status polling
    alone deliberately doesn't count, so a tablet left open on the app can't keep the device
    awake indefinitely — the app then shows the device as unreachable, with wake instructions.
  - **Warning**: for the last 10 s the OLED turns on and shows "Going to sleep in N s — Press to
    stay awake", and the status LED blinks. Any encoder input cancels it; so does any app
    request.
- **Wake source**: Long press (~2 seconds) on the rotary encoder button. The button GPIO (an LP
  GPIO, pull-up held during sleep) is configured as a deep-sleep wake source; any press wakes the
  chip. On boot the firmware times the press: if the button is released before ~2 s, the device
  goes straight back to deep sleep. This avoids a separate LP-core (ULP) program at the cost of a
  brief wake on accidental presses.
- **Power draw**: the ESP32-C5 itself is ≈ 10–30 µA with GPIO wake (the button's internal pull-up
  keeps the RTC peripheral domain on), but everything on the 3V3 and 5V rails stays powered:
  - OPT101: **≈ 120 µA** quiescent (datasheet, dark) — stays on the 3V3 rail by choice: on a
    1000 mAh cell that's still months of standby, and LiPo self-discharge (≈ 2–5 %/month) is in
    the same range. Powering it from a GPIO remains an option if measurements call for it.
  - MAX17043: < 1 µA in sleep mode (§5.3)
  - OLED in display-off mode: a few µA, plus its own LDO's quiescent current (it's powered from
    the 5V pin, which carries the battery voltage when unplugged)
  - Board-level parts (regulator quiescent current, any power LED) — **measure on hardware**;
    these may well dominate

  So expect ≈ 150 µA plus whatever the board itself draws (≈ 9 months of standby on 1000 mAh
  before the board's share).

### 7.2. Screen Power Saving

- After the configured timeout (default: 1 minute), the OLED is turned off to save power. The
  timeout only applies while idle.
- Any rotary encoder interaction wakes the screen (and is otherwise ignored). A device state
  change — e.g. a recording started from the app, or one finishing — also wakes it.

### 7.3. Timekeeping Across Deep Sleep

- The ESP32-C5's **RTC timer persists through deep sleep** using the internal RC oscillator.
- Drift is expected (several seconds per hour) but acceptable — the tablet re-syncs the clock on
  every connection.
- The system clock is set with `settimeofday()` and read with `gettimeofday()`. ESP-IDF keeps
  the system clock running across deep sleep (backed by the RTC timer), so no manual offset
  bookkeeping is needed. Note that `esp_timer_get_time()` restarts at 0 on every boot, including
  deep-sleep wake, so it must **not** be used for wall-clock time. Time is lost on full power loss
  until the next sync.
- Within a single recording, sample timing is hardware-precise (DMA-driven) regardless of RTC
  drift.

---

## 8. Wireless Communication

### 8.1. Transport: WiFi SoftAP

The ESP32-C5 operates as a **WiFi Access Point** (SoftAP). The tablet connects directly to it —
no router or internet required.

| Parameter | Value |
| :--- | :--- |
| Mode | SoftAP (ESP32 is the access point) |
| Band | 2.4 GHz or 5 GHz (WiFi 6 capable) |
| SSID | `IRSensor-XXXX` where XXXX = last 4 hex digits of the MAC (fixed) |
| Password | Generated on first boot, stored in NVS, shown on the OLED (first boot, and WiFi → Show SSID & IP); not changeable remotely |
| IP | `192.168.4.1` (default ESP-IDF SoftAP address) |
| Max clients | 1 (only the tablet) |

### 8.2. Protocol: HTTP REST API (Polling)

The ESP32 runs a lightweight HTTP server (`esp_http_server`). The app polls `GET /api/status`
at **~1 Hz** to track device state, recording progress, and battery level. No WebSocket is
needed — the simplicity of stateless REST is preferred on a single-core MCU with limited RAM.

#### REST Endpoints

| Method | Path | Description |
| :--- | :--- | :--- |
| `GET` | `/api/status` | Device status: battery (voltage + SoC%), free space, recording count, WiFi info, current state (`idle` / `recording` / `saving` / `compressing`), recording progress while recording, compression progress while compressing (shape under Response Formats) |
| `POST` | `/api/record` | Start recording. Body: `{"duration_s": 30, "sample_rate_hz": 10000}` (nominal rate; both required). Returns error if insufficient space or the duration exceeds the cap for that rate (§4.4). |
| `POST` | `/api/record/stop` | Stop the current recording early. The partial recording is finalized and saved. |
| `GET` | `/api/recordings` | List all recordings with metadata (entry fields under Response Formats). Served from RAM (§4.6), so it's quick however many recordings there are |
| `GET` | `/api/recordings/{id}` | Download a specific recording (binary, IREC format) |
| `DELETE` | `/api/recordings/{id}` | Delete a specific recording |
| `DELETE` | `/api/recordings` | Delete all recordings. Body must be exactly `{"confirm": "DELETE_ALL"}`; anything else is rejected with `400`. Guards against accidental/buggy requests — the user-facing confirmation is a dialog in the app. |
| `POST` | `/api/time` | Sync time from tablet. Body: `{"utc_us": <microseconds since epoch>}` |
| `POST` | `/api/compress` | Trigger compression of all uncompressed recordings |
| `POST` | `/api/settings` | Update device settings. Body: JSON of setting key-values |
| `POST` | `/api/sleep` | Enter deep sleep (§7.1). Responds first, then sleeps ~0.5 s later; WiFi goes down with it |
| `GET` | `/api/settings` | Read current device settings |
| `GET` | `/api/calibration` | Raw → mV lookup table for the OPT101 channel: `{"attenuation_db": 12, "mv": [<4096 integers>]}` (§3.4) |

#### Requests During Recording

While the device is not `idle` (recording, saving or compressing), only these requests are
served:

- `GET /api/status`
- `POST /api/record/stop` (recording only)
- `GET /api/settings` (served from RAM)

Every other request returns **`409 Conflict`** with `{"error": "busy", "state": "<state>"}`. This
includes settings changes, since NVS writes go to flash. The app retries once the state returns
to `idle`.

#### Response Formats

All JSON bodies are objects. Shapes for the endpoints whose output isn't obvious from the table:

| Endpoint | Success response |
| :--- | :--- |
| `GET /api/status` | `200` `{"state": "idle"\|"recording"\|"saving"\|"compressing", "battery": {"mv", "pct"}, "free_bytes", "recording_count", "wifi": {"ssid", "client_connected"}}`, plus `"recording": {"id", "elapsed_s", "total_s"}` while recording and `"compress": {"done", "total"}` while compressing |
| `POST /api/record` | `200` `{"id": 42, "duration_s": 30, "sample_rate_hz": 10000}` |
| `POST /api/record/stop` | `200` `{"id": 42}` |
| `GET /api/recordings` | `200` `{"recordings": [<entry>, …]}` in ascending ID order; each entry is `{"id", "start_utc_us", "duration_ms", "sample_rate_mhz", "sample_count", "size_bytes", "compressed", "overrun", "attenuation_db"}` (`attenuation_db` is a number, e.g. `12` or `2.5`) |
| `GET /api/recordings/{id}` | `200` `application/octet-stream` (chunked), `Content-Disposition: attachment; filename="00042.irec"` |
| `DELETE /api/recordings/{id}` | `200` `{"deleted": 1}` |
| `DELETE /api/recordings` | `200` `{"deleted": <count>}` |
| `POST /api/time` | `200` `{"utc_us": <device time after the update>}` |
| `GET` / `POST /api/settings` | `200` the full settings object: `{"brightness": "low"\|"medium"\|"high", "screen_timeout_s": 30\|60\|300\|0, "default_sample_rate_hz": …, "default_duration_s": 5\|10\|30\|60, "sleep_timeout_s": 300\|900\|1800\|0}` (`0` = never). `POST` accepts any subset of these keys and applies all or nothing. |
| `POST /api/compress` | `200` `{"queued": <count>}` — progress via `/api/status`; `0` means nothing to compress (state stays `idle`) |
| `POST /api/sleep` | `200` `{"sleeping": true}` |

Errors use `{"error": "<code>"}`, plus `"field"` for settings errors:

| Status | Codes |
| :--- | :--- |
| `400` | `invalid_body`, `invalid_json`, `invalid_rate`, `invalid_duration`, `invalid_time`, `invalid_setting` (+ `field`), `unknown_setting` (+ `field`), `confirmation_required` |
| `404` | `not_found` |
| `409` | `busy` (+ `state`) |
| `500` | `storage_error` |
| `503` | `calibration_unavailable` (chip has no eFuse calibration) |
| `507` | `insufficient_space` |

#### Long-Running Operations

- Compression runs in a lowest-priority background task that works in chunks and yields between
  them, so the task watchdog is never starved. Progress is reported via `GET /api/status`.
- Delete-all runs within the request: deleting a file only updates LittleFS metadata, so even
  ~250 files take well under a few seconds.
- Recording downloads are streamed in 4 KB chunks (`httpd_resp_send_chunk`).
- The server handles **one request at a time**. A status poll waits until a download finishes
  (a 6 MB file takes several seconds), so the app must not treat a slow poll during a download
  as a disconnect. The app sends its own requests one at a time as well.

#### Polling Pattern (App Side)

```
While connected:
  Every 1 second → GET /api/status
    - Update battery display, free space, connection indicator
    - If state == "recording": show progress bar (elapsed_s / total_s)
    - If state == "idle" and the list may have changed (just connected, or state or
      recording_count differs from the previous poll):
      → GET /api/recordings
      → GET /api/recordings/{id} for each recording not yet on the tablet, oldest first
```

Downloads are driven by the recording list, not by watching one recording finish:

- There is always a `saving` state between `recording` and `idle`, and a 1 s poll can miss a
  short recording entirely.
- Recordings can also be started from the device menu, or made while the tablet is away.
- A recording stopped before any sample is captured is not saved, so the `recording.id` seen in
  status may never appear in the list.

Retries, 409 handling and what is skipped are in §10.4.

### 8.3. Time Synchronization Protocol

1. Tablet connects to ESP32 SoftAP.
2. Once `GET /api/status` first reports `idle` after connecting (in other states the request
   gets `409`), the app sends `POST /api/time` with the current UTC timestamp in microseconds.
3. ESP32 sets its system clock from the received time with `settimeofday()`.
4. Recording start timestamps are read with `gettimeofday()`.
5. The system clock keeps running through deep sleep (RTC timer), so no extra persistence is
   needed.
6. On reconnection, the app re-syncs time to correct any RTC drift.

The device doesn't report whether its clock has been set. A recording made after a full power
loss and before the next sync has a start time near 1970. The app treats any start time before
2024-01-01 (the floor `POST /api/time` accepts) as "clock not set".

---

## 9. Firmware Architecture

### 9.1. Development Environment

| Parameter | Value |
| :--- | :--- |
| Framework | **PlatformIO** with **ESP-IDF** (Arduino-ESP32 as compatibility layer where convenient) |
| Language | **C++** |
| Build system | PlatformIO (wraps CMake / ESP-IDF build) |
| Target | `esp32-c5` |
| Source directory | `./firmware/` |

### 9.2. Key Dependencies (Libraries)

| Library | Purpose |
| :--- | :--- |
| ESP-IDF ADC Continuous Driver | DMA-based ADC sampling |
| ESP-IDF WiFi (SoftAP) | WiFi access point |
| ESP-IDF HTTP Server | REST API (`esp_http_server`) |
| ESP-IDF NVS Flash | Key-value settings storage |
| ESP-IDF LittleFS | Flash filesystem for recordings |
| ESP-IDF `esp_lcd` (SSD1306 panel) | OLED display driver (I2C), with a small framebuffer + font |
| MAX17043 driver | I2C fuel gauge — battery voltage and SoC% readings |
| FastLED or ESP-IDF RMT | WS2812B RGB LED control |
| ESP-IDF PCNT + own `ButtonDetector` | Encoder rotation (hardware quadrature decoding); debounced short/long press detection (small, host-tested — no extra component) |
| ESP-IDF ADC Calibration | Curve fitting calibration |
| miniz (in ESP32-C5 ROM) | Optional zlib recording compression — no extra library needed |

### 9.3. FreeRTOS Task Architecture

> **Important**: The ESP32-C5 is a **single-core** RISC-V processor. All tasks share one core.
> Task priorities and yielding discipline are critical to prevent starvation. Note that the
> ESP-IDF WiFi driver task (priority 23) outranks every application task (see §3.3).

| Task | Priority | Responsibility |
| :--- | :--- | :--- |
| **Recorder** (capture) | Highest (18) | Pull samples from DMA buffers → PSRAM recording buffer. Runs only during active recording. |
| **Recorder** (save) | Below HTTP (4) | After capture stops: compute CRC, write header + samples to LittleFS. Same task, priority lowered so `GET /api/status` stays responsive while saving. |
| **Compressor** | Lowest (1) | Compress All, in yielding chunks. |
| **WiFi / HTTP** | Medium | Handle REST API requests. Yield-friendly. |
| **UI Task** | Low | OLED rendering, rotary encoder polling, LED updates. Suspended during recording (except LED). |
| **Battery Monitor** | Lowest | Periodic MAX17043 I2C reads (voltage + SoC%). Deferred during recording to minimize I2C bus activity. |
| **Idle / Watchdog** | System | FreeRTOS idle task and task watchdog. |

### 9.4. Recording Flow

```
1. Record command received (from app or device menu)
2. Validate: duration within cap for the rate? sufficient free space? → if no, refuse with warning
3. Read battery (MAX17043), then defer battery reads
4. Freeze OLED to "Recording..." screen; lock menu (short press = stop)
5. Set RGB LED to pulsing red; set state = recording (API now returns 409 for most requests)
6. Configure ADC continuous mode: OPT101 channel only, requested sample rate
7. Use the PSRAM recording buffer reserved at boot
8. Record start timestamp (gettimeofday(), UTC microseconds)
9. Start ADC continuous (DMA begins)
10. DMA Drain task: parse DMA results → append uint16 samples to PSRAM buffer
11. On duration elapsed or stop command (button / POST /api/record/stop):
    a. Stop ADC continuous
    b. Set state = saving; OLED shows "Saving…"
    c. Compute CRC-32 of the sample data
    d. Write IREC header + samples from PSRAM to a temp file on LittleFS
    e. Rename temp file to /data/recordings/<id>.irec
12. Read battery, resume battery monitor and UI tasks; set state = idle
13. Update OLED with recording summary
14. Set RGB LED back to green (or cyan if tablet connected)
15. App discovers completion on next GET /api/status poll (≤1 s)
```

---

## 10. Tablet App Architecture

### 10.1. Platform & Technology

| Parameter | Value |
| :--- | :--- |
| Target device | Microsoft Surface Go 2 running Fedora Workstation (Linux). Kept OS-agnostic: also runs on macOS (development), possibly Android later |
| App framework | **Tauri** (v2+) |
| Frontend | **Svelte** (SvelteKit, as a static single-page app) with TypeScript |
| Backend | Rust (Tauri core) |
| Package manager | **pnpm** (version pinned in `package.json`, lockfile committed) |
| Source directory | `./app/` |

### 10.2. Key Frontend Libraries

| Library | Purpose |
| :--- | :--- |
| **uPlot** | Time-series graphing. Chosen over Chart.js for speed: a recording can hold 3 M samples. The app gives it only the visible window, reduced to each pixel column's min and max at their real sample times, and handles wheel, drag and pinch itself |
| **Svelte** | Reactive UI framework |
| **TypeScript** | Type-safe frontend code |
| **Tauri HTTP plugin** (`@tauri-apps/plugin-http`) | Communication with the ESP32. Requests are made from Rust, so the device needs no CORS headers (the firmware sends none, so the webview's own `fetch` can't reach it) |
| **Tauri fs plugin** | Local recording storage (§10.4) |
| **fflate** | zlib decompression of compressed recordings; behaves the same in every webview and in unit tests |

### 10.3. App Features

#### Core Features

1. **Device Connection**
   - The tablet joins the device's WiFi through the **OS**; the app doesn't manage WiFi itself,
     which keeps it OS-agnostic. When the device can't be reached, the app says which network to
     join (`IRSensor-XXXX`, the last SSID seen) and how to wake the device.
   - Auto-detect device at `192.168.4.1` (configurable, e.g. for the mock device)
   - Connection status indicator; "unreachable" after two failed status polls (e.g. after
     `POST /api/sleep`)
   - Automatic time sync once the device is idle after connecting (§8.3)

2. **Recording Control**
   - Set recording duration: single-select chips for 5 s, 10 s, 30 s, 1 min, 2 min, 5 min
     (5 min hidden at 20 kHz), plus "Custom" for any whole number of seconds up to the cap —
     300 s, or 120 s at 20 kHz (§4.4)
   - Set sample rate: single-select chips (1 kHz, 2 kHz, 5 kHz, 10 kHz, 20 kHz)
   - Start / stop recording remotely
   - Real-time progress indicator during recording
   - Automatic download of recordings (§10.4); a recording started from the app opens in the
     graph once it has downloaded

3. **Data Visualization**
   - Time-series graph of recording data (ADC value or calibrated voltage vs. time)
   - **Interactive zoom & pan** (pinch-to-zoom on touch screen, scroll wheel, drag to pan)
   - **Measurement cursors**: Tap/click two points to see Δt and Δvalue
   - **Multi-recording overlay**: Select multiple recordings to display on the same graph
     - Color-coded traces with legend
     - Toggle individual traces on/off
     - Time-alignment options (align by start, or manual offset)

4. **Recording Management**
   - Browse all recordings on device (synced list)
   - Download individual or all recordings to tablet local storage
   - Delete a recording from the device, the tablet, or both: one Delete button opens a dialog
     with a checkbox for each place the recording exists, and warns before deleting the last
     copy. A recording deleted from the tablet only is not downloaded again automatically (it
     can still be downloaded by hand).
   - Delete all recordings on the device (`DELETE /api/recordings`), after an in-app
     confirmation dialog that says how many aren't on the tablet yet. Tablet copies are kept.
   - View recording metadata (timestamp, duration, sample rate, size)
   - Name / tag / annotate recordings with notes

5. **Export**
   - Export recording data as **CSV** (timestamp_ms, raw_adc, voltage_mv) — `voltage_mv` uses the
     device's calibration table stored in the recording's sidecar (§3.4)
   - Export recording data as **JSON**
   - Export graph as image (PNG/SVG)

6. **Device Settings** (remote configuration)
   - Screen brightness
   - Screen timeout
   - Default sample rate
   - Default recording duration
   - (WiFi SSID and password are not remotely configurable — changing them would drop the
     tablet's connection; see §8.1)

7. **Statistics**
   - Per-recording: min, max, mean, standard deviation of ADC values
   - Display in a summary panel alongside the graph

#### UX Details

- **Dark mode** (default) — appropriate for controlled-lighting IR measurement environments
- **Touch-optimized** — large tap targets, swipe gestures for the Surface Go 2 touch screen
- **Responsive layout** — adapts to the Surface Go 2's 10.5" 1920×1280 display
- **Free space indicator** — shows device free storage in the status bar
- **Auto-reconnect** — the OS rejoins the remembered WiFi; the app keeps polling and, as soon
  as the device answers, re-syncs the time and resumes the list and downloads
- **Local time** — device times are UTC; the app shows them in the tablet's local time (UTC on
  hover), and "clock not set" for start times before 2024 (§8.3)
- **Handles both compressed and uncompressed recordings** transparently (checks IREC flags byte,
  decompresses zlib if needed) and verifies the CRC-32 over the uncompressed samples
- **Retries on `409 Busy`** — the device rejects most requests while recording/saving/compressing
- **Warns on overrun recordings** (IREC flags bit 1 / `"overrun": true`) — samples were lost, so
  timing within that recording can't be trusted

### 10.4. Local Storage

The app stores downloaded recordings locally on the tablet filesystem (via Tauri's fs API):

```
~/IRSensor/recordings/<SSID>_<id5>.irec   the file exactly as downloaded (compressed or not)
~/IRSensor/recordings/<SSID>_<id5>.json   its sidecar
~/IRSensor/devices/<SSID>.json            last calibration table seen, per device
```

- **Naming.** Recording IDs are unique per device only, and restart if the device's NVS is
  erased, so files are named by SSID and ID, e.g. `IRSensor-AB12_00042.irec`. A *different*
  recording that wants an existing name gets `-2`, `-3`, … rather than overwriting it.
- **Matching.** A device recording and a tablet copy are the same recording when SSID, ID and
  `start_utc_us` all match.
- **Sidecar** (`schema: 1`). The `.irec` is written first and the sidecar last, so a sidecar
  only exists for a complete download:

  | Field | Contents |
  | :--- | :--- |
  | `schema` | `1` |
  | `file` | the `.irec` file next to it |
  | `device_ssid`, `id` | where it came from |
  | `downloaded_utc_us` | when |
  | `start_utc_us`, `duration_ms`, `sample_rate_mhz`, `sample_count`, `size_bytes`, `compressed`, `overrun`, `attenuation_db` | the device's list entry (`size_bytes` is the downloaded file's size) |
  | `crc32`, `crc_ok` | the header's CRC and whether the samples matched it |
  | `calibration` | the device's `GET /api/calibration` answer at download time, or `null` |
  | `name`, `notes`, `tags` | reserved for annotation (`null`, `""`, `[]`) |

- **Index.** The sidecars *are* the local index: the app scans them at startup, and there is no
  separate database.
- **Auto-download.** Whenever the device is idle and its list may have changed (§8.2), the app
  downloads every recording not on the tablet, oldest first:
  - a `409` just means "not now": the work resumes at the next idle status poll;
  - network errors are retried 3 times with back-off, then the recording waits for a manual
    retry; counters reset when the device reconnects;
  - a `404` (deleted meanwhile, or a 0-sample recording) is dropped quietly;
  - a CRC mismatch is retried once; if it fails again the file is kept, marked
    `crc_ok: false`, and never deleted from the device automatically;
  - recordings deleted from the tablet only are skipped (§10.3.4).
- **Calibration fallback.** The table is fetched once per connection (it never changes for a
  chip) and cached per device. If `GET /api/calibration` returns `503 calibration_unavailable`,
  the app uses a table cached on an earlier connection, if any; otherwise the sidecar gets
  `calibration: null` and the graph shows raw ADC counts with a note. No approximate linear
  conversion is used.
- **Overrides** for development: `IRSENSOR_DATA_DIR` replaces `~/IRSensor`, and
  `IRSENSOR_DEVICE_URL` sets the device URL for one run.

---

## 11. Directory Structure

```
IR_sensor/
├── spec.md                              ← This document
├── wiring_diagram.md                    ← Wiring connections (TBD)
├── docs/
│   └── external/
│       ├── ESP32-C5-WIFI6-KIT-N32R8.md  ← Board pinout reference
│       └── opt101.pdf                   ← OPT101 datasheet
├── firmware/                            ← ESP32 firmware (PlatformIO + ESP-IDF, C++)
│   ├── platformio.ini
│   ├── src/
│   ├── include/
│   ├── lib/
│   └── ...
└── app/                                 ← Tablet app (Tauri + Svelte + TypeScript)
    ├── src-tauri/                       ← Rust backend
    ├── src/                             ← Svelte frontend
    ├── package.json
    └── ...
```

---

## 12. Key Design Decisions Summary

| Decision | Choice | Rationale |
| :--- | :--- | :--- |
| Sampling approach | ADC Continuous Mode + DMA | Hardware-driven sampling, no CPU jitter, reliable at 10 kHz+ |
| Firmware language | C++ (ESP-IDF via PlatformIO) | DMA/ADC APIs not available in MicroPython; deterministic timing required |
| Communication | WiFi SoftAP + HTTP REST (polling) | High throughput for 1+ MB transfers; no router needed; stateless REST is simpler than WebSocket on single-core MCU |
| App framework | Tauri + Svelte + TypeScript | Lightweight (~5-10 MB), web-based UI (vibe-codable), Rust backend for file I/O |
| App ↔ device transport | Tauri HTTP plugin | Requests from Rust: no CORS needed on the device, binary downloads work |
| Joining the device's WiFi | Through the OS; the app only detects the device | OS-agnostic (Linux, macOS, later Android); NetworkManager already remembers and rejoins |
| App graphing | uPlot with min/max decimation and own gestures | Millions of samples on a low-power tablet; pulse edges stay at their real times |
| Local recording index | The `.json` sidecars themselves | Nothing to get out of sync; a folder scan is cheap at this scale |
| Auto-download | List-driven, whenever the device is idle | Also catches device-menu recordings and ones made while the tablet was away |
| Storage format | Custom binary (IREC) on LittleFS | Compact (2 bytes/sample), CRC32 integrity, optional zlib compression |
| Settings persistence | ESP-IDF NVS (key-value) | Platform built-in; power-loss safe; no custom serialization code |
| Display during recording | Frozen OLED + RGB LED | Avoids I2C contention and CPU load on single-core MCU |
| Battery monitoring | MAX17043 I2C fuel gauge | Accurate ModelGauge™ SoC; no ADC channel conflict with OPT101; can read during recording |
| Time sync | Tablet sends UTC on connect → `settimeofday()`; system clock persists in deep sleep | No external RTC hardware needed; drift corrected on each reconnection |
| Deep sleep wake | GPIO wake on encoder button, firmware checks for ≥2 s hold after boot | Ultra-low standby power without a separate LP-core program |
| Recording buffering | Whole recording in PSRAM, saved to flash after capture | No flash writes during capture → no flash/PSRAM cache stalls; simpler than a streaming ring buffer |
| Max recording duration | 300 s (1–10 kHz), 120 s (20 kHz) | Must fit in ~6 MB of reserved PSRAM |
| ADC calibration | Raw counts in files; device serves raw → mV table via `/api/calibration` | Full resolution; per-chip eFuse calibration only exists on the device |
| Sample rate in header | Actual (measured) rate, in millihertz | DAQ practice; avoids mislabelling the timebase |
| OPT101 power | 3.3V from ESP32 rail | Stable supply on USB and battery; the 5V pin swings 3.0–5 V and at 5 V the output could exceed the ADC range. Output range ~0–2.15 V usable |
| OLED power | 5V pin (USB VBUS, or ~VBAT on battery) via the module's own 3.3V LDO | Board has one 3V3 pin; this module's I2C pull-ups go to its LDO output, so the bus stays at 3.3 V (verified with a multimeter) |
| ADC attenuation | 12 dB (full 0–3.3 V), fixed, recorded per file | Never clips the OPT101's ~2.15 V peak on any chip; the resolution given up doesn't affect timing |
