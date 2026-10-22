"""Checks a recording downloaded from the device (spec §4.2, §4.5).

    python3 tools/check_irec.py 00001.irec                  # header, CRC, signal stats, pulses
    python3 tools/check_irec.py 00001.irec --period-ms 250  # synthetic build: pulses every 250 ms?
    python3 tools/check_irec.py 00001.irec --list-pulses    # every pulse's start and width

Decompresses zlib payloads (flags bit 0). Exits non-zero if any check fails.
Standard library only.
"""
import argparse
import datetime
import struct
import sys
import zlib

HEADER = struct.Struct("<4sBIIQIBB5s")  # magic, version, rate_mhz, count, start_us, crc, flags, atten (0.1 dB), reserved
FLAG_COMPRESSED, FLAG_OVERRUN = 0x01, 0x02


def find_pulses(samples, threshold):
    """(start_index, end_index) of each run above threshold, ignoring one already high at index 0."""
    pulses, start = [], None
    for i in range(1, len(samples)):
        if samples[i] > threshold >= samples[i - 1]:
            start = i
        elif samples[i] <= threshold < samples[i - 1] and start is not None:
            pulses.append((start, i))
            start = None
    return pulses


def describe(values):
    return f"min {min(values):.3f}  mean {sum(values) / len(values):.3f}  max {max(values):.3f}"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("file")
    ap.add_argument("--threshold", type=int, help="pulse threshold in ADC counts (default: midway min–max)")
    ap.add_argument("--period-ms", type=float, help="expected pulse period; fails if any spacing is off by more than one sample")
    ap.add_argument("--list-pulses", action="store_true")
    args = ap.parse_args()

    data = open(args.file, "rb").read()
    ok = True

    def check(cond, msg):
        nonlocal ok
        print(("PASS  " if cond else "FAIL  ") + msg)
        ok = ok and cond

    if len(data) < HEADER.size:
        sys.exit("FAIL  file shorter than the 32-byte header")
    magic, version, rate_mhz, count, start_us, crc, flags, atten, _ = HEADER.unpack_from(data)
    check(magic == b"IREC" and version == 1, f"magic {magic!r}, version {version}")
    if not ok:
        sys.exit(1)

    payload = data[HEADER.size:]
    if flags & FLAG_COMPRESSED:
        try:
            payload = zlib.decompress(payload)
        except zlib.error as e:
            check(False, f"zlib payload: {e}")
            sys.exit(1)
    check(len(payload) == count * 2, f"payload {len(payload)} bytes for {count} samples")
    check(zlib.crc32(payload) == crc, f"CRC-32 {zlib.crc32(payload):08x} (header {crc:08x})")
    check(not flags & FLAG_OVERRUN, "no overrun (samples lost during capture)" if not flags & FLAG_OVERRUN
          else "OVERRUN flag set — samples were lost, timing unreliable")

    rate = rate_mhz / 1000
    when = datetime.datetime.fromtimestamp(start_us / 1e6, datetime.timezone.utc) if start_us else None
    print(f"\n      start    {when.isoformat() if when else '(clock not set)'}")
    print(f"      rate     {rate:.3f} Hz   samples {count}   duration {count / rate:.3f} s" if rate else "      rate     0")
    print(f"      size     {len(data)} bytes{' (compressed)' if flags & FLAG_COMPRESSED else ''}")
    print(f"      atten    {atten / 10:g} dB")

    samples = struct.unpack(f"<{len(payload) // 2}H", payload[: len(payload) // 2 * 2])
    if not samples or not rate:
        sys.exit(0 if ok else 1)
    lo, hi = min(samples), max(samples)
    print(f"      signal   min {lo}  mean {sum(samples) / len(samples):.1f}  max {hi}  (12-bit: 0–4095)")
    check(hi <= 4095, "values fit 12 bits")
    if hi >= 4095:
        print("      note     samples at 4095 — the ADC is clipping (see BRINGUP.md §2.1)")

    threshold = args.threshold if args.threshold is not None else (lo + hi) // 2
    pulses = find_pulses(samples, threshold)
    ms = 1000 / rate
    print(f"\n      pulses   {len(pulses)} above {threshold}")
    if pulses:
        widths = [(e - s) * ms for s, e in pulses]
        print(f"      width ms   {describe(widths)}")
    if len(pulses) > 1:
        spacing = [(b[0] - a[0]) * ms for a, b in zip(pulses, pulses[1:])]
        print(f"      period ms  {describe(spacing)}")
        if args.period_ms:
            worst = max(abs(s - args.period_ms) for s in spacing)
            check(worst <= ms + 1e-9, f"every period within one sample ({ms:.3f} ms) of {args.period_ms} ms (worst {worst:.3f} ms)")
    elif args.period_ms:
        check(False, "need at least two pulses to check the period")

    if args.list_pulses:
        for s, e in pulses:
            print(f"      {s * ms:12.3f} ms  width {(e - s) * ms:8.3f} ms")

    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
