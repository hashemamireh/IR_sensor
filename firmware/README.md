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

PlatformIO will automatically fetch ESP-IDF and the managed components (`littlefs`, `led_strip`) on the first build.

## Project Structure

```
firmware/
├── platformio.ini              # Build config (board, framework, flags)
├── partitions.csv              # Custom partition table (NVS + OTA + 25 MB storage)
├── sdkconfig.defaults          # ESP-IDF Kconfig overrides
├── boards/
│   └── waveshare_esp32c5_n32r8.json  # Custom board definition
├── include/
│   ├── config.h                # App-level constants (sample rates, IREC format, etc.)
│   └── pins.h                  # GPIO / I2C / ADC pin assignments
└── src/
    ├── main.cpp                # Entry point — initializes all subsystems & tasks
    ├── idf_component.yml       # ESP-IDF component dependencies
    └── CMakeLists.txt          # Source list for ESP-IDF build
```

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
| `GET` | `/api/recordings` | List all recordings |
| `GET` | `/api/recordings/{id}` | Download recording (IREC binary) |
| `DELETE` | `/api/recordings/{id}` | Delete a recording |
| `POST` | `/api/time` | Sync clock from tablet |
| `POST` | `/api/compress` | Compress all saved recordings |
| `GET/POST` | `/api/settings` | Read/write device settings |

### Recording Format (IREC)

32-byte header followed by `uint16_le[]` ADC samples (2 bytes each). Includes CRC32, UTC timestamp, and an optional zlib compression flag. See [`overview.md`](../overview.md) §4.2 for the full spec.

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

Key constants live in [`include/config.h`](include/config.h):

| Constant | Default | Description |
| :--- | :--- | :--- |
| `DEFAULT_SAMPLE_RATE_HZ` | 10 000 | ADC sample rate |
| `DEFAULT_DURATION_S` | 30 | Default recording length |
| `MAX_DURATION_S` | 300 | Hard cap (≈ 6 MB at 10 kHz) |
| `BATTERY_READ_INTERVAL_MS` | 30 000 | Fuel gauge polling interval |

Pin assignments are in [`include/pins.h`](include/pins.h). The custom partition table is in [`partitions.csv`](partitions.csv).

## License

See the root of this repository for license information.
