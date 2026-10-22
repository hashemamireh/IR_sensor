# IR Sensor Device — Firmware

ESP-IDF firmware for a battery-powered, handheld IR sensor recorder built on the [Waveshare ESP32-C5-WIFI6-KIT-N32R8](https://docs.waveshare.com/ESP32-C5-WIFI6-KIT).

Captures analog readings from an **OPT101 photodiode** (with IR bandpass filter) at up to **10 kHz** via DMA, stores recordings to on-chip flash, and serves them over WiFi to a companion tablet app.

> [!IMPORTANT]
> This firmware was written and cross-compiled without access to the physical board. It builds cleanly for ESP32-C5 but has **not been flashed or tested on hardware**. Expect pin/peripheral issues on first boot.

## Hardware

| Component | Part |
| :--- | :--- |
| MCU | ESP32-C5 — single-core RISC-V @ 240 MHz, 32 MB flash, 8 MB PSRAM |
| Sensor | OPT101 photodiode + transimpedance amp (ADC1, continuous DMA) |
| Display | 0.96″ SSD1306 I2C OLED 128×64 |
| Fuel gauge | MAX17043 I2C LiPo gauge (ModelGauge™ SoC) |
| Input | KY-040 rotary encoder (rotate + press) |
| Status LED | Onboard WS2812B RGB (RMT driver) |
| Power | 3.7 V LiPo, USB-C charging |

## Getting Started

### Prerequisites

- [PlatformIO CLI](https://docs.platformio.org/en/latest/core/installation.html) or the VS Code extension

### Build & Flash

```sh
cd firmware
pio run                    # build
pio run -t upload          # flash (connect board via USB-C)
pio device monitor         # serial monitor @ 115200 baud
```

### Synthetic Build (no sensor needed)

`irsensor_synthetic` is the same firmware, except the recorder generates a fake OPT101 signal (noisy baseline with a 5 ms pulse every 250 ms) instead of reading the ADC. Use it to check the whole recording pipeline — capture, save, list, download — on a bare board:

```sh
pio run -e irsensor_synthetic -t upload
```

Every recording logs a rough measured sample rate, e.g. `Captured 300000 samples in 30.002 s — ~9999.3 Hz`. With the real ADC build these numbers fill the actual-rate table in `lib/irsensor_core/recording.cpp` (spec §3.5).

### Unit Tests (no board needed)

Hardware-independent logic lives in `lib/irsensor_core` and is tested on the host with Unity:

```sh
pio test -e native
```

PlatformIO will automatically fetch ESP-IDF and the managed components (`littlefs`, `led_strip`) on the first build.

## Project Structure

```
firmware/
├── platformio.ini              # Build config (board, framework, flags)
├── partitions.csv              # Custom partition table (NVS + 3 MB app + ~29 MB storage)
├── sdkconfig.defaults          # ESP-IDF Kconfig overrides
├── boards/
│   └── waveshare_esp32c5_n32r8.json  # Custom board definition
├── include/
│   ├── config.h                # App-level constants (battery interval, WiFi, storage label)
│   └── pins.h                  # GPIO / I2C / ADC pin assignments
└── src/
    ├── main.cpp                # Entry point — initializes all subsystems & tasks
    ├── api.cpp/.h              # HTTP REST API (all endpoints, 409 gating)
    ├── battery.cpp/.h          # MAX17043 fuel gauge + monitor task
    ├── calibration.cpp/.h      # ADC raw → mV (eFuse curve fitting)
    ├── device_state.cpp/.h     # idle/recording/… state + lock for flash work
    ├── encoder.cpp/.h          # Rotary encoder + button
    ├── led.cpp/.h              # WS2812B status LED
    ├── recorder.cpp/.h         # ADC continuous capture → PSRAM → IREC file
    ├── recordings.cpp/.h       # Recording files: index, list, delete, ID counter
    ├── settings_store.cpp/.h   # User settings in NVS
    ├── storage.cpp/.h          # LittleFS mount + usage info
    ├── wifi_ap.cpp/.h          # WiFi SoftAP, generated password, client events
    ├── idf_component.yml       # ESP-IDF component dependencies
    └── CMakeLists.txt          # Source list for ESP-IDF build
├── lib/irsensor_core/          # Hardware-independent logic (no ESP-IDF includes)
│   ├── irec.*                  # IREC header encode/decode, CRC-32
│   ├── recording.*             # Rates, duration caps, free-space check, IDs/filenames
│   ├── settings.*              # Settings defaults, validation, JSON
│   ├── json_writer.*           # Streaming JSON writer
│   ├── api_model.*             # Device state, 409 gating, status/recording JSON
│   └── synthetic.*             # Fake OPT101 signal for the synthetic build
└── test/                       # Host-side Unity tests for lib/irsensor_core
```

Code in `lib/irsensor_core` must not include ESP-IDF headers, so it keeps building for the `native` test environment.

## Pin Map

| Function | GPIO | Notes |
| :--- | :--- | :--- |
| OPT101 output | 4 | ADC1_CH3 |
| I2C SDA | 23 | Shared: OLED (0x3C) + MAX17043 (0x36) |
| I2C SCL | 24 | |
| Encoder CLK | 8 | Rotation signal A |
| Encoder DT | 10 | Rotation signal B |
| Encoder SW | 5 | Button (LP_GPIO5, deep-sleep wake) |
| WS2812B LED | 27 | |

## Architecture

The firmware runs on FreeRTOS with tasks prioritized for a single-core MCU:

| Task | Priority | Role |
| :--- | :--- | :--- |
| ADC DMA drain | Highest | Moves samples from DMA → PSRAM staging buffer (recording only) |
| Storage flush | High | Writes PSRAM buffer → LittleFS in chunks (recording only) |
| WiFi / HTTP | Medium | REST API server |
| UI | Low | OLED + encoder + LED (frozen during recording) |
| Battery monitor | Lowest | Periodic MAX17043 reads (deferred during recording) |

### WiFi & API

The device runs as a **WiFi SoftAP** (`IRSensor-XXXX`). The companion app connects directly and uses a REST API:

| Method | Endpoint | Description |
| :--- | :--- | :--- |
| `GET` | `/api/status` | Device state, battery, free space |
| `POST` | `/api/record` | Start a recording |
| `POST` | `/api/record/stop` | Stop the current recording early |
| `GET` | `/api/recordings` | List all recordings |
| `GET` | `/api/recordings/{id}` | Download recording (IREC binary) |
| `DELETE` | `/api/recordings/{id}` | Delete a recording |
| `DELETE` | `/api/recordings` | Delete all (body `{"confirm":"DELETE_ALL"}`) |
| `POST` | `/api/time` | Sync clock from tablet |
| `POST` | `/api/compress` | Compress all saved recordings |
| `GET/POST` | `/api/settings` | Read/write device settings |
| `GET` | `/api/calibration` | Raw → mV lookup table |

Response and error formats are in [`spec.md`](../spec.md) §8.2. Not implemented yet: `/api/compress`.

The SoftAP password is generated on first boot, stored in NVS, and printed to the serial log (the OLED will show it once the UI exists).

### Recording Format (IREC)

32-byte header followed by `uint16_le[]` ADC samples (2 bytes each). Includes CRC32, UTC timestamp, and an optional zlib compression flag. See [`spec.md`](../spec.md) §4.2 for the full spec.

## Known Build Issues

### SCons version conflict (PlatformIO Core 6.2.0 + pioarduino)

The pioarduino fork of `platform-espressif32` ships with `tool-scons@4.8.1`, but PlatformIO Core 6.2.0 force-upgrades it to 4.11.1, causing `ModuleNotFoundError: No module named 'SCons.Tool.FortranCommon'`.

**Fix:** Patch `~/.platformio/platforms/espressif32/platform.json` — change the `tool-scons` entry to:

```json
"tool-scons": {
  "type": "tool",
  "optional": true,
  "owner": "platformio",
  "version": "~4.41101.0"
}
```

This needs to be re-applied after running `pio pkg update -p espressif32`.

### Missing Python packages (Homebrew PlatformIO)

If PlatformIO was installed via Homebrew, its bundled Python may be missing packages the ESP-IDF builder needs. Install into PlatformIO's own Python (adjust the Cellar path to match your version):

```sh
/opt/homebrew/Cellar/platformio/6.2.0_1/libexec/bin/python -m pip install pyyaml littlefs-python fatfs-ng
```

### Custom board definition

The board JSON in `boards/waveshare_esp32c5_n32r8.json` must also exist at `~/.platformio/platforms/espressif32/boards/waveshare_esp32c5_n32r8.json` for PlatformIO to find it. Copy it there if the platform is reinstalled:

```sh
cp boards/waveshare_esp32c5_n32r8.json ~/.platformio/platforms/espressif32/boards/
```

## Configuration

Recording constants live in [`lib/irsensor_core/recording.h`](lib/irsensor_core/recording.h), settings defaults in [`settings.h`](lib/irsensor_core/settings.h):

| Constant | Default | Description |
| :--- | :--- | :--- |
| `recording::DEFAULT_SAMPLE_RATE_HZ` | 10 000 | ADC sample rate |
| `recording::MAX_DURATION_S` | 300 | Cap at 1–10 kHz (≈ 6 MB at 10 kHz) |
| `recording::MAX_DURATION_20KHZ_S` | 120 | Cap at 20 kHz (PSRAM budget) |
| `ADC_POOL_BYTES` (`config.h`) | 16 KB | DMA result pool — ~200 ms at 20 kHz; raise if recordings report overrun |
| `settings::Settings::default_duration_s` | 30 | Default recording length |

Other device constants are in [`include/config.h`](include/config.h), e.g. `BATTERY_READ_INTERVAL_MS` (30 000).

Pin assignments are in [`include/pins.h`](include/pins.h). The custom partition table is in [`partitions.csv`](partitions.csv).

## License

See the root of this repository for license information.
