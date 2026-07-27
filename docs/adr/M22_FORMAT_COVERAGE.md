# ADR-M22.2: Broaden Decode Format Coverage via the Decoder Seam

## Status

Proposed (DRAFT, companion to `docs/rfc/M22_PRODUCT_POLISH.md` §F2)

## Context

`QtDecoder::canDecode` matches a hard-coded 6-entry list
(`jpg/jpeg/bmp/png/tif/tiff`). Qt can decode more formats
(`QImageReader::supportedImageFormats()` → WebP, GIF, and HEIF/AVIF when the
platform ships the plugins) but MViewer never offers them.

## Decision

`QtDecoder::extensions()` / `canDecode()` derive the claim set from
`QImageReader::supportedImageFormats()` (lower-cased) instead of the static
list. `RawDecoder` stays registered first and keeps owning RAW extensions, so
RAW preview behavior is unchanged. `DecoderRegistry` internals are **not**
modified — only the contents of `QtDecoder`'s claim list change through its
existing `extensions()` API.

## Rationale

- Every Qt-decodable format gains first-class decoding + M6 metadata for free.
- Stays strictly within the frozen boundary (DecoderRegistry untouched).
- `QtFallbackDecoder` remains the last safety net; no behavior regression for
  the historical 6 formats.

## Consequences

- ✅ WebP/GIF (and HEIF/AVIF when Qt plugins present) open with metadata.
- ✅ Future Qt codec additions are picked up automatically.
- ❌ Formats Qt cannot decode (e.g. EXR) still need a dedicated `IDecoder`
  (separate RFC, out of scope here).

## Related

- RFC M22 §F2
- `src/core/image/decoder/QtDecoder.cpp`, `DecoderRegistry.cpp` (frozen — not changed)
