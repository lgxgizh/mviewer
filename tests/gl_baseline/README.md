# Compare-canvas pixel baseline

Reference captures of what the **CPU QPainter** compare canvas paints today.
They exist so a future GPU compositor (review P2-09) can be diffed against them
pixel by pixel instead of being declared correct by inspection.

The ImageViewer tile path was verified the same way first: rendering the same
frame with `MVIEWER_GPU=1` and `MVIEWER_BLITTER=qt` versus
`MVIEWER_BLITTER=shader` gives a maximum per-channel difference of **0**, for both
a plain frame and a zebra overlay (`capture_viewer_gl.cpp`). That is what makes
ADR 037's opt-in safe to keep.

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

- The mode's own divider (and the swipe drag handle) is painted after the GL
  image, so a boundary pixel is a blend there too; the compositor leaves those to
  the CPU paint step.
- Captured with Mesa **llvmpipe** (software). A real GPU driver may rasterise
  antialiased edges one code differently; treat the default tolerance of 2 as
  the intended slack, not as an exact-match claim.
- The baselines encode the current split geometry, overlay alpha, checker size
  and diff threshold. Changing any of those in the product changes the baseline
  too - re-capture rather than adjusting the comparator.

## GPU compositing vs this baseline

`MVIEWER_COMPARE_GL=1` routes the split / swipe / overlay composite through a
fragment shader (`docs/adr/038`). Measured on Ubuntu 22.04 / Qt 6.8 / llvmpipe:
**zero differing pixels** for all three modes at scale factors 1.0 and 2.0.
Checkerboard is deliberately left on the CPU path (see ADR 038), so it matches by
construction rather than by comparison.
