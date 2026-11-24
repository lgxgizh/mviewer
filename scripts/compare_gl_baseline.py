#!/usr/bin/env python3
"""Compare a fresh compare-canvas capture against the committed baseline.

    python scripts/compare_gl_baseline.py <captured-dir> [--baseline tests/gl_baseline/dpr1]

Prints the per-image maximum channel difference and exits non-zero when any image
differs by more than --tolerance (default 2). Use it after changing the compare
canvas to prove the new path paints the same pixels as the CPU one.
"""
import argparse
import os
import struct
import sys
import zlib


def read_png(path):
    """Minimal PNG reader for 8-bit RGB/RGBA non-interlaced images."""
    with open(path, "rb") as fh:
        data = fh.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos = 8
    width = height = bitdepth = colourtype = None
    idat = bytearray()
    while pos + 8 <= len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        ctype = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if ctype == b"IHDR":
            width, height, bitdepth, colourtype = struct.unpack(">IIBB", body[:10])
        elif ctype == b"IDAT":
            idat += body
        elif ctype == b"IEND":
            break
        pos += 12 + length
    if bitdepth != 8 or colourtype not in (2, 6):
        raise ValueError(f"{path}: unsupported PNG (bitdepth={bitdepth} colour={colourtype})")
    channels = 3 if colourtype == 2 else 4
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    out = bytearray(height * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(height):
        filter_type = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if filter_type == 1:  # Sub
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif filter_type == 2:  # Up
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif filter_type == 3:  # Average
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif filter_type == 4:  # Paeth
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = prev[i]
                c = prev[i - channels] if i >= channels else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return width, height, channels, bytes(out)


def max_diff(a, b):
    if a[0] != b[0] or a[1] != b[1]:
        return None
    n = min(len(a[3]), len(b[3]))
    worst = 0
    for i in range(n):
        d = abs(a[3][i] - b[3][i])
        if d > worst:
            worst = d
    return worst


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("captured")
    ap.add_argument("--baseline", default="tests/gl_baseline/dpr1")
    ap.add_argument("--tolerance", type=int, default=2)
    args = ap.parse_args()

    names = ["split_0.png", "split_1.png", "swipe_0.png", "swipe_1.png",
             "overlay_0.png", "checker_0.png", "diff_0.png"]
    failed = False
    for name in names:
        cap_path = os.path.join(args.captured, name)
        base_path = os.path.join(args.baseline, name)
        if not os.path.exists(cap_path):
            print(f"MISSING {name}")
            failed = True
            continue
        if not os.path.exists(base_path):
            print(f"NO-BASELINE {name}")
            failed = True
            continue
        diff = max_diff(read_png(cap_path), read_png(base_path))
        if diff is None:
            print(f"SIZE-MISMATCH {name}")
            failed = True
            continue
        flag = "ok" if diff <= args.tolerance else "FAIL"
        if diff > args.tolerance:
            failed = True
        print(f"{flag:8s} {name:18s} max_channel_difference={diff}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
