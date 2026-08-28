#!/usr/bin/env python3
"""MViewer M47 deterministic LARGE-image fixture generator.

Phase 0 / baseline requirement: "建立 deterministic large-image
fixtures/generator". Generated products live under testdata/large/ which is
git-ignored (see .gitignore: testdata/* is ignored, ONLY the tracked generator
scripts are whitelisted), so a clean checkout / CI runner rebuilds the exact
corpus from this script instead of committing multi-MB binaries.

All images are DERIVED from a deterministic base pattern via NEAREST resize, so
pixel values are byte-identical on every machine/python/Pillow run. Generation
is idempotent: existing files are left untouched (fast), missing ones are
generated.

Fixture matrix (M47 Phase 0):
  large_jpeg_100mp.jpg    ~100 MP JPEG (12000x8333), quality 80
  large_tiff_100mp.tiff   ~100 MP TIFF (10000x10000), LZW-compressed
  high_compression.jpg    6000x4000 JPEG at quality 4 (heavy artifacts)
  exif_orientation6.jpg   4096x4096 JPEG with EXIF orientation 6 (rotate 90 CW)
  exif_orientation8.jpg   4096x4096 JPEG with EXIF orientation 8 (rotate 270 CW)
  exif_orientation6_wide.jpg 4000x2000 JPEG with EXIF orientation 6 (displayed 2000x4000)
  icc_adobe.jpg           2048x2048 JPEG with an embedded sRGB ICC profile (legacy)
  extreme_wide.jpg        20000x400 JPEG (extreme aspect, landscape)
  extreme_tall.jpg        400x20000 JPEG (extreme aspect, portrait)
  truncated_large.jpg     a valid 6000x4000 JPEG truncated at ~55% (corrupt input)
  truncated_large.tiff    a valid 4000x4000 TIFF truncated at ~50% (corrupt input)

M48 Phase 0 additions (real non-sRGB + orientation + deep-zoom):
  icc_adobe_12mp.jpg      4000x3000 JPEG embedding a CONSTRUCTED AdobeRGB1998
                          ICC (offline, parametric TRC) + a flat in-gamut patch
  icc_adobe_100mp.jpg     12000x8333 JPEG, same profile + patch
  exif_orient2..8_non_square.jpg  6000x4000 JPEGs, EXIF orientation 2..8
                          (raw pixels + corner markers, non-square so coordinate
                          confusion is detectable)
  deepzoom_hf_72mp.jpg    9000x8000 JPEG with 1px-period high-frequency detail
                          (deep-zoom density tests; ~53 MB)

M53 Phase 0 additions (large-source parity characterization):
  large_tiff_16bit.tiff   4096x4096 unsigned 16-bit grayscale TIFF
  large_png_16mp.png      4096x4096 PNG boundary fixture
  large_bmp_16mp.bmp      4096x4096 BMP boundary fixture

Usage:
  python testdata/generate_large_fixtures.py [--check] [--ensure] [--force]
    default    generate (idempotent) any missing fixture
    --check    validate required fixtures exist with correct dimensions/size
               (exit 1 on mismatch; writes nothing)
    --ensure   generate anything missing, then --check (CTest gate)
    --force    regenerate every fixture regardless of presence

Exit codes: 0 = ok, 1 = failure (missing Pillow, dimension/size mismatch, write failure).
"""

import argparse
import os
import sys

# Trusted local fixture generator: Pillow's decompression-bomb heuristic
# (89,478,485 px) would warn/raise on our genuine 100 MP fixtures. These files
# are produced by this script from a deterministic pattern, never untrusted.
from PIL import Image

Image.MAX_IMAGE_PIXELS = None

# relative path -> (width, height, min_bytes)
REQUIRED = {
    "large_jpeg_100mp.jpg": (12000, 8333, 1_000_000),
    "large_tiff_100mp.tiff": (10000, 10000, 1_000_000),
    "large_tiff_16bit.tiff": (4096, 4096, 100_000),
    "large_png_16mp.png": (4096, 4096, 100_000),
    "large_bmp_16mp.bmp": (4096, 4096, 100_000),
    "high_compression.jpg": (6000, 4000, 100_000),
    "exif_orientation6.jpg": (4096, 4096, 100_000),
    "exif_orientation8.jpg": (4096, 4096, 100_000),
    "exif_orientation6_wide.jpg": (4000, 2000, 100_000),
    "icc_adobe.jpg": (2048, 2048, 50_000),
    # M48 Phase 0: REAL non-sRGB fixtures (AdobeRGB1998 profile constructed
    # offline — never an sRGB profile wearing an AdobeRGB name), orientation
    # 2..8 NON-SQUARE with corner markers, and a >60MP 1px-period pattern.
    "icc_adobe_12mp.jpg": (4000, 3000, 200_000),
    "icc_adobe_100mp.jpg": (12000, 8333, 1_000_000),
    "exif_orient2_non_square.jpg": (6000, 4000, 100_000),
    "exif_orient3_non_square.jpg": (6000, 4000, 100_000),
    "exif_orient4_non_square.jpg": (6000, 4000, 100_000),
    "exif_orient5_non_square.jpg": (6000, 4000, 100_000),
    "exif_orient6_non_square.jpg": (6000, 4000, 100_000),
    "exif_orient7_non_square.jpg": (6000, 4000, 100_000),
    "exif_orient8_non_square.jpg": (6000, 4000, 100_000),
    "deepzoom_hf_72mp.jpg": (9000, 8000, 800_000),
    "extreme_wide.jpg": (20000, 400, 100_000),
    "extreme_tall.jpg": (400, 20000, 100_000),
    "truncated_large.jpg": (6000, 4000, 100_000),
    "truncated_large.tiff": (4000, 4000, 100_000),
}

# EXIF orientation tag constants (Pillow Image.Exif tag 274).
_ORIENTATION_TAG = 274


def build_adobe_rgb_1998_profile() -> bytes:
    """Deterministic AdobeRGB1998-compatible ICC profile (constructed, offline).

    Built from the documented AdobeRGB1998 primaries / D65 -> D50-adapted
    matrix columns and the 2.2 TRC — no external binary blob, no network. The
    canonical matrix values are the widely published D50-adapted ones used by
    every ICC implementation. lcms (Pillow/Qt) parses and transforms this
    profile; the test suite verifies the transform numerically.
    """
    import struct

    def s15f16(v: float) -> int:  # float -> signed 15.16 fixed point
        return int(round(v * 65536.0)) & 0xFFFFFFFF

    # AdobeRGB1998 (D65 primaries) -> D50-adapted XYZ matrix columns, and the
    # canonical Bradford D50 -> D65 adaptation (the inverse) used by real
    # profiles whose PCS is D50 while the primaries are D65-defined.
    rXYZ = (0.60974, 0.31111, 0.01947)
    gXYZ = (0.20528, 0.62567, 0.06087)
    bXYZ = (0.14919, 0.06322, 0.74457)
    wtpt = (0.9642, 1.0, 0.8249)
    d50_to_d65 = (1.04788, 0.02292, -0.0502, 0.0296, 0.9905, -0.0171, -0.0093, 0.0151, 0.7517)

    def xyz_tag(values) -> bytes:
        # TYPED v4 XYZType: 'XYZ ' + reserved + 3 x s15Fixed16 (20 bytes). Qt's
        # qicc rejects the classic 12-byte v2 form ("Undersized XYZ tag").
        return struct.pack(">4sI3I", b"XYZ ", 0, *(s15f16(v) for v in values))

    def chad_tag() -> bytes:
        # s15Fixed16ArrayType ('sf32') — Qt's qicc rejects any other type for
        # the chromatic-adaptation tag ("bad chad data type").
        out = struct.pack(">4sI", b"sf32", 0)
        for v in d50_to_d65:
            out += struct.pack(">I", s15f16(v))
        return out

    def trc_tag() -> bytes:
        # Parametric gamma (function type 0 = gamma, g as s15Fixed16). A plain
        # 'curv' gamma is rejected by lcms for transforms in this profile
        # shape (measured); the parametric form is accepted and equivalent.
        return struct.pack(">III", 0x70617261, 0, 0) + struct.pack(">I", s15f16(2.2))

    def mluc_tag(text: str) -> bytes:
        utf16 = text.encode("utf-16-be")
        # 'mluc' | reserved | recordCount | recordSize | records | text
        body = struct.pack(">IIII", 0x6D6C7563, 0, 1, 12) + struct.pack(
            ">2s2sII", b"en", b"US", len(utf16), 28
        ) + utf16
        return body

    def text_tag(text: str) -> bytes:
        raw = text.encode("ascii")
        return struct.pack(">II", 0x74657874, len(raw)) + raw

    tags = [
        (b"desc", mluc_tag("AdobeRGB1998 compatible (constructed)")),
        (b"cprt", text_tag("MViewer fixture generator")),
        (b"wtpt", xyz_tag(wtpt)),
        (b"chad", chad_tag()),
        (b"rXYZ", xyz_tag(rXYZ)),
        (b"gXYZ", xyz_tag(gXYZ)),
        (b"bXYZ", xyz_tag(bXYZ)),
        (b"rTRC", trc_tag()),
        (b"gTRC", trc_tag()),
        (b"bTRC", trc_tag()),
    ]

    def pad4(b: bytes) -> bytes:
        return b + b"\x00" * ((4 - len(b) % 4) % 4)

    header_size = 128
    tag_table_size = 4 + 12 * len(tags)
    offset = header_size + tag_table_size
    table = struct.pack(">I", len(tags))
    payload = b""
    for sig, data in tags:
        table += sig + struct.pack(">II", offset, len(data))
        payload += pad4(data)
        offset += len(pad4(data))

    size = header_size + tag_table_size + len(payload)
    header = struct.pack(
        ">I4sI4s4s4s6H4s4sIII8sI3II",
        size,
        b"lcms",
        0x02100000,
        b"mntr",
        b"RGB ",
        b"XYZ ",
        2000, 1, 1, 0, 0, 0,  # datetime (UTC 2000-01-01 00:00:00)
        b"acsp",
        b"MSFT",
        0,  # flags
        0,  # manufacturer
        0,  # model
        b"\x00" * 8,  # attributes
        0,  # rendering intent (perceptual)
        s15f16(0.9642), s15f16(1.0), s15f16(0.8249),  # D50 illuminant
        0,  # creator
    )
    # ICC.1 header is 128 bytes: the fields above fill 84, the remainder is a
    # reserved section that must be zero.
    assert len(header) == 84, len(header)
    header += b"\x00" * (header_size - len(header))
    return header + table + payload


def root_dir() -> str:
    return os.path.dirname(os.path.abspath(__file__))


def large_dir() -> str:
    return os.path.join(root_dir(), "large")


def base_pattern(w: int = 512, h: int = 512) -> "Image":
    """Deterministic base pattern: vertical gradient + grid + corner markers.

    Pixel values depend only on (x, y), never on randomness, so the resized
    products are reproducible across runs/machines.
    """
    from PIL import Image, ImageDraw

    im = Image.new("RGB", (w, h))
    px = im.load()
    cell = 64
    for y in range(h):
        for x in range(w):
            g = (x * 255) // w
            r = (y * 255) // h
            b = ((x + y) * 255) // (w + h)
            # grid lines make block alignment visible at any LOD.
            if x % cell == 0 or y % cell == 0:
                r, g, b = 255, 255, 255
            px[x, y] = (r, g, b)
    d = ImageDraw.Draw(im)
    # corner markers for orientation checks: top-left red, top-right green,
    # bottom-left blue, bottom-right yellow blocks.
    d.rectangle([0, 0, 31, 31], fill=(255, 0, 0))
    d.rectangle([w - 32, 0, w - 1, 31], fill=(0, 255, 0))
    d.rectangle([0, h - 32, 31, h - 1], fill=(0, 0, 255))
    d.rectangle([w - 32, h - 32, w - 1, h - 1], fill=(255, 255, 0))
    return im


def make_pattern(w: int, h: int):
    """Deterministically create a (w x h) patterned image via NEAREST resize."""
    from PIL import Image

    base = base_pattern()
    # NEAREST preserves the exact cell values -> deterministic, fast, cheap.
    return base.resize((w, h), Image.NEAREST)


def make_16bit_pattern(w: int, h: int):
    """Create a deterministic unsigned 16-bit grayscale source without NumPy."""
    from array import array
    from PIL import Image

    values = array("H")
    for y in range(h):
        row = [(x * 65535 // max(1, w - 1) + y * 65535 // max(1, h - 1)) // 2
               for x in range(w)]
        values.extend(row)
    if values.itemsize != 2:
        values.byteswap()
    return Image.frombytes("I;16", (w, h), values.tobytes())


def _read_exif_orientation(tag_value: int) -> bytes:
    """Encode an EXIF orientation tag (274) into a minimal EXIF blob."""
    # Build a minimal Exif dict and let Pillow serialize it.
    from PIL import Image

    exif = Image.Exif()
    exif[_ORIENTATION_TAG] = tag_value
    return exif.tobytes()


def _ensure_jpeg_exif_orientation(exif: bytes, tag_value: int) -> bytes:
    """Pillow may drop an orientation if it considers it redundant; force it."""
    from PIL import Image

    ex = Image.Exif()
    ex.load_from_fp if False else None
    try:
        ex.load(exif)
    except Exception:
        ex = Image.Exif()
    ex[_ORIENTATION_TAG] = tag_value
    return ex.tobytes()


def generate(root: str) -> None:
    from PIL import Image, ImageDraw

    os.makedirs(os.path.join(root, "large"), exist_ok=True)

    def path(name: str) -> str:
        return os.path.join(root, "large", name)

    def want(name: str) -> bool:
        return args.force or not os.path.isfile(path(name))

    if want("large_jpeg_100mp.jpg"):
        img = make_pattern(12000, 8333)
        img.save(path("large_jpeg_100mp.jpg"), "JPEG", quality=80, optimize=False)
        print("generated large_jpeg_100mp.jpg")

    if want("large_tiff_100mp.tiff"):
        img = make_pattern(10000, 10000)
        # LZW keeps the gradient pattern small on disk; no strip/planar tricks.
        img.save(path("large_tiff_100mp.tiff"), "TIFF", compression="tiff_lzw")
        print("generated large_tiff_100mp.tiff")

    # M53 Phase 0: high-bit-depth TIFF and large PNG/BMP boundary fixtures.
    if want("large_tiff_16bit.tiff"):
        img = make_16bit_pattern(4096, 4096)
        img.save(path("large_tiff_16bit.tiff"), "TIFF", compression="tiff_lzw")
        print("generated large_tiff_16bit.tiff (16-bit grayscale)")

    if want("large_png_16mp.png"):
        make_pattern(4096, 4096).save(path("large_png_16mp.png"), "PNG", optimize=False)
        print("generated large_png_16mp.png")

    if want("large_bmp_16mp.bmp"):
        make_pattern(4096, 4096).save(path("large_bmp_16mp.bmp"), "BMP")
        print("generated large_bmp_16mp.bmp")

    if want("high_compression.jpg"):
        img = make_pattern(6000, 4000)
        img.save(path("high_compression.jpg"), "JPEG", quality=4, optimize=False)
        print("generated high_compression.jpg")

    if want("exif_orientation6.jpg"):
        img = make_pattern(4096, 4096)
        ex = Image.Exif()
        ex[_ORIENTATION_TAG] = 6
        img.save(path("exif_orientation6.jpg"), "JPEG", quality=80, exif=ex.tobytes())
        print("generated exif_orientation6.jpg")

    if want("exif_orientation8.jpg"):
        img = make_pattern(4096, 4096)
        ex = Image.Exif()
        ex[_ORIENTATION_TAG] = 8
        img.save(path("exif_orientation8.jpg"), "JPEG", quality=80, exif=ex.tobytes())
        print("generated exif_orientation8.jpg")

    if want("exif_orientation6_wide.jpg"):
        # Non-square oriented fixture: the probe must report the DISPLAYED
        # geometry (2000x4000), not the raw 4000x2000, for orientation 6.
        img = make_pattern(4000, 2000)
        ex = Image.Exif()
        ex[_ORIENTATION_TAG] = 6
        img.save(path("exif_orientation6_wide.jpg"), "JPEG", quality=80, exif=ex.tobytes())
        print("generated exif_orientation6_wide.jpg")

    if want("icc_adobe.jpg"):
        img = make_pattern(2048, 2048)
        try:
            from PIL import ImageCms

            prof_srgb = ImageCms.createProfile("sRGB")
            img.save(path("icc_adobe.jpg"), "JPEG", quality=90, icc_profile=ImageCms.ImageCmsProfile(prof_srgb).tobytes())
            print("generated icc_adobe.jpg (sRGB ICC embedded)")
        except Exception as exc:  # noqa: BLE001 - littlecms2 may be absent
            img.save(path("icc_adobe.jpg"), "JPEG", quality=90)
            print("generated icc_adobe.jpg (ICC embed unavailable: %s)" % exc)

    # M48 Phase 0: a REAL AdobeRGB1998 profile (constructed deterministically,
    # verified by lcms at generation time) embedded into 12MP and 100MP JPEGs.
    # A solid green patch is drawn over the pattern so display-pipeline tests
    # can sample a flat region whose expected sRGB value is known analytically.
    adobe_profile = build_adobe_rgb_1998_profile()
    try:
        from PIL import ImageCms
        import io as _io

        ImageCms.ImageCmsProfile(_io.BytesIO(adobe_profile))  # lcms parse check
    except Exception as exc:  # pragma: no cover - generator self-check
        raise SystemExit("constructed AdobeRGB1998 profile failed lcms parse: %s" % exc)

    def save_adobe_jpeg(name, w, h, quality, patch):
        if not want(name):
            return
        img = make_pattern(w, h)
        d = ImageDraw.Draw(img)
        x0, y0, pw, ph = patch
        # In-gamut yellowish-green: under the AdobeRGB1998 matrix its sRGB
        # display value differs strongly from the raw RGB (measured delta >100
        # in one channel), so display-pipeline tests can detect a skipped ICC
        # conversion.
        d.rectangle([x0, y0, x0 + pw - 1, y0 + ph - 1], fill=(128, 224, 0))
        img.save(path(name), "JPEG", quality=quality, icc_profile=adobe_profile,
                 optimize=False)
        print("generated %s (constructed AdobeRGB1998 ICC embedded)" % name)

    save_adobe_jpeg("icc_adobe_12mp.jpg", 4000, 3000, 92, (1952, 1452, 96, 96))
    save_adobe_jpeg("icc_adobe_100mp.jpg", 12000, 8333, 85, (6020, 4200, 160, 160))

    # M48 Phase 0: NON-SQUARE EXIF orientation 2..8 fixtures with corner
    # markers (the base pattern's TL/TR/BL/BR blocks stay at raw corners) at
    # 24 MP so the Viewer LOD/region path (which only engages > 16 MP) is
    # exercised. The pixels are saved RAW; the EXIF tag declares the transform.
    for orient in range(2, 9):
        name = "exif_orient%d_non_square.jpg" % orient
        if not want(name):
            continue
        img = make_pattern(6000, 4000)
        ex = Image.Exif()
        ex[_ORIENTATION_TAG] = orient
        img.save(path(name), "JPEG", quality=80, exif=ex.tobytes())
        print("generated %s (EXIF orientation %d)" % (name, orient))

    # M48 Phase 0: >60MP 1px-period high-frequency pattern (72 MP). Built as a
    # 1000x1000 tile via frombytes (C-speed paste of a 9x8 grid; per-pixel
    # formula only fills 1M pixels once).
    if want("deepzoom_hf_72mp.jpg"):
        tile_w, tile_h = 1000, 1000
        buf = bytearray(tile_w * tile_h * 3)
        i = 0
        for y in range(tile_h):
            for x in range(tile_w):
                buf[i] = (x * 37 + y * 17 + 11) & 0xFF
                buf[i + 1] = (x * 13 + y * 53 + 29) & 0xFF
                buf[i + 2] = (x * 71 + y * 7 + 43) & 0xFF
                i += 3
        tile = Image.frombytes("RGB", (tile_w, tile_h), bytes(buf))
        img = Image.new("RGB", (9000, 8000))
        for ty in range(8):
            for tx in range(9):
                img.paste(tile, (tx * tile_w, ty * tile_h))
        img.save(path("deepzoom_hf_72mp.jpg"), "JPEG", quality=85, optimize=False)
        print("generated deepzoom_hf_72mp.jpg (1px-period, 72MP)")

    if want("extreme_wide.jpg"):
        img = make_pattern(20000, 400)
        img.save(path("extreme_wide.jpg"), "JPEG", quality=80, optimize=False)
        print("generated extreme_wide.jpg")

    if want("extreme_tall.jpg"):
        img = make_pattern(400, 20000)
        img.save(path("extreme_tall.jpg"), "JPEG", quality=80, optimize=False)
        print("generated extreme_tall.jpg")

    if want("truncated_large.jpg"):
        # Full valid encode, then truncate deterministically (~55%).
        full = os.path.join(root, "large", "_tmp_intact.jpg")
        img = make_pattern(6000, 4000)
        img.save(full, "JPEG", quality=80)
        data = open(full, "rb").read()
        cut = int(len(data) * 0.55)
        with open(path("truncated_large.jpg"), "wb") as fh:
            fh.write(data[:cut])
        os.remove(full)
        print("generated truncated_large.jpg")

    if want("truncated_large.tiff"):
        full = os.path.join(root, "large", "_tmp_intact.tiff")
        img = make_pattern(4000, 4000)
        img.save(full, "TIFF", compression="tiff_lzw")
        data = open(full, "rb").read()
        cut = int(len(data) * 0.50)
        with open(path("truncated_large.tiff"), "wb") as fh:
            fh.write(data[:cut])
        os.remove(full)
        print("generated truncated_large.tiff")


def check(root: str) -> bool:
    ok = True
    for name, (w, h, min_bytes) in sorted(REQUIRED.items()):
        p = os.path.join(root, "large", name)
        if not os.path.isfile(p):
            print("MISSING %s" % p)
            ok = False
            continue
        size = os.path.getsize(p)
        if size < min_bytes:
            print("TOO_SMALL %s (%d bytes < %d)" % (name, size, min_bytes))
            ok = False
        try:
            with Image.open(p) as im:
                if im.size != (w, h):
                    print("DIMS %s got %dx%d want %dx%d" % (name, im.size[0], im.size[1], w, h))
                    ok = False
        except Exception as exc:  # noqa: BLE001 - truncated files fail to open
            # Truncated fixtures are EXPECTED to fail Pillow's full decode but
            # must still be openable enough to report an error, never crash.
            if name.startswith("truncated_"):
                continue
            print("DECODE_FAIL %s: %s" % (name, exc))
            ok = False
    return ok


def main() -> int:
    global args
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true", help="validate only")
    ap.add_argument("--ensure", action="store_true", help="generate missing then validate")
    ap.add_argument("--force", action="store_true", help="regenerate everything")
    args = ap.parse_args()

    try:
        import PIL  # noqa: F401
    except ImportError:
        print("Pillow is not installed; cannot generate large fixtures")
        return 1

    root = root_dir()
    if args.check:
        return 0 if check(root) else 1
    generate(root)
    if args.ensure or args.force:
        if not check(root):
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
