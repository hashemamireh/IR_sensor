# IR Sensor

A battery-powered handheld recorder for IR light pulses, with a companion desktop/tablet app.

- **The device** is an ESP32-C5 board with an OPT101 photodiode. It samples at 1–20 kHz,
  stores recordings on flash, and serves them over its own WiFi network.
- **The app** finds the device, starts recordings, downloads them automatically, and shows
  them as a zoomable graph. It also has overlays, a live view, and CSV/JSON export.

## Download

Get the app from the **[latest release](https://github.com/hashemamireh/IR_sensor/releases/latest)**:

| Platform | File |
| :--- | :--- |
| macOS 11 or later, Apple Silicon (M1 or later) | `IR-Sensor_<version>_aarch64.dmg` |
| Fedora 44 or later, x86_64 | `IR-Sensor_<version>_x86_64.rpm` |

The release notes list each file's SHA-256, and so does the `SHA256SUMS` file attached to
the release. To check a download, run this in the folder you downloaded it to, and compare
the output with the release's hash:

```bash
shasum -a 256 IR-Sensor_*.dmg    # macOS
sha256sum IR-Sensor_*.rpm        # Fedora
```

### Install on macOS

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

### Install on Fedora

```bash
sudo dnf install webkit2gtk4.1 ./IR-Sensor_<version>_x86_64.rpm
```

**IR Sensor** then appears in the app launcher. If its window opens blank, start it from a
terminal with `WEBKIT_DISABLE_DMABUF_RENDERER=1 irsensor-app`.

### Other platforms

There's no ready-made build for Intel Macs, other Linux distributions or Windows. Build the
app from source by following [app/README.md](app/README.md).

### Making a release

Releases are built by GitHub Actions ([`.github/workflows/release.yml`](.github/workflows/release.yml)):

1. Set the new version in `app/src-tauri/tauri.conf.json` (CI checks the tag against this
   one), `app/package.json` and `app/src-tauri/Cargo.toml`. Then update `Cargo.lock` and
   commit all four:

   ```bash
   cargo update --workspace --offline --manifest-path app/src-tauri/Cargo.toml
   ```
2. Tag the commit and push the tag:

   ```bash
   git tag v0.2.0 && git push origin v0.2.0
   ```

3. The workflow builds the `.dmg` and the `.rpm` and creates a **draft** release with both
   files, `SHA256SUMS` and the notes. Check it on the Releases page and click **Publish**.

To test a build without making a release, run the workflow by hand (**Actions → Release →
Run workflow**). The files are then attached to that run as artifacts.

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
