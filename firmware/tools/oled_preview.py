"""Renders OLED screen dumps from the menu tests into one PNG contact sheet.

    mkdir -p /tmp/oled && UI_PREVIEW_DIR=/tmp/oled pio test -e native -f test_menu
    python3 tools/oled_preview.py /tmp/oled oled_screens.png

Pixels in the top 16 rows are drawn yellow and the rest blue, like the panel.
Standard library only.
"""
import pathlib
import struct
import sys
import zlib

SCALE, PAD, COLS = 3, 12, 3
BACKGROUND, OFF = (24, 24, 28), (0, 0, 0)
YELLOW, BLUE = (255, 214, 10), (90, 190, 255)


def png(width, height, rows):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))
    raw = b"".join(b"\x00" + bytes(v for px in row for v in px) for row in rows)
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9))
            + chunk(b"IEND", b""))


def main(src, out):
    files = sorted(pathlib.Path(src).glob("*.txt"))
    if not files:
        sys.exit(f"no screen dumps in {src}")
    cell_w, cell_h = 128 * SCALE + 2 * PAD, 64 * SCALE + 2 * PAD
    n_rows = (len(files) + COLS - 1) // COLS
    img = [[BACKGROUND] * (cell_w * COLS) for _ in range(cell_h * n_rows)]

    for i, f in enumerate(files):
        ox, oy = (i % COLS) * cell_w + PAD, (i // COLS) * cell_h + PAD
        for y, line in enumerate(f.read_text().splitlines()):
            for x, c in enumerate(line):
                color = (YELLOW if y < 16 else BLUE) if c == "#" else OFF
                for dy in range(SCALE):
                    img[oy + y * SCALE + dy][ox + x * SCALE:ox + (x + 1) * SCALE] = [color] * SCALE

    pathlib.Path(out).write_bytes(png(cell_w * COLS, cell_h * n_rows, img))
    print(f"{out}: {', '.join(f.stem for f in files)}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
