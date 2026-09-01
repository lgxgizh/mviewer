# M59 — Color-managed presentation contract

**Status:** Implemented in v1.0.16 (2026-09-02)

## Problem and boundary

Before M59, display conversion was a single source-ICC-to-sRGB operation. It
was deterministic, but it was not a physical-monitor color-management
contract: an ICC-tagged `QImage`, a `QPainter`, or an OpenGL framebuffer does
not by itself mean that the desktop compositor applied the monitor profile.
Metadata also diverged between the ordinary decoder and `FrameSequenceReader`:
bit depth could mean packed pixel depth, EXIF orientations 5/7 were ambiguous,
and sequence metadata could lose the embedded profile.

M59 defines one contract for every presentation path:

1. **Source truth is immutable.** Decode, analysis, Pixel Inspector, compare
   math, export, and persistence keep source pixels and source metadata. A
   display conversion always operates on a copy.
2. **Untagged/invalid source profiles mean sRGB.** A valid embedded ICC is
   parsed once into canonical metadata (`sRGB`, `AdobeRGB`, `DisplayP3`, or
   unknown) and retained as base64 sidecar data. Invalid ICC bytes never make
   the raster disappear.
3. **Presentation is direct source → target.** `QtConvert` converts directly
   to a supplied `DisplayColorContext`; the legacy overload explicitly uses
   an sRGB target. Invalid or unavailable target profiles fall back to sRGB.
4. **Target identity is explicit.** `DisplayColorContext` carries profile
   bytes, a fingerprint, and a monotonically changing generation. The
   `fingerprint@generation` cache key is part of every display-raster, tile,
   and warm-result request. Source/image caches never become monitor-profile
   caches.
5. **Monitor discovery is a UI concern.** On Windows,
   `DisplayColorContextProvider` reads the profile associated with the current
   `QWindow` device context using `GetICMProfileW`. Missing APIs, profile
   errors, and non-Windows hosts return an explicit sRGB context. Viewer and
   Compare refresh this context on resize/display changes.
6. **CPU/GPU parity.** Both paths use the same CPU `QtConvert` result before
   upload. The optional GPU path is transport/blit only; it does not invent a
   second transfer-function implementation.
7. **Bit depth is source metadata.** Metadata reports bits per channel from
   the source format (including 16-bit TIFF). The current `ImageData` display
   buffer remains 8-bit normalized, so conversion to the SDR presentation
   buffer is explicit and does not rewrite analysis/source data.

The shared semantics live in `core/image/QtMetadataSemantics.h`; public
domain/core headers remain Qt-free. Static, LOD/region, and sequence readers
all use the same orientation, channel, bit-depth, and ICC rules.

## Qualification and known physical boundary

M59 adds deterministic metadata, conversion, CPU/GPU-byte-parity, malformed
ICC, static/sequence, 16-bit probe, and real `ThumbnailPanel` latest-wins
regressions. The 10k and 50k Browse evaluator tiers run against the production
query function; a 660-file temporary directory drives the real asynchronous
panel path.

The test host cannot prove a particular physical panel's LUT, HDR policy,
mixed-DPI topology, or compositor behavior. Those rows remain
**MANUAL/BLOCKED** and must be qualified on representative Windows hardware.
