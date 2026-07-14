# Testdata Fixtures

Battery of deterministic reference images and intentionally broken files used by
MViewer's CI to verify image loading, parsing, and error handling.

## What's in This Directory

```
testdata/
├── generate_fixtures.py   — Run to regenerate all binaries (committed to git)
├── README.md              — You are here (committed to git)
├── CATALOG.md             — Tabular fixture catalog (committed)
├── .gitignore             — Excludes golden/** and corrupted/** binaries
├── golden/
│   ├── .gitkeep           — Keeps dir in git even when contents gitignored
│   ├── 64x64/             — Tiny (5 patterns x 4 formats = 20 files)
│   ├── 256x256/           — Small (5 patterns x 4 formats = 20 files)
│   ├── 512x512/           — Medium (3 patterns x 4 formats = 12 files)
│   ├── 1024x768/          — Large (3 patterns x 4 formats = 12 files)
│   ├── 1920x1080/         — xLarge (3 patterns x 4 formats = 12 files)
│   ├── 3840x2160/         — xxLarge (3 patterns x 4 formats = 12 files)
│   └── 8192x8192/         — Huge (2 patterns x 4 formats = 8 files)
└── corrupted/
    ├── .gitkeep           — Keeps dir in git
    ├── empty_64x64.png
    ├── truncated_50pct_64x64.jpg
    ├── ...                — 20 files testing decoder error paths
```

## How CI Uses These Files

**Golden regression tests** — Load each golden file with `QImage`, compare
against a known-pixel buffer (or MD5), assert the image decoded correctly with
correct dimensions and pixel format. Triggered on every PR.

**Corrupted error-path tests** — For each corrupted fixture, assert that
QImageReader returns null (or that `QImage::isNull()` is true) without crashing.
Also tests that the app shows a user-facing error instead of segfaulting.

**Format coverage** — Ensures every supported format (PNG/JPEG/BMP/TIFF) exercises
the correct `QImageReader` plugin. CI will fail if a format plugin is missing.

**Pixel-accurate tests** — Some tests verify exact pixel values (gradient at x=50
should be `(49, y*255/256, ...)`). The deterministic generation guarantees these
assertions hold across platforms and CI runners.

## Fixture Generation

```bash
# Clean-slate regeneration
python testdata/generate_fixtures.py --clean

# Incremental regeneration (adds only missing files)
python testdata/generate_fixtures.py

# Verify all expected files exist & are non-empty
python testdata/generate_fixtures.py --verify

# Print a summary table of every file and its size
python testdata/generate_fixtures.py --report
```

## Common Operations

| Goal | Command |
|------|---------|
| Regenerate all | `python testdata/generate_fixtures.py --clean` |
| Add new golden | `python testdata/generate_fixtures.py` (then commit CATALOG.md) |
| Add new corrupted | Manually add to `generate_fixtures.py`, re-run |
| Verify CI health | `python testdata/generate_fixtures.py --verify` |

## Git Policy

- `golden/**` — **gitignored** (large binaries, no diff benefit)
- `corrupted/**` — **gitignored** (test fixtures only)
- `generate_fixtures.py` — **tracked** (source of truth)
- `CATALOG.md` — **tracked** (human-readable manifest)
- `README.md` — **tracked** (this file)

To make this work, each subdirectory has a `.gitkeep` placeholder so git retains
the directory even though the binary contents are ignored.

## Size Budget

Golden directory totals ~593 MB across 96 files (24 per format). This is
acceptable for CI runners with 10 GB+ disk but large for a git clone — which is
why the directory is gitignored by default.
