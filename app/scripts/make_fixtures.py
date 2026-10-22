"""Regenerates the IREC test fixtures in src/lib/fixtures/ from the mock device.

    python3 scripts/make_fixtures.py      (or: pnpm fixtures)

Uses build_irec / compress_irec from tools/mock_device/server.py, so the reader is
tested against the exact bytes the mock serves. expected.json holds what Python's
reference decoding sees (header fields, CRC, sample stats) for the tests to compare.
"""
import json
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(HERE, "..", "src", "lib", "fixtures")
sys.dont_write_bytecode = True  # keep tools/ untouched
sys.path.insert(0, os.path.join(REPO, "tools", "mock_device"))

import server  # noqa: E402

START_UTC_US = 1_792_000_000_000_000  # 2026-10-14T17:46:40Z


def describe(data):
    h = server.decode_header(data)
    payload = data[server.HEADER_SIZE:]
    if h["flags"] & server.FLAG_COMPRESSED:
        payload = zlib.decompress(payload)
    samples = struct.unpack("<%dH" % (len(payload) // 2), payload)
    return {
        "sample_rate_mhz": h["sample_rate_mhz"],
        "sample_count": h["sample_count"],
        "start_utc_us": h["start_utc_us"],
        "crc32": h["crc32"],
        "flags": h["flags"],
        "atten_tenths_db": h["atten_tenths_db"],
        "first": list(samples[:8]),
        "last": samples[-1],
        "sum": sum(samples),
        "min": min(samples),
        "max": max(samples),
    }


def main():
    os.makedirs(OUT, exist_ok=True)
    plain = server.build_irec(7, 10_000_000, 4000, START_UTC_US)
    files = {
        "plain.irec": plain,
        "compressed.irec": server.compress_irec(plain),
        # A non-integer actual rate, as the firmware's measured table may produce
        "fractional_rate.irec": server.build_irec(8, 10_004_300, 3000, START_UTC_US + 1),
        "overrun.irec": server.build_irec(9, 20_000_000, 8000, START_UTC_US + 2, overrun=True),
    }
    assert files["compressed.irec"][25] & server.FLAG_COMPRESSED, "fixture didn't compress"

    expected = {}
    for name, data in files.items():
        with open(os.path.join(OUT, name), "wb") as f:
            f.write(data)
        expected[name] = describe(data)
    expected["calibration_mv"] = server.calibration_table()
    with open(os.path.join(OUT, "expected.json"), "w") as f:
        json.dump(expected, f, indent=1)
        f.write("\n")
    print("wrote %d fixtures to %s" % (len(files), os.path.relpath(OUT)))


if __name__ == "__main__":
    main()
