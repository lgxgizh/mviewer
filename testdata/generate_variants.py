#!/usr/bin/env python3
"""
MViewer Phase-4 variant fixture generator (M13 Phase 4).

Extends the base fixtures in generate_fixtures.py with the FORMAT / INTEGRITY
variants the engineering review's Phase 4 calls for and that MViewer must open
(or gracefully skip) without crashing:

    testdata/variants/
        16bit_tiff.tif     real 16-bit grayscale TIFF
        gray.png           single-channel grayscale PNG
        rgba.png           RGBA PNG (alpha)
        cmyk.tif           CMYK TIFF (4 samples)
        bad_exif.jpg       valid JPEG + malformed APP1/EXIF
        bad_icc.png        PNG with a garbage iCCP profile chunk

These are REAL files (valid container structures), not decoder-shortcut stubs,
so they exercise the actual decode / metadata paths. The acceptance test
(test_assets_acceptance) opens every file and asserts No Crash + (decode OR
graceful skip).

Stdlib only (no Pillow) so it runs on the CI/build box.

Usage:
    python generate_variants.py            # generate variants/
    python generate_variants.py --clean     # wipe variants/ first
    python generate_variants.py --verify    # check presence
"""
import struct
import zlib
import os
import sys
import argparse
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
VARIANTS = ROOT / "variants"

RNG_SEED = 0xC0FFEE

def xorshift32(seed):
    state = seed & 0xFFFFFFFF
    out = bytearray()
    while True:
        state ^= (state << 13) & 0xFFFFFFFF
        state ^= (state >> 17)
        state ^= (state << 5) & 0xFFFFFFFF
        yield (state >> 24) & 0xFF

def gen_gray_bytes(w, h, seed=RNG_SEED):
    gen = xorshift32(seed)
    return bytes([next(gen) for _ in range(w * h)])

def gen_rgb_bytes(w, h, seed=RNG_SEED):
    gen = xorshift32(seed)
    return bytes([next(gen) for _ in range(w * h * 3)])

# ── PNG writers ────────────────────────────────────────────────────────
def png_chunk(tag, data):
    c = tag + data
    return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

def write_png_gray(path, w, h, gray):
    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0)  # 8-bit grayscale
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw.extend(gray[y * w:(y + 1) * w])
    idat = zlib.compress(bytes(raw), 9)
    with open(path, "wb") as f:
        f.write(sig + png_chunk(b"IHDR", ihdr) + png_chunk(b"IDAT", idat) + png_chunk(b"IEND", b""))

def write_png_rgba(path, w, h, rgba):
    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)  # 8-bit RGBA
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw.extend(rgba[y * w * 4:(y + 1) * w * 4])
    idat = zlib.compress(bytes(raw), 9)
    with open(path, "wb") as f:
        f.write(sig + png_chunk(b"IHDR", ihdr) + png_chunk(b"IDAT", idat) + png_chunk(b"IEND", b""))

def write_png_badicc(path, w, h, rgb):
    """Valid RGB PNG with a garbage iCCP chunk (invalid ICC profile)."""
    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw.extend(rgb[y * w * 3:(y + 1) * w * 3])
    idat = zlib.compress(bytes(raw), 9)
    # iCCP: name + nul + compression(0) + zlib-compressed garbage (not a real profile)
    garbage = zlib.compress(b"not-a-real-icc-profile-" * 8, 9)
    iccp = b"mviewer\x00" + bytes([0]) + garbage
    with open(path, "wb") as f:
        f.write(sig + png_chunk(b"IHDR", ihdr) + png_chunk(b"iCCP", iccp)
                + png_chunk(b"IDAT", idat) + png_chunk(b"IEND", b""))

# ── TIFF writers ──────────────────────────────────────────────────────
def tiff_header():
    return b"II\x2a\x00\x08\x00\x00\x00"  # little-endian, magic 42, IFD at 8

def write_tiff_16bit_gray(path, w, h, samples16):
    """Real 16-bit grayscale TIFF (BitsPerSample=16, 1 sample)."""
    header = tiff_header()
    num_tags = 12
    ifd_size = 2 + num_tags * 12 + 4
    strip_offset = 8 + ifd_size
    strip_byte_count = w * h * 2
    pixel_data = b"".join(struct.pack("<H", v) for v in samples16)
    tags = [
        (256, 3, 1, w), (257, 3, 1, h), (258, 3, 1, 16), (259, 3, 1, 1),
        (262, 3, 1, 1), (273, 4, 1, strip_offset), (277, 3, 1, 1),
        (278, 3, 1, h), (279, 4, 1, strip_byte_count),
        (282, 5, 1, 0), (283, 5, 1, 0), (296, 3, 1, 1),
    ]
    with open(path, "wb") as f:
        f.write(header)
        f.write(struct.pack("<H", num_tags))
        extra = bytearray()
        extra_offset = strip_offset + strip_byte_count
        for tag, typ, count, value in tags:
            if typ == 5:
                num = value * 72 if value else 72
                den = 72 if value else 1
                extra.extend(struct.pack("<II", num, den))
                off = extra_offset + len(extra) - 8
                f.write(struct.pack("<HHII", tag, typ, count, off))
            else:
                f.write(struct.pack("<HHII", tag, typ, count, value))
        f.write(struct.pack("<I", 0))
        f.write(pixel_data)
        f.write(extra)

def write_tiff_cmyk(path, w, h, cmyk):
    """Real CMYK TIFF (PhotometricInterpretation=5, 4 samples, 8-bit)."""
    header = tiff_header()
    num_tags = 12
    ifd_size = 2 + num_tags * 12 + 4
    strip_offset = 8 + ifd_size
    strip_byte_count = w * h * 4
    pixel_data = bytes(cmyk)
    # BitsPerSample for CMYK = 4 SHORTs (8,8,8,8), stored inline after the IFD.
    # Reserve its slot in the extra block up front so the offset is stable.
    extra_offset = strip_offset + strip_byte_count
    bits_offset = extra_offset          # first 8 bytes of extra = the 4 SHORTs
    extra = bytearray(struct.pack("<HHHH", 8, 8, 8, 8))
    tags = [
        (256, 3, 1, w), (257, 3, 1, h), (258, 3, 4, bits_offset),
        (259, 3, 1, 1), (262, 3, 1, 5), (273, 4, 1, strip_offset),
        (277, 3, 1, 4), (278, 3, 1, h), (279, 4, 1, strip_byte_count),
        (282, 5, 1, 0), (283, 5, 1, 0), (296, 3, 1, 1),
    ]
    with open(path, "wb") as f:
        f.write(header)
        f.write(struct.pack("<H", num_tags))
        for tag, typ, count, value in tags:
            if typ == 5:
                num = value * 72 if value else 72
                den = 72 if value else 1
                extra.extend(struct.pack("<II", num, den))
                off = extra_offset + len(extra) - 8
                f.write(struct.pack("<HHII", tag, typ, count, off))
            else:
                f.write(struct.pack("<HHII", tag, typ, count, value))
        f.write(struct.pack("<I", 0))
        f.write(pixel_data)
        f.write(extra)

# ── JPEG with bad EXIF ─────────────────────────────────────────────────
def write_jpeg_bad_exif(path, w, h, rgb):
    """Valid baseline JPEG structure + a malformed APP1/EXIF segment.
    The pixel scan is a stub (decode may fail), but the EXIF TLV is intentionally
    truncated to exercise MViewer's EXIF-error handling without crashing."""
    # Build a minimal valid JPEG container (SOI..EOI) with a bad APP1.
    with open(path, "wb") as f:
        f.write(b"\xff\xd8")  # SOI
        # APP1 with "Exif\x00\x00" then truncated EXIF (garbage, no valid IFD)
        app1_body = b"Exif\x00\x00" + b"\x00" * 4 + b"\x1b\x00"  # claims TTF/II but no IFD
        f.write(b"\xff\xe1")
        f.write(struct.pack(">H", len(app1_body) + 2))
        f.write(app1_body)
        # Minimal SOF0 + SOS + EOI shell (no real scan — decoder should skip gracefully)
        f.write(b"\xff\xc0\x00\x11\x08" + struct.pack(">H", h) + struct.pack(">H", w)
                + b"\x03\x01\x11\x00\x02\x11\x01\x03\x11\x01")
        f.write(b"\xff\xda\x00\x0c\x03\x01\x00\x02\x00\x03\x00\x00\x3f\x00")
        f.write(b"\x00" * 8)
        f.write(b"\xff\xd9")  # EOI

def generate_variants():
    VARIANTS.mkdir(parents=True, exist_ok=True)
    files = []
    # small-ish dimensions to keep generation fast
    W, H = 256, 256
    gray = gen_gray_bytes(W, H)
    rgb = gen_rgb_bytes(W, H)
    rgba = bytearray()
    g2 = xorshift32(RNG_SEED + 1)
    for i in range(W * H):
        r = next(g2); g = next(g2); b = next(g2); a = 200 if (i % 2 == 0) else 80
        rgba.extend([r, g, b, a])
    cmyk = bytearray()
    g3 = xorshift32(RNG_SEED + 2)
    for i in range(W * H):
        cmyk.extend([next(g3), next(g3), next(g3), next(g3)])

    write_png_gray(VARIANTS / "gray.png", W, H, gray)
    files.append("gray.png")
    write_png_rgba(VARIANTS / "rgba.png", W, H, bytes(rgba))
    files.append("rgba.png")
    write_png_badicc(VARIANTS / "bad_icc.png", W, H, rgb)
    files.append("bad_icc.png")

    # 16-bit grayscale samples (0..65535)
    g4 = xorshift32(RNG_SEED + 3)
    samples16 = [(next(g4) << 8) | next(g4) for _ in range(W * H)]
    write_tiff_16bit_gray(VARIANTS / "16bit_tiff.tif", W, H, samples16)
    files.append("16bit_tiff.tif")

    write_tiff_cmyk(VARIANTS / "cmyk.tif", W, H, cmyk)
    files.append("cmyk.tif")

    write_jpeg_bad_exif(VARIANTS / "bad_exif.jpg", W, H, rgb)
    files.append("bad_exif.jpg")

    return files

def do_verify():
    expected = ["gray.png", "rgba.png", "bad_icc.png", "16bit_tiff.tif", "cmyk.tif", "bad_exif.jpg"]
    errors = []
    for name in expected:
        p = VARIANTS / name
        if not p.exists():
            errors.append(f"MISSING: {name}")
        elif p.stat().st_size == 0:
            errors.append(f"EMPTY: {name}")
    if errors:
        print("VERIFY FAILED:")
        for e in errors:
            print(f"  {e}")
        return 1
    print("VERIFY OK -- all variant fixtures present")
    return 0

def main():
    parser = argparse.ArgumentParser(description="MViewer Phase-4 variant fixture generator")
    parser.add_argument("--clean", action="store_true")
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    if args.clean and VARIANTS.exists():
        import shutil
        shutil.rmtree(VARIANTS)
        print(f"Cleaned {VARIANTS}")
    if args.verify:
        return do_verify()
    t0 = time.time()
    VARIANTS.mkdir(parents=True, exist_ok=True)
    (VARIANTS / ".gitkeep").touch()
    print("MViewer Phase-4 variant fixtures")
    print(f"Root: {VARIANTS}\n")
    files = generate_variants()
    for fn in files:
        sz = (VARIANTS / fn).stat().st_size
        print(f"  + variants/{fn}  ({sz} bytes)")
    print(f"\nDone in {time.time()-t0:.1f}s -- {len(files)} variant files")
    return 0

if __name__ == "__main__":
    sys.exit(main())
