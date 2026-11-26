"""Grid noise estimate: high-frequency energy per tile.

Usage (the bridge supplies the paths):
    python -I noise_estimate.py <request.json> <response.json>

Reports one box per tile whose Laplacian energy is above `sigmaSigma` standard
deviations of the tile distribution, so a dead or very noisy corner of the frame
shows up without eyeballing a histogram. Args: grid (default 4), thresholdSigma
(default 2.0). PNG input only.
"""

import json
import math
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
    sigma_limit = float(args.get("thresholdSigma", 2.0))

    try:
        width, height, pixels = read_png(request.get("image", ""))
    except Exception as exc:
        with open(sys.argv[2], "w", encoding="utf-8") as out:
            json.dump({"ok": False, "message": "cannot decode image: %s" % exc}, out)
        return 0

    values = [luma(p) for p in pixels]
    tile_w = max(1, width // grid)
    tile_h = max(1, height // grid)

    def energy(x0, y0, x1, y1):
        """Mean absolute Laplacian inside the tile (a cheap roughness proxy)."""
        total = 0.0
        count = 0
        for y in range(y0, y1):
            for x in range(x0, x1):
                centre = values[y * width + x]
                lap = 4.0 * centre
                if x > 0:
                    lap -= values[y * width + x - 1]
                if x + 1 < width:
                    lap -= values[y * width + x + 1]
                if y > 0:
                    lap -= values[(y - 1) * width + x]
                if y + 1 < height:
                    lap -= values[(y + 1) * width + x]
                total += abs(lap)
                count += 1
        return total / count if count else 0.0

    tiles = []
    for ty in range(grid):
        for tx in range(grid):
            x0, y0 = tx * tile_w, ty * tile_h
            x1 = width if tx == grid - 1 else x0 + tile_w
            y1 = height if ty == grid - 1 else y0 + tile_h
            tiles.append((tx, ty, x0, y0, x1, y1, energy(x0, y0, x1, y1)))

    mean = sum(t[-1] for t in tiles) / len(tiles)
    var = sum((t[-1] - mean) ** 2 for t in tiles) / len(tiles)
    sigma = math.sqrt(var)
    limit = mean + sigma_limit * sigma

    boxes = [
        [x0, y0, x1 - x0, y1 - y0, "noise %.1f (mean %.1f)" % (e, mean)]
        for (_tx, _ty, x0, y0, x1, y1, e) in tiles
        if e > limit
    ]
    message = "noise %.1f +/- %.1f over %d tile(s); %d above %.1f" % (
        mean, sigma, len(tiles), len(boxes), limit)
    with open(sys.argv[2], "w", encoding="utf-8") as out:
        json.dump({"ok": True, "message": message, "boxes": boxes}, out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
