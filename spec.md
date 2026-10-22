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

1. Captures analog readings from an **OPT101 photodiode** (with IR bandpass filter) at up to
   **10 kHz** (0.1 ms per sample) to achieve **≤1 ms measurement precision**.
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
| Attenuation | 6 dB recommended (range ~0–2.2 V, matching OPT101 output ceiling) |
| Sampling mode | **Continuous mode with DMA** (GDMA controller) |
| Configurable rates | 1 kHz, 5 kHz, **10 kHz** (default), 20 kHz |
| Channel during recording | OPT101 channel only |

### 3.3. DMA Strategy

The ESP32-C5's GDMA controller offloads ADC sampling entirely to hardware:

- ADC conversions are triggered automatically at the configured sample rate.
- Results are deposited into linked-list descriptor buffers in internal SRAM via DMA — **no CPU
  intervention required per sample**.
- The firmware drains DMA buffers in a high-priority FreeRTOS task, copying data to a PSRAM
  staging buffer before flushing to flash.
- At 10 kHz × 2 bytes/sample = **20 KB/s**, this is trivially within the GDMA's capability
  (hardware supports up to ~250–350 kS/s).

### 3.4. Calibration

- Use ESP-IDF's **ADC Curve Fitting calibration scheme** (`ADC_CALI_SCHEME_VER_CURVE_FITTING`) to
  convert raw ADC counts to millivolts.
- Apply calibration on first boot or when requested via the app.

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
  0x05    4B    Sample rate in Hz (uint32_le)
  0x09    4B    Sample count (uint32_le)
  0x0D    8B    Start timestamp — UTC microseconds since epoch (uint64_le)
  0x15    4B    CRC32 of data payload (uint32_le)
  0x19    1B    Flags: bit 0 = compressed (zlib)
  0x1A    6B    Reserved (zero-filled)

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

### 4.4. Recording Constraints

- **Maximum duration**: 300 seconds (5 minutes) hard cap.
- **Pre-recording check**: Before starting, the firmware calculates the required storage
  (`sample_rate × duration × 2 bytes + 32 byte header`) and compares against free space. If
  insufficient, recording is **refused with a warning** (displayed on OLED and returned to the
  app).
- **Compression**: Optional. A manual "Compress All Saved" action is available from the device
  menu. Compression uses zlib on stored files. The app handles both compressed and uncompressed
  files transparently (checks the flags byte in the header).

### 4.5. Data Integrity

- Each recording includes a **CRC32** checksum of the data payload, computed at write time and
  verified on read/transfer.

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
- **Connection indicator**: Icon or symbol when a tablet is connected via WiFi

#### Content Area (Blue Zone)

Shows the current screen context: status screen, menu, or recording feedback.

### 6.2. Behavior During Recording

- The **OLED is frozen** to a static "Recording..." screen. No redraws are performed to avoid I2C
  bus contention and CPU time competition with the DMA buffer drain task.
- **Battery reads** (MAX17043 via I2C) are deferred during recording to minimize I2C bus activity.
  A reading is taken immediately before and after each recording session.
- The **RGB LED** provides visual feedback instead:
  - 🔴 **Red pulsing** = recording in progress
- After recording completes, the OLED refreshes with the result summary.

### 6.3. RGB LED Status Scheme

| Color | Pattern | Meaning |
| :--- | :--- | :--- |
| 🟢 Green | Steady | Idle, ready |
| 🔴 Red | Pulsing | Recording in progress |
| 🔵 Blue | Steady | Tablet connected (overlays with green → cyan/alternating) |
| 🟡 Amber | Steady | Low battery warning |
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
│   ├── Set Duration → [5s │ 10s │ 30s │ 60s │ 120s │ 300s]
│   └── Set Sample Rate → [1 kHz │ 5 kHz │ 10 kHz │ 20 kHz]
│
├── 📂 Recordings
│   ├── [Recording 1 — 2026-10-01 14:23, 30s @ 10kHz]
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
│   ├── Default Sample Rate → [1 kHz │ 5 kHz │ 10 kHz │ 20 kHz]
│   └── Default Duration → [5s │ 10s │ 30s │ 60s]
│
└── 💤 Deep Sleep → [Confirm? Yes / No]
```

### 6.5. Settings Persistence

- User settings (brightness, timeout, default sample rate, default duration) are stored in
  **ESP-IDF NVS (Non-Volatile Storage)** — the platform's built-in key-value store.
- NVS provides power-loss-safe transactional writes and built-in wear leveling, with no need for
  a custom file format or serialization code.
- Settings survive deep sleep and power cycles.
- NVS uses its own dedicated flash partition, separate from the LittleFS data partition.

---

## 7. Power Management

### 7.1. Deep Sleep

- **Entry**: Via the device menu ("Deep Sleep" → confirm) or via app command.
- **Wake source**: Long press (~2 seconds) on the rotary encoder button. The LP (low-power)
  RISC-V core monitors the GPIO for the encoder press while the main HP core is fully powered
  down.
- **Power draw**: Deep sleep with LP core monitoring ≈ 10–30 µA (depending on peripherals left
  active).

### 7.2. Screen Power Saving

- After the configured timeout (default: 1 minute), the OLED is turned off to save power.
- Any rotary encoder interaction wakes the screen.

### 7.3. Timekeeping Across Deep Sleep

- The ESP32-C5's **RTC timer persists through deep sleep** using the internal RC oscillator.
- Drift is expected (several seconds per hour) but acceptable — the tablet re-syncs the clock on
  every connection.
- The time offset (received from the tablet) is stored in **RTC memory** (`RTC_DATA_ATTR`) so it
  survives deep sleep cycles.
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
| SSID | Configurable (default: `IRSensor-XXXX` where XXXX = last 4 of MAC) |
| Password | Configurable (default: generated, shown on OLED on first boot) |
| IP | `192.168.4.1` (default ESP-IDF SoftAP address) |
| Max clients | 1 (only the tablet) |

### 8.2. Protocol: HTTP REST API (Polling)

The ESP32 runs a lightweight HTTP server (`esp_http_server`). The app polls `GET /api/status`
at **~1 Hz** to track device state, recording progress, and battery level. No WebSocket is
needed — the simplicity of stateless REST is preferred on a single-core MCU with limited RAM.

#### REST Endpoints

| Method | Path | Description |
| :--- | :--- | :--- |
| `GET` | `/api/status` | Device status: battery (voltage + SoC%), free space, recording count, WiFi info, current state (`idle` / `recording`), and if recording: `{elapsed_s, total_s, recording_id}` |
| `POST` | `/api/record` | Start recording. Body: `{"duration_s": 30, "sample_rate_hz": 10000}`. Returns error if insufficient space. |
| `GET` | `/api/recordings` | List all recordings with metadata (id, timestamp, duration, sample rate, size, compressed flag) |
| `GET` | `/api/recordings/{id}` | Download a specific recording (binary, IREC format) |
| `DELETE` | `/api/recordings/{id}` | Delete a specific recording |
| `DELETE` | `/api/recordings` | Delete all recordings (with confirmation token) |
| `POST` | `/api/time` | Sync time from tablet. Body: `{"utc_us": <microseconds since epoch>}` |
| `POST` | `/api/compress` | Trigger compression of all uncompressed recordings |
| `POST` | `/api/settings` | Update device settings. Body: JSON of setting key-values |
| `GET` | `/api/settings` | Read current device settings |

#### Polling Pattern (App Side)

```
While connected:
  Every 1 second → GET /api/status
    - Update battery display, free space, connection indicator
    - If state == "recording": show progress bar (elapsed_s / total_s)
    - If state changed from "recording" → "idle": recording complete
      → GET /api/recordings/{id} to download
```

### 8.3. Time Synchronization Protocol

1. Tablet connects to ESP32 SoftAP.
2. App sends `POST /api/time` with current UTC timestamp in microseconds.
3. ESP32 stores the offset between `esp_timer_get_time()` and the received UTC time.
4. All subsequent recording timestamps are computed from this offset.
5. Offset is stored in RTC memory to survive deep sleep.
6. On reconnection, the app re-syncs time to correct any RTC drift.

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
| SSD1306 / U8g2 | OLED display driver (I2C) |
| MAX17043 driver | I2C fuel gauge — battery voltage and SoC% readings |
| FastLED or ESP-IDF RMT | WS2812B RGB LED control |
| Rotary encoder library | Encoder input handling (interrupt-driven) |
| ESP-IDF ADC Calibration | Curve fitting calibration |
| miniz / zlib | Optional recording compression |

### 9.3. FreeRTOS Task Architecture

> **Important**: The ESP32-C5 is a **single-core** RISC-V processor. All tasks share one core.
> Task priorities and yielding discipline are critical to prevent starvation.

| Task | Priority | Responsibility |
| :--- | :--- | :--- |
| **ADC DMA Drain** | Highest | Pull samples from DMA buffers → PSRAM staging buffer. Runs only during active recording. |
| **Storage Flush** | High | Write staged samples from PSRAM to LittleFS in chunks. Runs during recording. |
| **WiFi / HTTP** | Medium | Handle REST API requests. Yield-friendly. |
| **UI Task** | Low | OLED rendering, rotary encoder polling, LED updates. Suspended during recording (except LED). |
| **Battery Monitor** | Lowest | Periodic MAX17043 I2C reads (voltage + SoC%). Deferred during recording to minimize I2C bus activity. |
| **Idle / Watchdog** | System | FreeRTOS idle task and task watchdog. |

### 9.4. Recording Flow

```
1. Record command received (from app or device menu)
2. Validate: sufficient free space? → if no, refuse with warning
3. Freeze OLED to "Recording..." screen
4. Defer battery monitor reads (minimize I2C bus activity during recording)
5. Set RGB LED to pulsing red
6. Configure ADC continuous mode: OPT101 channel only, requested sample rate
7. Allocate PSRAM staging buffer
8. Record start timestamp (RTC-offset-corrected UTC microseconds)
9. Start ADC continuous (DMA begins)
10. DMA Drain task: read DMA buffers → append to PSRAM staging buffer
11. Storage Flush task: periodically write PSRAM buffer chunks to LittleFS temp file
12. On duration elapsed or stop command:
    a. Stop ADC continuous
    b. Flush remaining PSRAM buffer to flash
    c. Compute CRC32 of data payload
    d. Write final IREC header
    e. Rename temp file to final recording filename
13. Resume battery monitor and UI tasks
14. Update OLED with recording summary
15. Set RGB LED back to green (or cyan if tablet connected)
16. App discovers completion on next GET /api/status poll (≤1 s)
```

---

## 10. Tablet App Architecture

### 10.1. Platform & Technology

| Parameter | Value |
| :--- | :--- |
| Target device | Microsoft Surface Go 2 running Fedora Workstation (Linux) |
| App framework | **Tauri** (v2+) |
| Frontend | **Svelte** (SvelteKit) with TypeScript |
| Backend | Rust (Tauri core) |
| Source directory | `./app/` |

### 10.2. Key Frontend Libraries

| Library | Purpose |
| :--- | :--- |
| **uPlot** or **Chart.js** | High-performance time-series graphing with zoom/pan |
| **Svelte** | Reactive UI framework |
| **TypeScript** | Type-safe frontend code |
| Tauri HTTP / fetch API | Communication with ESP32 (REST polling) |

### 10.3. App Features

#### Core Features

1. **Device Connection**
   - Connect to ESP32 SoftAP WiFi network
   - Auto-detect device at `192.168.4.1`
   - Connection status indicator
   - Automatic time sync on connect

2. **Recording Control**
   - Set recording duration (5–300 seconds)
   - Set sample rate (1 kHz, 5 kHz, 10 kHz, 20 kHz)
   - Start / stop recording remotely
   - Real-time progress indicator during recording
   - Automatic download of recording data on completion

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
   - Delete recordings from device
   - View recording metadata (timestamp, duration, sample rate, size)
   - Name / tag / annotate recordings with notes

5. **Export**
   - Export recording data as **CSV** (timestamp_ms, raw_adc, voltage_mv)
   - Export recording data as **JSON**
   - Export graph as image (PNG/SVG)

6. **Device Settings** (remote configuration)
   - Screen brightness
   - Screen timeout
   - Default sample rate
   - Default recording duration
   - WiFi SSID and password

7. **Statistics**
   - Per-recording: min, max, mean, standard deviation of ADC values
   - Display in a summary panel alongside the graph

#### UX Details

- **Dark mode** (default) — appropriate for controlled-lighting IR measurement environments
- **Touch-optimized** — large tap targets, swipe gestures for the Surface Go 2 touch screen
- **Responsive layout** — adapts to the Surface Go 2's 10.5" 1920×1280 display
- **Free space indicator** — shows device free storage in the status bar
- **Auto-reconnect** — remembers ESP32 AP and reconnects automatically
- **Handles both compressed and uncompressed recordings** transparently (checks IREC flags byte,
  decompresses zlib if needed)

### 10.4. Local Storage

The app stores downloaded recordings locally on the tablet filesystem (via Tauri's fs API):

- Default location: `~/IRSensor/recordings/`
- Each recording saved as the original `.irec` binary file + a `.json` metadata sidecar
- The app maintains a local index/database of all downloaded recordings for offline browsing

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
| Storage format | Custom binary (IREC) on LittleFS | Compact (2 bytes/sample), CRC32 integrity, optional zlib compression |
| Settings persistence | ESP-IDF NVS (key-value) | Platform built-in; power-loss safe; no custom serialization code |
| Display during recording | Frozen OLED + RGB LED | Avoids I2C contention and CPU load on single-core MCU |
| Battery monitoring | MAX17043 I2C fuel gauge | Accurate ModelGauge™ SoC; no ADC channel conflict with OPT101; can read during recording |
| Time sync | Tablet sends UTC on connect; RTC persists in deep sleep | No external RTC hardware needed; drift corrected on each reconnection |
| Deep sleep wake | Long press (≥2 s) on encoder button via LP core | Ultra-low standby power; LP RISC-V core monitors GPIO |
| Max recording duration | 300 seconds | Prevents accidental flash exhaustion; ~6 MB at 10 kHz |
| OPT101 power | 3.3V from ESP32 rail | Device operates on battery (no 5V available unplugged); output range ~0–2.15 V usable |
| ADC attenuation | 6 dB (~0–2.2 V range) | Best match for OPT101 output ceiling at 3.3V supply |
