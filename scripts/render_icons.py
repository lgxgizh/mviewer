#!/usr/bin/env python3
"""Rasterize toolbar and theme SVGs to pre-rendered PNG masks.

MViewer does not link Qt Svg (the Linux CI image only has qt6-base). Toolbar
icons are tinted at runtime from these masks, so each SVG is drawn once per
scale with QSvgRenderer and saved as an alpha PNG.

Toolbar masks (logical size 16, matching kToolbarIconPx):

    resources/icons/<id>.png
    resources/icons/<id>@1.5x.png
    resources/icons/<id>@2x.png
    resources/icons/<id>@3x.png

Theme indicator masks (Qt stylesheets load @2x and @3x automatically when the
qrc alias sits next to the 1x name):

    check / dot             16, 32, 48
    chevron-up / chevron-down   10, 20, 30

Requires PySide6. Headless:

    QT_QPA_PLATFORM=offscreen /path/to/python scripts/render_icons.py
    QT_QPA_PLATFORM=offscreen /path/to/python scripts/render_icons.py \\
        --contact /tmp/icons_contact.png

The contact sheet is a review image (dark and light tints). It is not an
application resource.
"""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

from PySide6.QtCore import QRectF, Qt
from PySide6.QtGui import QColor, QFont, QGuiApplication, QImage, QPainter
from PySide6.QtSvg import QSvgRenderer

ROOT = Path(__file__).resolve().parents[1]
ICON_DIR = ROOT / "resources" / "icons"
THEME_DIR = ROOT / "resources" / "theme"

# Logical toolbar pixel size. Keep in sync with mviewer::ui::kToolbarIconPx.
TOOLBAR_PX = 16
TOOLBAR_SCALES = (
    ("", 1.0),
    ("@1.5x", 1.5),
    ("@2x", 2.0),
    ("@3x", 3.0),
)

# (logical px, scales). Chevrons are 10px in the stylesheet; check and dot
# fill a 16px indicator.
THEME_SPECS = {
    "check": (16, (1.0, 2.0, 3.0)),
    "dot": (16, (1.0, 2.0, 3.0)),
    "chevron-up": (10, (1.0, 2.0, 3.0)),
    "chevron-down": (10, (1.0, 2.0, 3.0)),
}


def render_svg(svg_path: Path, pixels: int) -> QImage:
    """Draw one SVG into a square transparent image of `pixels` on a side."""
    renderer = QSvgRenderer(str(svg_path))
    if not renderer.isValid():
        raise RuntimeError(f"invalid SVG: {svg_path}")
    image = QImage(pixels, pixels, QImage.Format_ARGB32_Premultiplied)
    image.fill(Qt.transparent)
    painter = QPainter(image)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
    renderer.render(painter, QRectF(0, 0, pixels, pixels))
    painter.end()
    return image.convertToFormat(QImage.Format_ARGB32)


def save_mask_png(image: QImage, path: Path) -> None:
    """Write an 8-bit gray+alpha PNG. Gray is white; alpha is the coverage.

    The runtime tints by replacing RGB and keeping alpha, so a white mask and
    a black mask behave the same. Gray+alpha stays small in git.
    """
    image = image.convertToFormat(QImage.Format_ARGB32)
    width = image.width()
    height = image.height()
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # PNG filter: None
        for x in range(width):
            alpha = (image.pixel(x, y) >> 24) & 0xFF
            raw.append(255)
            raw.append(alpha)

    def chunk(tag: bytes, data: bytes) -> bytes:
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 4, 0, 0, 0)  # 8-bit gray+alpha
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", ihdr)
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


def suffix_for(scale: float) -> str:
    if scale == 1.0:
        return ""
    if scale == 1.5:
        return "@1.5x"
    if scale == 2.0:
        return "@2x"
    if scale == 3.0:
        return "@3x"
    raise ValueError(f"unsupported scale {scale}")


def render_toolbar() -> list[str]:
    names: list[str] = []
    svgs = sorted(ICON_DIR.glob("*.svg"))
    if not svgs:
        raise RuntimeError(f"no SVGs in {ICON_DIR}")
    for svg in svgs:
        names.append(svg.stem)
        for suffix, scale in TOOLBAR_SCALES:
            pixels = int(round(TOOLBAR_PX * scale))
            image = render_svg(svg, pixels)
            save_mask_png(image, ICON_DIR / f"{svg.stem}{suffix}.png")
    return names


def render_theme() -> None:
    for name, (logical, scales) in THEME_SPECS.items():
        svg = THEME_DIR / f"{name}.svg"
        if not svg.is_file():
            raise RuntimeError(f"missing theme SVG: {svg}")
        for scale in scales:
            pixels = int(round(logical * scale))
            image = render_svg(svg, pixels)
            save_mask_png(image, THEME_DIR / f"{name}{suffix_for(scale)}.png")


def tint(image: QImage, color: QColor) -> QImage:
    out = image.convertToFormat(QImage.Format_ARGB32)
    red, green, blue = color.red(), color.green(), color.blue()
    for y in range(out.height()):
        for x in range(out.width()):
            alpha = (out.pixel(x, y) >> 24) & 0xFF
            out.setPixel(x, y, (alpha << 24) | (red << 16) | (green << 8) | blue)
    return out


def paint_contact(path: Path, names: list[str]) -> None:
    """Dark row-block over a light row-block, labels under each glyph."""
    cell = 132
    icon_px = 48
    cols = 8
    theme_names = list(THEME_SPECS)
    rows_icons = (len(names) + cols - 1) // cols
    rows_theme = (len(theme_names) + cols - 1) // cols
    block_rows = rows_icons + rows_theme
    width = cols * cell + 24
    header = 36
    block_h = header + block_rows * cell + 8
    height = block_h * 2

    sheet = QImage(width, height, QImage.Format_ARGB32)
    painter = QPainter(sheet)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
    font = QFont("sans-serif", 8)
    painter.setFont(font)

    panels = (
        (QColor("#1c1c1e"), QColor("#f5f5f7"), "Dark tint"),
        (QColor("#f4f4f6"), QColor("#1c1c1e"), "Light tint"),
    )
    for index, (bg, fg, title) in enumerate(panels):
        top = index * block_h
        painter.fillRect(0, top, width, block_h, bg)
        painter.setPen(fg)
        painter.drawText(12, top + 24, title)

        def draw_row(items: list[str], directory: Path, row0: int) -> None:
            for i, name in enumerate(items):
                col = i % cols
                row = row0 + i // cols
                svg = directory / f"{name}.svg"
                # Theme glyphs are 10–16px in the UI. Draw them at 32px so a
                # chevron is still readable on the sheet.
                px = icon_px if directory == ICON_DIR else 32
                image = tint(render_svg(svg, px), fg)
                x = 12 + col * cell + (cell - px) // 2
                y = top + header + row * cell + 4
                painter.drawImage(x, y, image)
                painter.setPen(fg)
                label = name
                painter.drawText(12 + col * cell, y + px + 14, cell, 16, Qt.AlignmentFlag.AlignHCenter, label)

        draw_row(names, ICON_DIR, 0)
        draw_row(theme_names, THEME_DIR, rows_icons)

    painter.end()
    path.parent.mkdir(parents=True, exist_ok=True)
    if not sheet.save(str(path), "PNG"):
        raise RuntimeError(f"failed to save contact sheet: {path}")


def main() -> int:
    # QSvgRenderer and QFont need a Qt GUI application, even offscreen.
    # Pass an empty argv so Qt does not consume this script's flags.
    app = QGuiApplication([])
    del app

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--contact",
        type=Path,
        help="also write a dark/light contact sheet to this PNG path",
    )
    args = parser.parse_args()

    names = render_toolbar()
    render_theme()
    print(f"rendered {len(names)} toolbar icons x {len(TOOLBAR_SCALES)} scales")
    print(f"rendered {len(THEME_SPECS)} theme indicators")
    if args.contact:
        paint_contact(args.contact, names)
        print(f"contact sheet: {args.contact}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
