# IR Sensor

A battery-powered handheld recorder for IR light pulses, with a companion desktop/tablet app.

- **The device** is an ESP32-C5 board with an OPT101 photodiode. It samples at 1–20 kHz,
  stores recordings on flash, and serves them over its own WiFi network.
- **The app** finds the device, starts recordings, downloads them automatically, and shows
  them as a zoomable graph. It also has overlays, a live view, and CSV/JSON export.

## Download

The app for **macOS on Apple Silicon** (M1 or later, macOS 11 or later):

| Version | File | Size | SHA-256 |
| :--- | :--- | :--- | :--- |
| 0.1.0 | [IR-Sensor_0.1.0_aarch64.dmg](https://github.com/hashemamireh/IR_sensor/releases/download/v0.1.0/IR-Sensor_0.1.0_aarch64.dmg) | 3.7 MB | `935c29a6e9422c2d2b0f7e7dcba048b636191b4f95cf6ffd9448d259497a8174` |

All versions are on the [Releases](https://github.com/hashemamireh/IR_sensor/releases) page.
Each release also has a `SHA256SUMS` file.

### Check the download

Run this in the folder you downloaded to. It should print the same hash as the table above:

```bash
shasum -a 256 IR-Sensor_0.1.0_aarch64.dmg
```

### Install and first launch

1. Open the `.dmg` and drag **IR Sensor** into **Applications**.
2. Open IR Sensor. The app is ad-hoc signed but not notarised by Apple, so the first time,
   macOS says it "could not verify" the app. Click **Done**.
3. Open **System Settings → Privacy & Security**, scroll down to "IR Sensor was blocked",
   and click **Open Anyway**. You only have to do this once.

If macOS instead says the app "is damaged", remove the download quarantine flag and open
it again:

```bash
xattr -dr com.apple.quarantine "/Applications/IR Sensor.app"
```

### Other platforms

There's no ready-made build yet for Intel Macs, Linux or Windows. Build the app from source
by following [app/README.md](app/README.md). A Fedora `.rpm` is planned.

## Using it

1. Turn on the device and join its `IRSensor-XXXX` WiFi network. The password is on the
   device's screen under **WiFi → Show SSID & IP**.
2. Open the app. It finds the device at `192.168.4.1`, sets the device's clock, and
   downloads any recordings it doesn't have yet.

Recordings are saved in `~/IRSensor/recordings/`.

## Repository layout

| Path | What's there |
| :--- | :--- |
| [`spec.md`](spec.md) | The design: hardware, sampling, file format, REST API, app behaviour |
| [`firmware/`](firmware/) | ESP32-C5 firmware (ESP-IDF / PlatformIO); see [`BRINGUP.md`](firmware/BRINGUP.md) |
| [`app/`](app/) | The companion app: Tauri 2, SvelteKit, TypeScript, uPlot |
| [`tools/mock_device/`](tools/mock_device/) | A Python mock of the device's REST API, for working on the app without hardware |
| [`wiring_diagram.md`](wiring_diagram.md) | How the parts are wired |

## License

IR Sensor is free software, released under the
[GNU General Public License v3.0 or later](LICENSE).
