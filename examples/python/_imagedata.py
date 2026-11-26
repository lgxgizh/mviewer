"""Minimal pure-stdlib PNG reader for the MViewer script bridge examples.

The script bridge runs `python -I`, which keeps the interpreter away from
PYTHONPATH and from the script's own directory. Importing this helper therefore
needs the explicit ``sys.path`` insert you see in every example script.

Only what the examples need is supported: non-interlaced, 8-bit greyscale /
greyscale+alpha / RGB / RGBA, all five row filters. Anything else raises
``ValueError`` so the caller can report it instead of silently misreading bytes.
"""

import struct
import zlib


def _rows(data, width, height, channels):
    """Undo PNG row filtering; yields `height` tuples of `width*channels` ints."""
    stride = width * channels
    if len(data) < height * (stride + 1):
        raise ValueError("truncated PNG image data")
    prev = bytearray(stride)
    pos = 0
    out = []
    for _ in range(height):
        ftype = data[pos]
        pos += 1
        line = bytearray(data[pos:pos + stride])
        pos += stride
        if ftype == 0:
            pass
        elif ftype == 1:  # Sub
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif ftype == 2:  # Up
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ftype == 3:  # Average
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif ftype == 4:  # Paeth
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = prev[i]
                c = prev[i - channels] if i >= channels else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        else:
            raise ValueError("unknown PNG filter %d" % ftype)
        out.append(tuple(line))
        prev = line
    return out


def read_png(path):
    """Return (width, height, rgb) with rgb a flat list of (r, g, b) tuples."""
    with open(path, "rb") as fh:
        blob = fh.read()
    if blob[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG file")

    pos = 8
    width = height = depth = color = None
    idat = bytearray()
    palette = None
    while pos + 8 <= len(blob):
        (length,) = struct.unpack(">I", blob[pos:pos + 4])
        ctype = blob[pos + 4:pos + 8]
        body = blob[pos + 8:pos + 8 + length]
        if ctype == b"IHDR":
            width, height, depth, color, comp, filt, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or interlace != 0:
                raise ValueError("only non-interlaced 8-bit PNG is supported")
        elif ctype == b"IDAT":
            idat += body
        elif ctype == b"PLTE":
            palette = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif ctype == b"IEND":
            break
        pos += 12 + length

    if color == 2:      # RGB
        channels = 3
    elif color == 6:    # RGBA
        channels = 4
    elif color == 0:    # grey
        channels = 1
    elif color == 4:    # grey + alpha
        channels = 2
    elif color == 3:    # indexed
        channels = 1
    else:
        raise ValueError("unsupported PNG colour type %r" % (color,))

    raw = zlib.decompress(bytes(idat))
    rows = _rows(raw, width, height, channels)
    flat = []
    for row in rows:
        for x in range(width):
            px = row[x * channels:(x + 1) * channels]
            if color == 3:
                flat.append(palette[px[0]] if palette and px[0] < len(palette) else (0, 0, 0))
            elif color == 0 or color == 4:
                flat.append((px[0], px[0], px[0]))
            else:
                flat.append((px[0], px[1], px[2]))
    return width, height, flat
