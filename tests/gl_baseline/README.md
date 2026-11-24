# Compare-canvas pixel baseline

Reference captures of what the **CPU QPainter** compare canvas paints today.
They exist so a future GPU compositor (review P2-09) can be diffed against them
pixel by pixel instead of being declared correct by inspection.

## Contents

| Path | Scale factor | Source |
|---|---|---|
| `dpr1/` | 1.0 | captured from v1.0.212, Ubuntu 22.04 / Qt 6.8 / llvmpipe |
| `dpr2/` | 2.0 | same |

Each directory holds seven images: `split_0/1` (divider at 35% and 65%), `swipe_0/1`
(same positions), `overlay_0`, `checker_0`, `diff_0`.

## How to capture

The tool is deliberately **not** a ctest target - it needs a window system:

```sh
# build it against the normal libraries (see the file header)
Xvfb :99 -screen 0 1600x1000x24 &
DISPLAY=:99 QT_QPA_PLATFORM=xcb ./capture_compare_canvas <outdir> [scaleFactor]
```

## How to compare

```sh
python scripts/compare_gl_baseline.py <fresh-capture-dir> [--baseline tests/gl_baseline/dpr1]
```

Exit code is non-zero when any image differs by more than `--tolerance`
(default 2 codes per channel).

## Caveats

- Captured with Mesa **llvmpipe** (software). A real GPU driver may rasterise
  antialiased edges one code differently; treat the default tolerance of 2 as
  the intended slack, not as an exact-match claim.
- The baselines encode the current split geometry, overlay alpha, checker size
  and diff threshold. Changing any of those in the product changes the baseline
  too - re-capture rather than adjusting the comparator.
