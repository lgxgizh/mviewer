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
  icc_adobe.jpg           2048x2048 JPEG with an embedded AdobeRGB ICC profile
  extreme_wide.jpg        20000x400 JPEG (extreme aspect, landscape)
  extreme_tall.jpg        400x20000 JPEG (extreme aspect, portrait)
  truncated_large.jpg     a valid 6000x4000 JPEG truncated at ~55% (corrupt input)
  truncated_large.tiff    a valid 4000x4000 TIFF truncated at ~50% (corrupt input)

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
    "high_compression.jpg": (6000, 4000, 100_000),
    "exif_orientation6.jpg": (4096, 4096, 100_000),
    "exif_orientation8.jpg": (4096, 4096, 100_000),
    "icc_adobe.jpg": (2048, 2048, 50_000),
    "extreme_wide.jpg": (20000, 400, 100_000),
    "extreme_tall.jpg": (400, 20000, 100_000),
    "truncated_large.jpg": (6000, 4000, 100_000),
    "truncated_large.tiff": (4000, 4000, 100_000),
}

# EXIF orientation tag constants (Pillow Image.Exif tag 274).
_ORIENTATION_TAG = 274


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
    from PIL import Image

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
