# IR Sensor app

The companion tablet app for the IR sensor recorder (`spec.md` §10). It's built with
Tauri 2, SvelteKit (as a static single-page app), TypeScript and uPlot. The target is
a Surface Go 2 running Fedora; it's developed on macOS.

Milestone 1 does the following:

- Connects to the device and polls its status.
- Syncs the device clock.
- Records, with rate and duration pickers and a progress bar.
- Auto-downloads recordings.
- Lists recordings on the device and on the tablet.
- Deletes recordings from the device and/or the tablet, including "delete all on device".
- Shows a recording as a zoomable graph in mV, with an overrun warning.

Milestone 2 adds:

- **Overlays:** up to 8 recordings on one graph, aligned by start or by first rising
  edge, with a manual offset per trace and live values under the pointer.
- **Export:** CSV or JSON, of the whole recording or the visible window, at full or
  lower (block-averaged) resolution.
- **Device settings:** brightness, screen timeout, and the device's Quick Start rate
  and duration.
- **Names, tags and notes** on tablet recordings, and a search box for the list.

Live view (spec §3.6) shows the sensor continuously without recording: a scrolling
min/max strip chart with a big current reading and peak, and a Record button.

Measurement cursors wait for the Surface test. Statistics and image export are
dropped for now.

## Setup

You need Rust (stable), Node 20 or newer, pnpm, and the system libraries Tauri needs.

### macOS

```bash
xcode-select --install
brew install rustup node pnpm
rustup default stable
```

Homebrew's `rustup` is keg-only, so add it to your `PATH` (for example in `~/.zshrc`):

```bash
export PATH="/opt/homebrew/opt/rustup/bin:$PATH"
```

### Fedora (Surface Go)

```bash
sudo dnf check-update
sudo dnf install webkit2gtk4.1-devel openssl-devel curl wget file \
  libappindicator-gtk3-devel librsvg2-devel libxdo-devel nodejs
sudo dnf group install "c-development"
curl --proto '=https' --tlsv1.2 https://sh.rustup.rs -sSf | sh
curl -fsSL https://get.pnpm.io/install.sh | sh -
```

These are Tauri's [Linux prerequisites](https://v2.tauri.app/start/prerequisites/)
plus Node, Rust and pnpm.

### Install

```bash
cd app
pnpm install
```

`package.json` pins the pnpm version, and `pnpm-lock.yaml` pins every dependency.

### Keep the Rust build out of the synced folder

This project lives in a ProtonDrive folder, and a Tauri build directory runs to a few
GB. Point Cargo somewhere that isn't synced:

```bash
export CARGO_TARGET_DIR="$HOME/Library/Caches/irsensor-app/target"   # macOS
export CARGO_TARGET_DIR="$HOME/.cache/irsensor-app/target"           # Linux
```

`node_modules` (about 150 MB) stays in `app/` and is synced too. The repo's git ignores
it, but ProtonDrive doesn't.

## Run

### Against the mock device

```bash
python3 ../tools/mock_device/server.py --seed 3          # terminal 1
IRSENSOR_DEVICE_URL=http://127.0.0.1:8080 pnpm tauri dev  # terminal 2
```

Without `IRSENSOR_DEVICE_URL`, the app uses whichever device you last chose. Click
**Device: …** in the top-right corner, then its **Connection** tab, to switch between the real device, the mock, or
another address. The mock's README lists flags worth trying, such as
`--overrun-every 3`, `--no-calibration`, `--no-cors`, `--wake-after 5` and
`--time-scale 5`.

### Against the real device

1. Join the `IRSensor-XXXX` WiFi network in your system's WiFi settings. The password
   is on the device's screen, under WiFi → Show SSID & IP.
2. Run `pnpm tauri dev` and pick **IR sensor** (`http://192.168.4.1`).

The app doesn't join the WiFi itself (decision D1). When the device can't be reached,
it says which network to join.

### Environment variables

| Variable | Effect |
| :--- | :--- |
| `IRSENSOR_DATA_DIR` | Where recordings are stored. The default is `~/IRSensor`. Use a scratch folder to keep test data out of your real one. |
| `IRSENSOR_DEVICE_URL` | The device URL for this run only. It isn't saved. |

### Browser-only mode

`pnpm dev` serves the UI at <http://localhost:1420> without Tauri. Two things differ:

- Requests use the browser's `fetch`, so only the mock works, because it sends CORS
  headers and the real firmware doesn't.
- Recordings are kept in memory, so they're gone when the page reloads.

It's useful for quick UI work.

## Test

```bash
pnpm test     # unit tests: IREC reader, decimation, list merging
pnpm check    # svelte-check / TypeScript
pnpm fixtures # regenerate src/lib/fixtures/ from the mock's build_irec
```

The IREC fixtures are built by `tools/mock_device/server.py` (`build_irec`,
`compress_irec`). `expected.json` holds Python's own decoding of them, so the
TypeScript reader is checked against the same bytes the mock serves. Regenerate the
fixtures if the IREC format changes.

## Build

```bash
pnpm tauri build
```

On Fedora this produces an `.rpm`, a `.deb` and an AppImage under
`$CARGO_TARGET_DIR/release/bundle/`.

## How it works

### Requests

All device traffic goes through `src/lib/api.ts`. Inside Tauri it uses the HTTP plugin,
so requests are made from Rust and CORS doesn't apply. Outside Tauri it falls back to
`fetch`.

The device answers one request at a time, so the app queues its own requests too. A
status poll that waits behind a multi-MB download is not treated as a disconnect.

### Connection (`src/lib/device.svelte.ts`)

- It polls `/api/status` every second. After 2 failed polls the device counts as
  unreachable, for example after `POST /api/sleep` or after walking out of range.
- On each new connection, once the device is first idle, it does these in order:
  1. Syncs the time.
  2. Fetches the calibration table and caches it per device.
  3. Reads the recording list.
- A 409 just means "not now". The work is picked up again at the next idle poll.

### Auto-download (decision D5)

Whenever the device is idle and its list may have changed, the app downloads any
recording it doesn't have on the tablet, oldest first. This also covers:

- recordings started from the device's own button
- recordings made while the tablet was away
- 0-sample recordings, which the firmware discards: they simply never appear in the
  list

If something goes wrong:

- **Network errors** are retried 3 times with back-off. After that, the row shows
  **Retry**.
- **CRC mismatch:** the app retries once. If it fails again, it keeps the file and
  marks it "CRC failed".
- **Deleted from the tablet only:** the recording is never fetched again
  automatically. The row offers **Download** instead.

### Storage (`src/lib/storage.ts`, `src/lib/recordings.ts`; decision D4)

```
~/IRSensor/recordings/IRSensor-AB12_00042.irec   the file exactly as downloaded
~/IRSensor/recordings/IRSensor-AB12_00042.json   its sidecar
~/IRSensor/devices/IRSensor-AB12.json            last calibration table per device
```

The sidecars are the index: the app scans them at startup, and there's no separate
database.

- **How recordings are matched:** device and tablet copies match on SSID, ID and start
  time, because IDs restart if the device's NVS is erased.
- **File names:** a different recording that wants an existing name gets `-2`, `-3`
  and so on.
- **What the sidecar holds:** the device's list entry, the CRC result, and the device's
  calibration table at download time (or `null` if the chip has none). See `Sidecar`
  in `recordings.ts` for the schema.
- **Names, tags and notes** live only in the sidecar, on this tablet; the device has
  no API for them. Saving a recording again (a manual re-download) keeps them.

### Graph (`src/lib/components/Graph.svelte`, `src/lib/graphView.ts`, `src/lib/decimate.ts`)

- **Time:** taken from the header's actual rate in millihertz, not the nominal rate.
- **Values:** shown in mV through the sidecar's table, but only if the table's
  attenuation matches the header's byte 0x1A. Otherwise they're shown as raw counts,
  with a note.
- **Speed:** uPlot only gets the visible window, reduced to the minimum and maximum of
  each pixel column at their real sample times. Pulse edges stay in place, and once
  you zoom in far enough you see the raw samples. A per-trace summary (min/max per
  block of 64 and 4096 samples) keeps each redraw about the same cost however long the
  recordings are: 8 × 3 M samples redraw in about 3 ms on an M-series Mac instead of 48.
- **Overlays:** "+ Compare" on a list row adds it to the graph; the first recording
  is the main one (its name, tags, notes and Export). Each trace's legend chip shows
  its value under the pointer, hides or shows it, and holds a manual offset in ms.
  - **By start:** every recording's first sample at t = 0.
  - **By first edge:** every recording's first rising crossing of its min–max
    midpoint at t = 0, interpolated between samples. A trace with no edge stays
    aligned by start, with a note.
  - **Units:** mV only if every trace has a matching calibration table; otherwise all
    are shown in counts.
- **Gestures:** the app handles zoom and pan itself:
  - Scroll wheel or trackpad: zoom around the pointer. Sideways scroll pans.
  - Drag: pan.
  - Two-finger pinch: keeps the two times under your fingers pinned, so it zooms and
    pans at once.
  - Double-click: reset.

### Export (`src/lib/export.ts`, `src/lib/exportFile.ts`)

Export… exports the main recording, always from the real samples, never the graph's
thinned view.

- **CSV:** `timestamp_ms,raw_adc,voltage_mv`. Times are ms since the recording's first
  sample, to 1 µs. `voltage_mv` is empty without a calibration table.
- **JSON:** one object: `recording` (metadata, name, tags, notes), the rates,
  `first_timestamp_ms` and `timestamp_step_ms`, `calibration_mv`, then the `raw_adc`
  and `voltage_mv` arrays.
- **Range:** the whole recording, or the part of it in the visible window.
- **Resolution:** full, or a lower rate made by averaging each block of N samples.
  Each row is timed at its block's centre, and `voltage_mv` is averaged after
  converting each sample. Times are then only good to about one output period.
- **Where:** a Save dialog starting in `~/IRSensor/exports/`. The file is written in
  chunks, so memory stays flat; a cancelled or failed export removes the partial
  file. In browser mode the file downloads instead.

### Device settings

The **Settings** tab of the **Device: …** menu in the status bar reads the device's settings (allowed in any state) and
saves only what you changed. Saving is disabled while the device is busy; nothing is
queued. The Quick Start values only affect the device's own menu, not the app's
record settings.

### Live view (`src/lib/live.ts`, `src/lib/components/LiveView.svelte`)

- **Starting:** **Live** in the top bar starts live mode, or shows / hides the view while
  it runs. Live mode started from the device's own menu (or already running when the
  app connects) opens the view by itself.
- **Data:** while the device reports `state: "live"`, the app polls
  `GET /api/live?since=…` about 4×/s. Each 20 ms bucket is [min, max, mean] raw counts.
  The app keeps 30 s of buckets and draws the min–max band plus the mean, newest at the
  right (10 s or 30 s window). Missed buckets (`first_seq > since`) leave a break in the
  chart, and the device's dropped-sample count shows as a badge.
- **Reading:** "Now" is the mean of the last 100 ms (as on the OLED); "Peak" is the
  highest max in the window. mV through this connection's calibration table, else counts.
- **Record** uses the Record popover's rate and duration; the device ends live mode
  first, and the view closes.
- **Ending:** polling doesn't count as activity, so live mode ends after the device's
  Live timeout (Device menu → Settings) unless you do something. When it ends from the device
  (timeout, its button, another client) the view keeps the last readings and offers
  **Restart live view**.
- **Allowed during live mode:** manual downloads, deletes and settings. Auto-downloads
  wait until the device is idle, because flash reads can make live mode drop samples.

### Times

Device times are UTC. The app shows them in local time, with UTC on hover. A
timestamp before 2024 means the device clock wasn't set (after a full power loss), and
is shown as "clock not set".

## Please check on the Surface

These can only be checked on the real tablet. Please report back:

- [ ] The window opens. If it's blank, try `WEBKIT_DISABLE_DMABUF_RENDERER=1 pnpm tauri dev`.
- [ ] Two-finger pinch on the graph zooms the graph, not the whole page.
- [ ] One-finger drag pans the graph, and the recordings list scrolls by touch.
- [ ] Panning and zooming a 300 s, 10 kHz recording (3 M samples) feels smooth enough.
- [ ] Buttons and chips are easy to hit with a finger.
- [ ] The recording list and graph fit on the 1920×1280 screen at the default scaling.
- [ ] With 3–8 long recordings overlaid, zoom and pan still feel smooth.
- [ ] The legend chips (hide/show, − / + offset, ✕) are big enough to tap.
- [ ] Export of a long recording to CSV finishes, and the file opens.
- [ ] Live view: the strip chart scrolls smoothly and the big reading is readable at arm's length.
- [ ] The Record, Export and Device popovers open and close comfortably by touch (tap outside closes them).

## Layout

```
app/
├── src/lib/
│   ├── api.ts               typed REST client (spec §8.2)
│   ├── http.ts              Tauri HTTP plugin / fetch, with timeouts
│   ├── device.svelte.ts     connection, polling, time sync, auto-download
│   ├── irec.ts              IREC reader: header, zlib (fflate), CRC-32
│   ├── recordings.ts        sidecar schema, device ↔ tablet matching
│   ├── storage.ts           files under ~/IRSensor (Tauri fs plugin)
│   ├── decimate.ts          min/max decimation + per-trace summary for the graph
│   ├── graphView.ts         uPlot wrapper (several traces) + gestures
│   ├── align.ts             overlay alignment: start, first edge, manual offset
│   ├── export.ts            CSV / JSON export, chunked, optional averaging
│   ├── live.ts              live-mode bucket buffer: gaps, reading, peak, chart data
│   ├── recordSettings.svelte.ts  record rate / duration, shared and remembered
│   ├── exportFile.ts        Save dialog + streaming the export to disk
│   ├── format.ts            local time, sizes, rates
│   ├── components/          StatusBar, RecordPanel, RecordingsList, Graph, …
│   └── fixtures/            IREC test files from the mock
├── src/routes/+page.svelte  the single screen
├── scripts/make_fixtures.py
└── src-tauri/               Rust: plugins, fs scope, IRSENSOR_* env vars
```
