"""Flag isolated hot / dead pixels.

Usage (the bridge supplies the paths):
    python -I hot_pixels.py <request.json> <response.json>

Request  : {"image": "<absolute path>", "args": {...}}
Response : {"ok": true, "message": "...", "boxes": [[x, y, w, h, "label"], ...]}

A pixel is reported when it is at least `delta` codes away from EVERY value in
its 3x3 neighbourhood, which rejects smooth gradients and edges while keeping
true single-pixel defects. Tune `DELTA`; only PNG input is supported (see
_imagedata.py).
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from _imagedata import read_png  # noqa: E402  (explicit path under `python -I`)

DELTA = 48        # codes of isolation required to report a pixel
MAX_BOXES = 200   # keep the overlay readable on a badly damaged sensor


def luma(rgb):
    return (rgb[0] * 299 + rgb[1] * 587 + rgb[2] * 114) // 1000


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    with open(sys.argv[1], "r", encoding="utf-8") as fh:
        request = json.load(fh)
    image = request.get("image", "")
    args = request.get("args") or {}
    delta = int(args.get("delta", DELTA))
    max_boxes = int(args.get("maxBoxes", MAX_BOXES))

    try:
        width, height, pixels = read_png(image)
    except Exception as exc:  # report, never traceback into the viewer
        with open(sys.argv[2], "w", encoding="utf-8") as out:
            json.dump({"ok": False, "message": "cannot decode %s: %s" % (image, exc)}, out)
        return 0

    def at(x, y):
        return luma(pixels[y * width + x])

    boxes = []
    for y in range(height):
        for x in range(width):
            centre = at(x, y)
            isolated = True
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    if dx == 0 and dy == 0:
                        continue
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < width and 0 <= ny < height:
                        if abs(at(nx, ny) - centre) < delta:
                            isolated = False
                            break
                if not isolated:
                    break
            if isolated:
                kind = "hot" if centre > 128 else "dead"
                boxes.append([x, y, 1, 1, "%s@%d,%d=%d" % (kind, x, y, centre)])
                if len(boxes) >= max_boxes:
                    break
        if len(boxes) >= max_boxes:
            break

    message = "%d isolated pixel(s) at delta=%d (%dx%d)" % (len(boxes), delta, width, height)
    if len(boxes) >= max_boxes:
        message += " - truncated at %d" % max_boxes
    with open(sys.argv[2], "w", encoding="utf-8") as out:
        json.dump({"ok": True, "message": message, "boxes": boxes}, out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
