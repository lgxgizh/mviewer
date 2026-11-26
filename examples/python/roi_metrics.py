"""Per-region metrics: mean / contrast of a grid, with boxes on outliers.

Usage (the bridge supplies the paths):
    python -I roi_metrics.py <request.json> <response.json>

Args:
    grid           cells per axis (default 4)
    maxSpread      report a cell when its mean differs from the frame mean by
                   more than this many codes (default 24)

The response carries one box per offending cell plus a `message` summary, so the
same script works as a quick flat-field / shading check. PNG input only.
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _imagedata import read_png  # noqa: E402


def luma(rgb):
    return (rgb[0] * 299 + rgb[1] * 587 + rgb[2] * 114) / 1000.0


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    with open(sys.argv[1], "r", encoding="utf-8") as fh:
        request = json.load(fh)
    args = request.get("args") or {}
    grid = max(1, int(args.get("grid", 4)))
    max_spread = float(args.get("maxSpread", 24.0))

    try:
        width, height, pixels = read_png(request.get("image", ""))
    except Exception as exc:
        with open(sys.argv[2], "w", encoding="utf-8") as out:
            json.dump({"ok": False, "message": "cannot decode image: %s" % exc}, out)
        return 0

    values = [luma(p) for p in pixels]
    tile_w = max(1, width // grid)
    tile_h = max(1, height // grid)

    def stats(x0, y0, x1, y1):
        total = 0.0
        count = 0
        for y in range(y0, y1):
            for x in range(x0, x1):
                total += values[y * width + x]
                count += 1
        return (total / count, count) if count else (0.0, 0)

    frame_mean = sum(values) / len(values)
    cells = []
    for ty in range(grid):
        for tx in range(grid):
            x0, y0 = tx * tile_w, ty * tile_h
            x1 = width if tx == grid - 1 else x0 + tile_w
            y1 = height if ty == grid - 1 else y0 + tile_h
            mean, count = stats(x0, y0, x1, y1)
            cells.append((x0, y0, x1, y1, mean, count))

    boxes = [
        [x0, y0, x1 - x0, y1 - y0, "mean %.1f (frame %.1f)" % (m, frame_mean)]
        for (x0, y0, x1, y1, m, _c) in cells
        if abs(m - frame_mean) > max_spread
    ]
    worst = max(abs(c[4] - frame_mean) for c in cells) if cells else 0.0
    message = ("frame mean %.1f over %d cell(s); %d outside +/-%.1f; worst deviation %.1f"
               % (frame_mean, len(cells), len(boxes), max_spread, worst))
    with open(sys.argv[2], "w", encoding="utf-8") as out:
        json.dump({"ok": True, "message": message, "boxes": boxes}, out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
