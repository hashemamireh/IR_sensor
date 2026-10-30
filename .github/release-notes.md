## Downloads

| File | For | SHA-256 |
| :--- | :--- | :--- |
| `{{DMG}}` | macOS 11 or later on Apple Silicon (M1 or later) | `{{DMG_SHA256}}` |
| `{{RPM}}` | Fedora 44 or later, x86_64 | `{{RPM_SHA256}}` |

To check a download, run this in the folder you downloaded it to, then compare the output
with the table above or with `SHA256SUMS`:

```bash
shasum -a 256 {{DMG}}    # macOS
sha256sum {{RPM}}        # Fedora
```

## Install on macOS

The app is ad-hoc signed, but not notarised by Apple.

1. Open the `.dmg`, drag **IR Sensor** to **Applications**, and open it. macOS says it
   "could not verify" the app. Click **Done**.
2. Go to **System Settings → Privacy & Security** and click **Open Anyway**. You only
   need to do this once.

If macOS instead says the app "is damaged", run this, then open the app again:

```bash
xattr -dr com.apple.quarantine "/Applications/IR Sensor.app"
```

## Install on Fedora

```bash
sudo dnf install webkit2gtk4.1 ./{{RPM}}
```

**IR Sensor** then appears in the app launcher. If its window opens blank, start it
from a terminal like this instead:

```bash
WEBKIT_DISABLE_DMABUF_RENDERER=1 irsensor-app
```
