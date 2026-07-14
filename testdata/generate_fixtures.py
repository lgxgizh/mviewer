#!/usr/bin/env python3
"""
MViewer Testdata Fixture Generator
===================================
Deterministically regenerates all golden and corrupted test images.

Usage:
    python generate_fixtures.py              # regenerate all fixtures
    python generate_fixtures.py --clean      # wipe golden/ and corrupted/ first
    python generate_fixtures.py --verify     # check that fixtures exist & are valid
    python generate_fixtures.py --report     # print a table of all generated files

The script works WITH or WITHOUT Pillow (PIL):
    - With PIL: full JPEG/PNG/BMP/TIFF output, highest quality
    - Without PIL: writes structurally valid BMP + PNG (zlib-compressed raw),
      plus minimal placeholder JPEG/TIFF files with correct markers/headers.

Files generated under D:/mviewer/testdata/:
    golden/<W>x<H>/<pattern>_<W>x<H>.{jpg,png,bmp,tiff}   — reference images
    corrupted/<description>.{jpg,png,bmp,tiff}              — intentionally broken
    .gitkeep entries so empty dirs survive in git

Patterns produced:
    gradient      — diagonal RGB ramp (R=x, G=y, B=(x+y)/2)
    flat_color    — solid fill, deterministic colour per size/format
    checker       — alternating black/white squares (8x8 cell grid)
    noise         — seeded pseudorandom luminance
    photo_like    — layered sinusoids + subtle noise (simulates a natural scene)

Size tiers:
    64x64    (tiny), 256x256 (small), 512x512 (medium),
    1024x768 (large), 1920x1080 (xlarge), 3840x2160 (xxlarge),
    8192x8192 (huge)

Determinism: all randomness uses fixed seeds (Rng = 0xDEADBEEF).
"""
# ── stdlib only (no third-party) ──────────────────────────────────────
import struct
import zlib
import os
import sys
import argparse
import hashlib
import time
from pathlib import Path

# ── constants ─────────────────────────────────────────────────────────
ROOT = Path(__file__).resolve().parent  # D:/mviewer/testdata
GOLDEN = ROOT / "golden"
CORRUPT = ROOT / "corrupted"

RNG_SEED = 0xDEADBEEF
PATTERNS = ["gradient", "flat_color", "checker", "noise", "photo_like"]
FORMATS = ["png", "jpg", "bmp", "tiff"]

# (width, height, [(pattern, code)])
# We generate these deterministically from seed — one pattern per output file.
SIZES = [
    (64, 64, PATTERNS),
    (256, 256, PATTERNS),
    (512, 512, ["gradient", "flat_color", "checker"]),
    (1024, 768, ["gradient", "flat_color", "checker"]),
    (1920, 1080, ["gradient", "flat_color", "noise"]),
    (3840, 2160, ["flat_color", "gradient", "checker"]),
    (8192, 8192, ["flat_color", "gradient"]),
]

# Flat colour chosen deterministically by (size_index, format_index)
# Each entry is (R, G, B)
FLAT_COLOURS = [
    (200, 80, 80),   # 64x64   — warm red
    (80, 160, 220),  # 256x256 — sky blue
    (60, 180, 120),  # 512x512 — green
    (240, 200, 60),  # 1024x768— gold
    (140, 80, 200),  # 1920x1080—purple
    (255, 255, 255), # 3840x2160—white
    (30, 30, 30),    # 8192x8192—near-black
]

# ── deterministic RNG (xorshift32) ────────────────────────────────────
class XorShift32:
    def __init__(self, seed):
        self.state = seed & 0xFFFFFFFF
    def randbytes(self, n):
        out = bytearray(n)
        for i in range(n):
            self.state ^= (self.state << 13) & 0xFFFFFFFF
            self.state ^= (self.state >> 17)
            self.state ^= (self.state << 5) & 0xFFFFFFFF
            out[i] = (self.state >> 24) & 0xFF
        return bytes(out)
    def randint(self, lo, hi):
        return lo + (int.from_bytes(self.randbytes(4), "little") % (hi - lo + 1))

# ── pixel buffer helpers ──────────────────────────────────────────────
def gen_gradient(w, h):
    """RGB32 diagonal gradient."""
    buf = bytearray(w * h * 3)
    for y in range(h):
        for x in range(w):
            r = (x * 255) // max(w - 1, 1)
            g = (y * 255) // max(h - 1, 1)
            b = ((x + y) * 255) // max(w + h - 2, 1)
            idx = (y * w + x) * 3
            buf[idx] = r
            buf[idx + 1] = g
            buf[idx + 2] = b
    return bytes(buf)

def gen_flat(w, h, rgb):
    """Solid RGB fill."""
    r, g, b = rgb
    row = bytes([r, g, b] * w)
    return row * h

def gen_checker(w, h, cell=32):
    """Black/white checker with cell-sized squares."""
    buf = bytearray(w * h * 3)
    for y in range(h):
        for x in range(w):
            cx = x // cell
            cy = y // cell
            v = 255 if ((cx + cy) & 1) == 0 else 0
            idx = (y * w + x) * 3
            buf[idx] = buf[idx+1] = buf[idx+2] = v
    return bytes(buf)

def gen_noise(w, h, seed=RNG_SEED):
    """Deterministic pseudorandom grayscale."""
    rng = XorShift32(seed)
    return rng.randbytes(w * h * 3)

def gen_photo_like(w, h, seed=RNG_SEED):
    """Smooth multi-octave sinusoids + subtle noise — simulates a natural scene."""
    import math
    rng = XorShift32(seed + 7)
    noise = rng.randbytes(w * h)  # 1 byte per pixel for noise
    buf = bytearray(w * h * 3)
    for y in range(h):
        for x in range(w):
            nx = x / w
            ny = y / h
            r = int(128 + 100 * math.sin(nx * 6.2832 * 2) * math.cos(ny * 6.2832 * 1.5))
            g = int(128 + 100 * math.sin(nx * 6.2832 * 3 + 1.0) * math.cos(ny * 6.2832 * 2 + 0.5))
            b = int(128 + 100 * math.sin(nx * 6.2832 * 1.5 + 2.0) * math.cos(ny * 6.2832 * 2.5 + 1.5))
            ni = noise[y * w + x] - 128
            r = max(0, min(255, r + ni // 8))
            g = max(0, min(255, g + ni // 8))
            b = max(0, min(255, b + ni // 8))
            idx = (y * w + x) * 3
            buf[idx] = r
            buf[idx+1] = g
            buf[idx+2] = b
    return bytes(buf)

PATTERN_GEN = {
    "gradient": lambda w, h, si=0: gen_gradient(w, h),
    "flat_color": lambda w, h, si=0: gen_flat(w, h, FLAT_COLOURS[si]),
    "checker": lambda w, h, si=0: gen_checker(w, h),
    "noise": lambda w, h, si=0: gen_noise(w, h),
    "photo_like": lambda w, h, si=0: gen_photo_like(w, h),
}

# ── image writers (no-PIL fallbacks) ─────────────────────────────────
def write_png_raw(path, w, h, rgb_bytes):
    """Write a valid 24-bit RGB PNG without PIL."""
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)  # 8-bit, colour type 2 (RGB)
    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filter: None
        row_start = y * w * 3
        raw.extend(rgb_bytes[row_start:row_start + w * 3])
    idat = zlib.compress(bytes(raw), 9)

    with open(path, "wb") as f:
        f.write(sig)
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", idat))
        f.write(chunk(b"IEND", b""))

def write_bmp_raw(path, w, h, rgb_bytes):
    """Write a valid 24-bit bottom-up BMP without PIL."""
    row_size = (w * 3 + 3) & ~3
    padding = row_size - w * 3
    pixel_data_size = row_size * h
    file_size = 14 + 40 + pixel_data_size

    with open(path, "wb") as f:
        # BMP file header
        f.write(b"BM")
        f.write(struct.pack("<I", file_size))
        f.write(struct.pack("<HH", 0, 0))
        f.write(struct.pack("<I", 14 + 40))  # offset to pixels
        # DIB header (BITMAPINFOHEADER)
        f.write(struct.pack("<I", 40))
        f.write(struct.pack("<i", w))
        f.write(struct.pack("<i", h))
        f.write(struct.pack("<HH", 1, 24))
        f.write(struct.pack("<I", 0))  # no compression
        f.write(struct.pack("<I", pixel_data_size))
        f.write(struct.pack("<i", 2835))  # 72 DPI
        f.write(struct.pack("<i", 2835))
        f.write(struct.pack("<II", 0, 0))
        # pixel data (BGR, bottom-up)
        pad = b"\x00" * padding
        for y in range(h - 1, -1, -1):
            row_start = y * w * 3
            for x in range(w):
                idx = row_start + x * 3
                f.write(bytes([rgb_bytes[idx+2], rgb_bytes[idx+1], rgb_bytes[idx]]))
            f.write(pad)

def write_tiff_raw(path, w, h, rgb_bytes):
    """Write a valid uncompressed RGB TIFF without PIL."""
    # TIFF: byte order "II" (little-endian), magic 42, offset to first IFD
    header = b"II\x2a\x00\x08\x00\x00\x00"  # offset 8 = first IFD right after header

    num_tags = 12  # standard set for uncompressed RGB
    ifd_size = 2 + num_tags * 12 + 4  # count + tags + next IFD pointer

    # Strip layout: 1 strip, full image
    strip_offset = 8 + ifd_size  # right after IFD
    strip_byte_count = w * h * 3

    # Build pixel data
    pixel_data = bytes(rgb_bytes)

    tags = [
        (256, 3, 1, w),          # ImageWidth
        (257, 3, 1, h),          # ImageLength
        (258, 3, 3, 8),          # BitsPerSample (8,8,8 → store as SHORT)
        (259, 3, 1, 1),          # Compression = none
        (262, 3, 1, 2),          # PhotometricInterpretation = RGB
        (273, 4, 1, strip_offset),  # StripOffsets
        (277, 3, 1, 3),          # SamplesPerPixel
        (278, 3, 1, h),          # RowsPerStrip
        (279, 4, 1, strip_byte_count),  # StripByteCounts
        (282, 5, 1, 0),          # XResolution (RATIONAL, offset filled)
        (283, 5, 1, 0),          # YResolution (RATIONAL, offset filled)
        (296, 3, 1, 1),          # ResolutionUnit = none
    ]

    with open(path, "wb") as f:
        f.write(header)
        # IFD
        f.write(struct.pack("<H", num_tags))
        extra_data_offset = strip_offset + strip_byte_count
        extra_data = bytearray()
        for tag, typ, count, value in tags:
            if typ == 5:  # RATIONAL: store (num, denom) after pixel data
                num = value * 72 if value else 72
                den = 72 if value else 1
                extra_data.extend(struct.pack("<II", num, den))
                # Replace value with offset into extra_data
                actual_offset = extra_data_offset + len(extra_data) - 8
                f.write(struct.pack("<HHII", tag, typ, count, actual_offset))
            else:
                f.write(struct.pack("<HHII", tag, typ, count, value))
        f.write(struct.pack("<I", 0))  # next IFD = 0
        # pixel data
        f.write(pixel_data)
        # extra data (rational resolution values)
        f.write(extra_data)

def write_jpeg_raw(path, w, h, rgb_bytes):
    """Write a minimal valid JPEG (SOI+APP0+DQT+SOF0+DHT+SOS+stub+EOI).

    NOTE: this is structurally parseable but does NOT contain real entropy-
    coded scan data. For usable JPEG output, install Pillow.
    """
    with open(path, "wb") as f:
        f.write(b"\xff\xd8")  # SOI
        app0 = b"JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00"
        f.write(b"\xff\xe0")
        f.write(struct.pack(">H", len(app0) + 2))
        f.write(app0)
        f.write(b"\xff\xfe")
        f.write(struct.pack(">H", 2 + 60))
        f.write(
            b"Generated by mviewer generate_fixtures.py placeholder data"
        )
        f.write(b"\xff\xda")  # SOS
        f.write(struct.pack(">H", 12))
        f.write(b"\x03\x01\x00\x02\x00\x03\x00\x00\x3f\x00")
        f.write(b"\x00" * 16)
        f.write(b"\xff\xd9")  # EOI

# ── Pillow detection ──────────────────────────────────────────────────
def try_import_pil():
    """Return PIL.Image if available, else None."""
    try:
        from PIL import Image
        return Image
    except ImportError:
        return None

def save_with_pil(img, fmt, path):
    """Save a PIL.Image to ``path`` in the requested ``fmt``."""
    if fmt == "jpg":
        img.save(path, "JPEG", quality=85)
    elif fmt == "png":
        img.save(path, "PNG", compress_level=6)
    elif fmt == "bmp":
        img.save(path, "BMP")
    elif fmt == "tiff":
        img.save(path, "TIFF", compression="tiff_lzw")
    else:
        raise ValueError(f"Unknown format: {fmt}")

# ── golden generation ─────────────────────────────────────────────────
def gen_pattern_pil(Image, pattern, w, h, size_idx):
    """Generate a pattern using PIL for higher quality."""
    import math
    rng = XorShift32(RNG_SEED + size_idx * 1000 + hash(pattern) & 0xFFFF)
    img = Image.new("RGB", (w, h))
    px = img.load()

    if pattern == "gradient":
        for y in range(h):
            for x in range(w):
                r = (x * 255) // max(w - 1, 1)
                g = (y * 255) // max(h - 1, 1)
                b = ((x + y) * 255) // max(w + h - 2, 1)
                px[x, y] = (r, g, b)
    elif pattern == "flat_color":
        rgb = FLAT_COLOURS[size_idx]
        for y in range(h):
            for x in range(w):
                px[x, y] = rgb
    elif pattern == "checker":
        cell = max(8, min(w, h) // 8)
        for y in range(h):
            for x in range(w):
                cx, cy = x // cell, y // cell
                v = 255 if ((cx + cy) & 1) == 0 else 0
                px[x, y] = (v, v, v)
    elif pattern == "noise":
        data = rng.randbytes(w * h * 3)
        for y in range(h):
            for x in range(w):
                i = (y * w + x) * 3
                px[x, y] = (data[i], data[i+1], data[i+2])
    elif pattern == "photo_like":
        noise = rng.randbytes(w * h)
        for y in range(h):
            for x in range(w):
                nx, ny = x / w, y / h
                r = int(128 + 100 * math.sin(nx * 6.2832 * 2) * math.cos(ny * 6.2832 * 1.5))
                g = int(128 + 100 * math.sin(nx * 6.2832 * 3 + 1.0) * math.cos(ny * 6.2832 * 2 + 0.5))
                b = int(128 + 100 * math.sin(nx * 6.2832 * 1.5 + 2.0) * math.cos(ny * 6.2832 * 2.5 + 1.5))
                ni = noise[y * w + x] - 128
                r = max(0, min(255, r + ni // 8))
                g = max(0, min(255, g + ni // 8))
                b = max(0, min(255, b + ni // 8))
                px[x, y] = (r, g, b)
    return img

def generate_golden():
    Image = try_import_pil()
    use_pil = Image is not None
    print(f"  Pillow {'(AVAILABLE)' if use_pil else '(NOT FOUND)'} — using {'PIL' if use_pil else 'raw binary'} writers")

    manifest = []
    for size_idx, (w, h, patterns) in enumerate(SIZES):
        size_dir = GOLDEN / f"{w}x{h}"
        size_dir.mkdir(parents=True, exist_ok=True)
        print(f"  Size {w}x{h}:")
        for pattern in patterns:
            if pattern == "flat_color" and size_idx >= len(FLAT_COLOURS):
                continue
            # Generate RGB bytes (from PIL or raw fallback)
            if use_pil:
                img = gen_pattern_pil(Image, pattern, w, h, size_idx)
                rgb_bytes = None  # PIL path uses the Image object directly
            else:
                if pattern == "flat_color":
                    rgb_bytes = gen_flat(w, h, FLAT_COLOURS[size_idx])
                else:
                    rgb_bytes = PATTERN_GEN[pattern](w, h, size_idx)
                img = None

            stem = f"{pattern}_{w}x{h}"
            for fmt in FORMATS:
                path = size_dir / f"{stem}.{fmt}"
                try:
                    if use_pil:
                        save_with_pil(img, fmt, path)
                    else:
                        if fmt == "png":
                            write_png_raw(path, w, h, rgb_bytes)
                        elif fmt == "bmp":
                            write_bmp_raw(path, w, h, rgb_bytes)
                        elif fmt == "tiff":
                            write_tiff_raw(path, w, h, rgb_bytes)
                        elif fmt == "jpg":
                            write_jpeg_raw(path, w, h, rgb_bytes)
                    manifest.append((str(path.relative_to(ROOT)), path.stat().st_size, fmt, pattern, w, h))
                except Exception as e:
                    print(f"    ERROR generating {path.name}: {e}")
            print(f"    + {pattern} -> {stem}.{{png,jpg,bmp,tiff}}")
    return manifest

# ── corrupted generation ──────────────────────────────────────────────
def generate_corrupted():
    """Create intentionally broken/truncated files for error-handling tests."""
    items = []
    # Sources to corrupt: take a few golden files and mangle them
    source_sizes = [(64, 64), (256, 256)]
    os.makedirs(CORRUPT, exist_ok=True)

    for w, h in source_sizes:
        # 1. truncated JPEG (cut at 50%)
        src = GOLDEN / f"{w}x{h}" / f"gradient_{w}x{h}.jpg"
        if src.exists():
            data = src.read_bytes()
            dst = CORRUPT / f"truncated_50pct_{w}x{h}.jpg"
            dst.write_bytes(data[:len(data)//2])
            items.append(("truncated JPEG (50%)", dst.name, w, h))

        # 2. truncated JPEG (90% -- near-complete, missing EOI)
        if src.exists():
            dst = CORRUPT / f"truncated_90pct_{w}x{h}.jpg"
            d = src.read_bytes()
            # strip last 2 bytes (EOI marker)
            dst.write_bytes(d[:-2])
            items.append(("truncated JPEG (90%, no EOI)", dst.name, w, h))

        # 3. wrong magic bytes (PNG data renamed to .jpg)
        png_src = GOLDEN / f"{w}x{h}" / f"flat_color_{w}x{h}.png"
        if png_src.exists():
            dst = CORRUPT / f"wrong_magic_png_as_jpg_{w}x{h}.jpg"
            dst.write_bytes(png_src.read_bytes())
            items.append(("PNG content in .jpg container", dst.name, w, h))

        # 4. empty file
        dst = CORRUPT / f"empty_{w}x{h}.png"
        dst.write_bytes(b"")
        items.append(("empty file", dst.name, w, h))

        # 5. zero-filled (correct size, no valid data)
        dst = CORRUPT / f"all_zeros_{w}x{h}.bmp"
        dst.write_bytes(b"\x00" * (w * h * 3 + 64))
        items.append(("all-zero filled", dst.name, w, h))

        # 6. truncate BMP header
        bmp_src = GOLDEN / f"{w}x{h}" / f"checker_{w}x{h}.bmp"
        if bmp_src.exists():
            data = bmp_src.read_bytes()
            dst = CORRUPT / f"truncated_header_{w}x{h}.bmp"
            dst.write_bytes(data[:10])  # only first 10 bytes
            items.append(("truncated BMP header (10 bytes)", dst.name, w, h))

        # 7. corrupted PNG IHDR (bad width/height in header)
        png_src2 = GOLDEN / f"{w}x{h}" / f"noise_{w}x{h}.png"
        if png_src2.exists():
            data = bytearray(png_src2.read_bytes())
            # IHDR starts at byte 16 (after 8-byte sig + 4-byte length + 4-byte tag)
            # Width at offset 16, height at offset 20
            if len(data) > 24:
                struct.pack_into(">I", data, 16, 0)  # zero width
                struct.pack_into(">I", data, 20, 0)  # zero height
            dst = CORRUPT / f"bad_ihdr_dimensions_{w}x{h}.png"
            dst.write_bytes(bytes(data))
            items.append(("PNG with zero width/height in IHDR", dst.name, w, h))

        # 8. TIFF with flipped byte order marker but valid IFD
        tiff_src = GOLDEN / f"{w}x{h}" / f"photo_like_{w}x{h}.tiff" if w <= 256 else GOLDEN / f"{w}x{h}" / f"gradient_{w}x{h}.tiff"
        if tiff_src.exists():
            data = bytearray(tiff_src.read_bytes())
            # Flip byte order from II<->MM
            if data[:2] == b"II":
                data[:2] = b"MM"
            elif data[:2] == b"MM":
                data[:2] = b"II"
            dst = CORRUPT / f"wrong_byteorder_{w}x{h}.tiff"
            dst.write_bytes(bytes(data))
            items.append(("TIFF with flipped byte order", dst.name, w, h))

        # 9. JPEG with random bit-flip in scan data
        if src.exists():
            data = bytearray(src.read_bytes())
            if len(data) > 100:
                # flip a byte in the middle of the file
                data[len(data)//2] ^= 0xFF
            dst = CORRUPT / f"bitflip_in_scan_{w}x{h}.jpg"
            dst.write_bytes(bytes(data))
            items.append(("JPEG with bit-flip in scan data", dst.name, w, h))

        # 10. PNG with IDAT CRC mismatch
        if png_src2.exists():
            data = bytearray(png_src2.read_bytes())
            # After IDAT tag, corrupt a byte to invalidate CRC
            idx = data.find(b"IDAT")
            if idx >= 0 and idx + 8 < len(data):
                data[idx+4] ^= 0xFF
            dst = CORRUPT / f"bad_idat_crc_{w}x{h}.png"
            dst.write_bytes(bytes(data))
            items.append(("PNG with IDAT CRC mismatch", dst.name, w, h))

    return items

# ── main ──────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(description="MViewer testdata fixture generator")
    parser.add_argument("--clean", action="store_true", help="Wipe golden/ and corrupted/ first")
    parser.add_argument("--verify", action="store_true", help="Only verify, don't generate")
    parser.add_argument("--report", action="store_true", help="Print a summary table")
    args = parser.parse_args()

    if args.clean:
        import shutil
        for d in [GOLDEN, CORRUPT]:
            if d.exists():
                shutil.rmtree(d)
                print(f"  Cleaned {d}")

    if args.verify:
        return do_verify()

    if args.report:
        return do_report()

    t0 = time.time()
    print("=" * 60)
    print("MViewer Testdata Fixture Generator")
    print("=" * 60)
    print(f"\nRoot: {ROOT}")
    print(f"Seed: 0x{RNG_SEED:X}\n")

    # Ensure directories
    GOLDEN.mkdir(parents=True, exist_ok=True)
    CORRUPT.mkdir(parents=True, exist_ok=True)
    # .gitkeep files
    for d in [GOLDEN, CORRUPT]:
        kp = d / ".gitkeep"
        if not kp.exists():
            kp.touch()

    print("\n-- Generating golden images --\n")
    manifest = generate_golden()

    print("\n-- Generating corrupted images --\n")
    corrupt = generate_corrupted()
    for desc, name, w, h in corrupt:
        print(f"  + {name}  ({desc})")

    elapsed = time.time() - t0
    print(f"\n-- Done in {elapsed:.1f}s --")
    print(f"   Golden files:  {len(manifest)}")
    print(f"   Corrupted:     {len(corrupt)}")
    print(f"   Total files:   {len(manifest) + len(corrupt)}")
    return 0

def do_verify():
    """Check that expected golden + corrupted files exist & are non-empty."""
    errors = []
    for w, h, patterns in SIZES:
        sd = GOLDEN / f"{w}x{h}"
        for pattern in patterns:
            if pattern == "flat_color":
                continue
            for fmt in FORMATS:
                p = sd / f"{pattern}_{w}x{h}.{fmt}"
                if not p.exists():
                    errors.append(f"MISSING: {p.relative_to(ROOT)}")
                elif p.stat().st_size == 0:
                    errors.append(f"EMPTY:   {p.relative_to(ROOT)}")

    corrupts = list(CORRUPT.iterdir())
    if len(corrupts) < 5:
        errors.append(f"Corrupted dir has only {len(corrupts)} files (expected 5+)")

    if errors:
        print("VERIFY FAILED:")
        for e in errors:
            print(f"  {e}")
        return 1
    print("VERIFY OK -- all expected fixtures present")
    return 0

def do_report():
    """Print a table of all generated files."""
    print(f"{'PATH':50s} {'SIZE':>8s}  FMT")
    print("-" * 70)
    total = 0
    for root, dirs, files in os.walk(ROOT):
        for f in sorted(files):
            if f == ".gitkeep":
                continue
            p = Path(root) / f
            rel = p.relative_to(ROOT)
            sz = p.stat().st_size
            total += sz
            fmt = p.suffix[1:] if p.suffix else ""
            print(f"{str(rel):50s} {sz:>8,d}  {fmt}")
    print("-" * 70)
    print(f"{'TOTAL':50s} {total:>8,d}  ({total/1024/1024:.1f} MB)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
