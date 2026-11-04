#!/usr/bin/env python3
"""Generate the MViewer application icon (aperture-ring squircle).

Writes the vector master and the raster assets used by the build:

    resources/mviewer.svg   1024x1024 master artwork
    resources/mviewer.png   1024x1024 (qrc :/app/mviewer.png, window icon)
    resources/mviewer.ico   16..256 multi-size, 256 stored as PNG (exe, NSIS)

Small sizes are drawn from simplified variants of the same geometry
(fuller tile, no drop shadow, wider blade gaps) so they stay legible.

Requires: shapely, resvg-py, pillow (a venv is fine):

    python -m venv .venv && .venv/bin/pip install shapely resvg-py pillow
    .venv/bin/python scripts/make_app_icon.py [--sheet /tmp/icon-sheet.png]
"""
from __future__ import annotations

import argparse
import io
import math
import struct
from pathlib import Path

import resvg_py
from PIL import Image
from shapely.geometry import LineString, Point, Polygon
from shapely.ops import unary_union

ROOT = Path(__file__).resolve().parent.parent
C = 512.0

# Spectrum, clockwise from the top blade.
BLADES = [
    ("#5CDBFF", "#1FA8FF"),  # cyan
    ("#8F7BFF", "#B04DFF"),  # violet
    ("#E04FE6", "#FF3D9A"),  # magenta
    ("#FF9A3D", "#FF5F2E"),  # orange
    ("#FF8A5C", "#F2557A"),  # coral
    ("#5A8BFF", "#4A5CF0"),  # blue
]


def squircle(x0, y0, size, n=5.0, steps=360):
    a = size / 2.0
    cx, cy = x0 + a, y0 + a
    pts = []
    for i in range(steps):
        t = 2 * math.pi * i / steps
        ct, st = math.cos(t), math.sin(t)
        x = a * math.copysign(abs(ct) ** (2 / n), ct)
        y = a * math.copysign(abs(st) ** (2 / n), st)
        pts.append((cx + x, cy + y))
    return pts


def path_d(pts):
    s = "M" + " L".join(f"{x:.1f},{y:.1f}" for x, y in pts) + " Z"
    return s


def geom_d(g):
    polys = [g] if g.geom_type == "Polygon" else list(g.geoms)
    out = []
    for p in polys:
        out.append(path_d(list(p.exterior.coords)[:-1]))
        for hole in p.interiors:
            out.append(path_d(list(hole.coords)[:-1]))
    return " ".join(out)


def rot(p, ang, c=(C, C)):
    x, y = p[0] - c[0], p[1] - c[1]
    ca, sa = math.cos(ang), math.sin(ang)
    return (c[0] + x * ca - y * sa, c[1] + x * sa + y * ca)


def blades(R, r, gap, round_r, bend, twist):
    """Six pinwheel blades: disk minus hole, split by six curved cuts."""
    disk = Point(C, C).buffer(R, 256)
    hole = Point(C, C).buffer(r, 128)
    # One cut: starts on the hole tangent, sweeps out past the rim, bent.
    a0 = math.radians(twist)
    tan_pt = (C + r * math.cos(a0), C + r * math.sin(a0))
    d = (-math.sin(a0), math.cos(a0))  # tangent direction
    start = tan_pt
    end = (tan_pt[0] + d[0] * R * 1.6, tan_pt[1] + d[1] * R * 1.6)
    mid = ((start[0] + end[0]) / 2, (start[1] + end[1]) / 2)
    nrm = (math.cos(a0), math.sin(a0))
    ctrl = (mid[0] + nrm[0] * bend, mid[1] + nrm[1] * bend)
    curve = []
    for i in range(65):
        t = i / 64
        x = (1 - t) ** 2 * start[0] + 2 * (1 - t) * t * ctrl[0] + t * t * end[0]
        y = (1 - t) ** 2 * start[1] + 2 * (1 - t) * t * ctrl[1] + t * t * end[1]
        curve.append((x, y))
    cuts = []
    for k in range(6):
        ang = k * math.pi / 3
        cuts.append(LineString([rot(p, ang) for p in curve]).buffer(gap / 2, cap_style=2))
    body = disk.difference(hole).difference(unary_union(cuts))
    pieces = sorted(body.geoms, key=lambda g: g.area, reverse=True)[:6]
    out = []
    for g in pieces:
        g = g.buffer(-round_r * 1.8, join_style=1).buffer(round_r * 1.8, join_style=1, resolution=24)
        g = g.simplify(0.15)
        cx, cy = g.centroid.x, g.centroid.y
        out.append((math.atan2(cy - C, cx - C), g))
    # order clockwise starting from the top (-90deg in screen coords)
    out.sort(key=lambda t: (t[0] + math.pi / 2) % (2 * math.pi))
    return [g for _, g in out]


def build_svg(variant="full"):
    if variant == "full":
        margin, R, r, gap, rr, bend, shadow = 100, 272, 54, 20, 13, 50, True
    elif variant == "mid":
        margin, R, r, gap, rr, bend, shadow = 56, 300, 74, 30, 16, 44, False
    else:  # small
        margin, R, r, gap, rr, bend, shadow = 24, 340, 92, 50, 18, 40, False
    size = 1024 - 2 * margin
    tile = squircle(margin, margin, size)
    tile_d = path_d(tile)
    bl = blades(R, r, gap, rr, bend, twist=-60)

    defs = [
        '<linearGradient id="tile" x1="0.15" y1="0" x2="0.85" y2="1">'
        '<stop offset="0" stop-color="#FDFEFF"/><stop offset="0.55" stop-color="#F1F5FC"/>'
        '<stop offset="1" stop-color="#D9E6F8"/></linearGradient>',
        '<radialGradient id="tileGlow" cx="0.3" cy="0.2" r="0.9">'
        '<stop offset="0" stop-color="#FFFFFF" stop-opacity="0.9"/>'
        '<stop offset="1" stop-color="#FFFFFF" stop-opacity="0"/></radialGradient>',
        '<linearGradient id="rim" x1="0" y1="0" x2="0" y2="1">'
        '<stop offset="0" stop-color="#FFFFFF" stop-opacity="1"/>'
        '<stop offset="1" stop-color="#B8CBE8" stop-opacity="0.9"/></linearGradient>',
        '<linearGradient id="gloss" x1="0" y1="0" x2="0.6" y2="1">'
        '<stop offset="0" stop-color="#FFFFFF" stop-opacity="0.55"/>'
        '<stop offset="0.45" stop-color="#FFFFFF" stop-opacity="0.08"/>'
        '<stop offset="1" stop-color="#FFFFFF" stop-opacity="0"/></linearGradient>',
    ]
    if shadow:
        defs.append(
            '<filter id="tileShadow" x="-20%" y="-20%" width="140%" height="150%">'
            '<feGaussianBlur in="SourceAlpha" stdDeviation="18"/>'
            '<feOffset dy="16" result="b"/>'
            '<feFlood flood-color="#1E2F55" flood-opacity="0.26"/>'
            '<feComposite in2="b" operator="in" result="s1"/>'
            '<feGaussianBlur in="SourceAlpha" stdDeviation="3"/><feOffset dy="3" result="c"/>'
            '<feFlood flood-color="#1E2F55" flood-opacity="0.18"/>'
            '<feComposite in2="c" operator="in" result="s2"/>'
            '<feMerge><feMergeNode in="s1"/><feMergeNode in="s2"/></feMerge></filter>')
        defs.append(
            '<filter id="bladeShadow" x="-30%" y="-30%" width="160%" height="170%">'
            '<feGaussianBlur in="SourceGraphic" stdDeviation="10"/>'
            '<feOffset dy="8"/><feComponentTransfer><feFuncA type="linear" slope="0.38"/>'
            '</feComponentTransfer></filter>')

    blade_paths = []
    for i, (g, (c0, c1)) in enumerate(zip(bl, BLADES)):
        minx, miny, maxx, maxy = g.bounds
        # gradient runs from the outer rim towards the blade's inner tip
        ang = math.atan2(g.centroid.y - C, g.centroid.x - C)
        ox, oy = C + math.cos(ang) * R, C + math.sin(ang) * R
        ix, iy = C + math.cos(ang + 1.0) * r * 0.5, C + math.sin(ang + 1.0) * r * 0.5
        defs.append(
            f'<linearGradient id="b{i}" gradientUnits="userSpaceOnUse" '
            f'x1="{ox:.1f}" y1="{oy:.1f}" x2="{ix:.1f}" y2="{iy:.1f}">'
            f'<stop offset="0" stop-color="{c0}"/><stop offset="1" stop-color="{c1}"/>'
            '</linearGradient>')
        blade_paths.append((i, geom_d(g)))

    body = []
    if shadow:
        body.append(f'<path d="{tile_d}" fill="#000" filter="url(#tileShadow)"/>')
    body.append(f'<path d="{tile_d}" fill="url(#tile)"/>')
    body.append(f'<path d="{tile_d}" fill="url(#tileGlow)" opacity="0.7"/>')
    sw = 4 if variant == "full" else 10
    body.append(f'<path d="{tile_d}" fill="none" stroke="url(#rim)" stroke-width="{sw}"/>')
    if variant == "small":
        body.append(f'<path d="{tile_d}" fill="none" stroke="#9FB4D6" stroke-width="22" '
                    'stroke-opacity="0.75"/>')
    if shadow:
        body.append('<g filter="url(#bladeShadow)">' + "".join(
            f'<path d="{d}" fill="url(#b{i})"/>' for i, d in blade_paths) + '</g>')
    for i, d in blade_paths:
        body.append(f'<path d="{d}" fill="url(#b{i})"/>')
        if variant != "small":
            body.append(f'<path d="{d}" fill="url(#gloss)"/>')
            body.append(f'<path d="{d}" fill="none" stroke="#FFFFFF" stroke-opacity="0.45" '
                        f'stroke-width="{3 if variant == "full" else 5}"/>')

    return ('<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" '
            'viewBox="0 0 1024 1024">\n<defs>' + "".join(defs) + '</defs>\n'
            + "\n".join(body) + '\n</svg>\n')


def render(svg, px):
    data = bytes(resvg_py.svg_to_bytes(svg_string=svg, width=px, height=px))
    return Image.open(io.BytesIO(data)).convert("RGBA")


def variant_for(px):
    if px <= 32:
        return "small"
    if px <= 96:
        return "mid"
    return "full"


def write_ico(path, images):
    entries, blobs = [], []
    for im in images:
        w, h = im.size
        if w >= 256:
            buf = io.BytesIO(); im.save(buf, "PNG", optimize=True); blob = buf.getvalue()
        else:
            # 32-bit BGRA DIB with AND mask
            px = im.tobytes("raw", "BGRA")
            rows = [px[y * w * 4:(y + 1) * w * 4] for y in range(h)][::-1]
            mask_stride = ((w + 31) // 32) * 4
            mask = b"\x00" * mask_stride * h
            hdr = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, w * h * 4 + len(mask), 0, 0, 0, 0)
            blob = hdr + b"".join(rows) + mask
        entries.append((w, h, blob))
    off = 6 + 16 * len(entries)
    out = struct.pack("<HHH", 0, 1, len(entries))
    for w, h, blob in entries:
        out += struct.pack("<BBBBHHII", w % 256, h % 256, 0, 0, 1, 32, len(blob), off)
        off += len(blob)
    Path(path).write_bytes(out + b"".join(b for _, _, b in entries))


ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sheet", help="write a light/dark contact sheet here")
    ap.add_argument("--out", default=str(ROOT / "resources"))
    a = ap.parse_args()
    out = Path(a.out)
    svgs = {v: build_svg(v) for v in ("full", "mid", "small")}
    (out / "mviewer.svg").write_text(svgs["full"], encoding="utf-8")
    big = render(svgs["full"], 1024)
    big.save(out / "mviewer.png", "PNG", optimize=True)
    imgs = []
    for s in ICO_SIZES:
        v = variant_for(s)
        # supersample then downscale for crisp small sizes
        im = render(svgs[v], s * 4).resize((s, s), Image.LANCZOS)
        imgs.append(im)
    write_ico(out / "mviewer.ico", imgs)
    if a.sheet:
        sizes = ICO_SIZES + [512]
        imgs2 = imgs + [render(svgs["full"], 512)]
        W = sum(s + 24 for s in sizes) + 24
        H = (512 + 48) * 2
        sheet = Image.new("RGBA", (W, H), "#FFFFFF")
        dark = Image.new("RGBA", (W, H // 2), "#1E1F24")
        sheet.paste(dark, (0, H // 2))
        x = 24
        for s, im in zip(sizes, imgs2):
            for row in range(2):
                y = row * (H // 2) + (H // 2 - s) // 2
                sheet.alpha_composite(im, (x, y))
            x += s + 24
        sheet.save(a.sheet)


if __name__ == "__main__":
    main()
