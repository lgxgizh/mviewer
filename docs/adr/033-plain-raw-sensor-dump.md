# ADR 033 — Plain `.raw` sensor dumps (scoped format-freeze exception)

Date: 2026-11-11
Status: Accepted

## Context

Camera and ISP engineers capture headerless sensor dumps and save them as
`.raw`: Bayer (or monochrome) samples straight from the sensor, with no
container, no metadata and no embedded preview. Until now MViewer listed
`.raw` among the camera RAW extensions and sent it to LibRaw, which cannot
read these files, so they showed as broken thumbnails.

Like raw YUV (ADR 032), a sensor dump only becomes a picture once the viewer
knows its size, bit depth, storage layout, black level and colour filter
pattern. The user only needs "roughly what the scene looks like", so a simple,
fast pipeline is enough.

## Decision

1. **Scoped exception to the format freeze.** Headerless Bayer / mono `.raw`
   viewing is in scope. The rest of the post-M22 freeze, and ADR 032's
   limits, stay in force.
2. **`.raw` is always a plain sensor dump.** `raw` is removed from the camera
   RAW extension list (`rawExtensions()`), so `.raw` never reaches LibRaw,
   the RAW metadata reader or the RAW thumbnail path. Panasonic / Leica `.raw`
   camera files are therefore not supported; `.rw2` and all other
   camera RAW extensions are unchanged. In the browse type filter, `.raw`
   has its own group, "Sensor RAW (.raw)", which is checked (visible) by
   default; the hidden-by-default "RAW" group keeps camera RAW only (amended
   after 1.0.174: under "Other formats" the dumps did not follow any RAW
   checkbox). Saved filter choices never name the new group, so existing users
   see it visible without a migration.
3. **Clean-room implementation** from public knowledge only: MIPI CSI-2
   RAW10/RAW12 byte packing, the IEC 61966-2-1 sRGB transfer function,
   textbook bilinear demosaicing and gray-world / white-patch white balance.
   No dcraw, LibRaw, RawTherapee or other GPL source is copied or used as a
   template. libyuv and LibRaw are not used for this path.
4. **Module layout.** Qt-free core in `src/core/image/rawplain/`:
   - `PlainRawFormat` — parameters, canonical text form, size math (checked
     `int64_t`), file-name size inference, file-size validation and guesses.
   - `PlainRawUnpack` — bytes → one `uint16` sample per pixel.
   - `PlainRawRender` — black/white level, white balance, digital gain,
     bilinear demosaic, sRGB gamma → RGB24; a 2×2-quad preview path for
     thumbnails.
   - `PlainRawFile` — parameter store (global defaults + per-file overrides),
     file resolution and reading, cache-key suffix, pixel sample text.
   - `PlainRawDecoder` (`src/core/image/decoder/`) — `IDecoder`.
5. **Registration.** `PlainRawDecoder` is one more entry in
   `DecoderRegistry::resetToDefaults()`, right after `YuvDecoder`, for the
   same reasons as ADR 032 §4. The registry API stays frozen.
6. **Parameters.**
   - Size: `WIDTHxHEIGHT` in the file name (case-insensitive `x`, also `×`),
     checked against the file size. When the size does not match, common
     bit-depth / packing combinations (16-bit container, MIPI RAW10/12, 8-bit)
     are tried and the first exact match is used; otherwise the viewer shows a
     hint with the expected and actual byte counts.
   - Bit depth 8, 10, 12, 14, 16. Storage: 16-bit container LSB-aligned
     (**default**), 16-bit MSB-aligned, MIPI RAW10, MIPI RAW12, 8-bit. Byte
     order (little-endian default) for 16-bit containers.
   - Header offset and row stride (both default 0 = none / tightly packed).
   - Black level (one value, in the chosen bit depth) and white level
     (default 2^bits − 1).
   - CFA: RGGB, BGGR, GRBG, GBRG, Mono.
   - White balance: none, gray world (default), white patch (brightest
     non-clipped region), manual R/B gains.
   - Digital gain: **default 1×**, presets 1/2/4/8× and a free value
     (0.125–64×). No automatic brightening by default.
   - sRGB gamma on by default.
7. **Where settings live.** Preferences → RAW holds the global defaults
   (`plainRaw/defaults` in QSettings, canonical text). Right-click →
   "RAW Parameters..." on a `.raw` file opens a dialog that overrides the
   defaults for that file for the current session only; "Use defaults" drops
   the override. Every change re-renders the file and its thumbnail (the
   cache key carries the effective parameters).
8. **Views.** `.raw` is read-only: rotate / flip in the normal view stay
   disabled (the file is never rewritten). Compare mode's temporary view
   rotation works as for any image. The pixel readout appends the stored
   sample value and its CFA colour.
9. **Safety.** Dimensions ≤ 65536, ≤ 2^28 samples, all byte math checked;
   short files are rejected with a reason; parsers never throw.

## Consequences

- `rawExtensions()` loses `raw`; tests that list it are updated.
- `DecoderRegistry::resetToDefaults()` gains one entry; lineup counts include
  `PlainRawDecoder`.
- `ImageRepository` and the thumbnail cache append
  `plainRawCacheKeySuffix()` ("|praw=<params>") to `.raw` keys.
- New logic lives in new translation units; `compareworkspace.h`,
  `imageviewer.h` and `thumbnailpanel.h` are not grown.
