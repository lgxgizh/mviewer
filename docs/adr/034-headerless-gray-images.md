# ADR 034 — Headerless grayscale `.gray` / `.grey` images (scoped format-freeze exception)

Date: 2026-11-12
Status: Accepted

## Context

Imaging and test tools often write a single-channel image as raw bytes with no
header: one byte per pixel, a 16-bit container, or 1-bit black/white bitmaps,
saved as `.gray` or `.grey`. Like plain `.raw` sensor dumps (ADR 033), such a
file only becomes a picture once the viewer knows its size and storage. Until
now `.gray` was one of the default raw-YUV extension rules (ADR 032, pixel
format `gray`) and `.grey` was not listed at all.

## Decision

1. **Scoped exception to the format freeze.** Headerless single-channel
   grayscale viewing for `.gray` and `.grey` (any letter case) is in scope.
   The rest of the post-M22 freeze and the limits of ADR 032/033 stay in force.
2. **Reuse the plain-dump module.** Gray files go through the ADR 033 code
   (`src/core/image/rawplain/`, `PlainRawDecoder`) as a mono-only kind
   (`DumpKind::Gray`): no Bayer pattern, no white balance (forced Mono / None
   even when an override says otherwise). Thumbnails, Details dimensions
   (source probe), compare, pixel readout (`GRAY=<value> (<bits>-bit)`), the
   cache-key suffix and override relocation are shared with `.raw`. An
   override moves or copies with a file only when the old and new names are
   the same kind.
3. **Never LibRaw, no longer YUV.** `.gray` / `.grey` are not camera RAW
   extensions. The `gray → gray` default YUV extension rule is removed and the
   YUV extension map refuses `gray` / `grey` (like `y4m`), so a saved user
   rule for them is ignored. `.y` keeps mapping to YUV `gray`. Multi-frame
   gray YUV sequences named `.gray` must be renamed to `.y` or `.yuv`.
4. **Storage.**
   - **8-bit (default):** one byte per pixel.
   - **16-bit:** two bytes per pixel, value in the low bits (LSB-aligned
     container, default) or high bits (MSB-aligned), significant bits 8–16,
     little-endian by default with a byte-order option. As for `.raw`,
     samples above the configured depth are never wrapped: with the defaults
     the detected depth is used, with an override they are clipped.
   - **1-bit:** 8 pixels per byte, **MSB first** (the first pixel is bit 7 of
     the first byte), every row padded to a whole byte (row bytes =
     ⌈width / 8⌉), value 1 = white, 0 = black.
   - Header offset and row stride as for `.raw`.
5. **Size.** `WIDTHxHEIGHT` in the file name, as for `.raw`: when a name holds
   several, the one consistent with the file size wins. When the size does
   not match, 8-bit, 16-bit and 1-bit storage are tried (no MIPI packings).
   Without a usable size the file shows a placeholder thumbnail and the user
   sets the size in the per-file dialog.
6. **Rendering.** `(v − black) / (white − black)`, clamped, × brightness gain
   (default 1×), optional sRGB gamma, R = G = B. **Gamma is off by default**:
   8-bit and most 16-bit gray files already hold display values, so the
   default shows the stored codes unchanged (8-bit with gain 1 is the
   identity). Users with linear sensor data can turn sRGB gamma on. No
   invert option.
7. **Settings.** A separate set from `.raw`: global defaults in
   **Tools → Preferences → Gray** (QSettings `grayRaw/defaults`, same canonical
   text as `plainRaw/defaults`), and **right-click → Gray Parameters...** for a
   session-only per-file override ("Use defaults" clears it). Only gray
   parameters are shown: size, bit depth / storage, byte order, offset /
   stride, black / white level, gain, gamma.
8. **Browse filter.** Own type group **"Gray images (.gray/.grey)"**
   (zh_CN 灰度图 (.gray/.grey)), **unchecked (hidden) by default**. Existing
   saved filter choices get the group added to their hidden list once
   (migration), so it is hidden for them too until they check it.
9. **Views.** Read-only like `.raw`: rotate / flip in the normal view are
   disabled; compare mode's temporary rotation works.

## Consequences

- `PlainRawDecoder::extensions()` lists `raw`, `gray`, `grey`; `isPlainRawPath`
  covers all three and `isGrayRawPath` / `dumpKindForPath` tell them apart.
- `Packing::Bits1` (token `bits1`) is added to the shared format.
- YUV tests that used a `.gray` file name move to `.y`.
- New logic lives in existing rawplain units or new translation units;
  `compareworkspace.h`, `imageviewer.h` and `thumbnailpanel.h` are not grown.
