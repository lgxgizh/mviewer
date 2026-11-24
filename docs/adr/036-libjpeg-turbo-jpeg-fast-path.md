# ADR 036 — libjpeg-turbo JPEG decode fast path

Date: 2026-11-23
Status: Accepted

## Context

JPEG is the most common input format, and the decode path was:

1. `QImageReader::read()` decodes with the IJG-derived plugin into a `QImage`
   (32-bit ARGB).
2. `toImageData()` copies that into an `ImageData` (RGB24) - a second full-image
   pass.
3. For the LOD path, `QImageReader::setScaledSize()` asks Qt to rescale.

Measured on this machine with `mviewer_bench` (synthetic 3840x2160 JPEG,
`Decoder::decodeScaled(path, 3840)`, 12 samples, p50): **170 ms**, against a
`perf_baseline.json` entry of 170 ms. The review's P1-05 target was < 45 ms.

## Decision

1. **libjpeg-turbo 3.1.0, static only.** Fetched at configure time exactly like
   LibRaw (`cmake/LibJpegTurbo.cmake`), pinned by SHA256, with the same offline
   `third_party/` cache. It is built as its own CMake project via
   `ExternalProject_Add` - libjpeg-turbo refuses to be embedded as a
   subdirectory - and installed into the build tree. Static linking means no
   extra DLL in the portable zip or the installer.
   Opt-outs: `-DMVIEWER_WITH_TURBOJPEG=OFF` (do not fetch) and
   `-DMVIEWER_USE_TURBOJPEG=OFF` (fetch but do not use).
2. **`core/image/decoder/JpegTurbo.{h,cpp}`** wraps `tjDecompressHeader3` /
   `tjDecompress2`, writing `TJPF_RGB` (or `TJPF_GRAY`) straight into an
   `ImageData`. Every entry point is safe in a build without the library:
   `hasTurboJpeg()` returns false and `decodeJpegTurbo()` returns a null image.
   The **accurate** SIMD IDCT is used (`flags = 0`), not `TJFLAG_FASTDCT`:
   the fast flag buys about 9% and costs up to 8 codes of accuracy, which is
   visible against Qt's decode of the same file.
3. **Only baseline 8-bit RGB / greyscale JPEGs take the fast path.** CMYK, YCCK
   and anything else return null and fall back to Qt. 16-bit JPEGs are excluded
   in `decodeFullResult` (`sixteen`), because the raw16 path needs Qt's
   normalised raster.
4. **An embedded ICC profile keeps Qt's decode.** The decoded pixels carry no
   colour space, and the colour-managed display path plus the metadata centre
   read `meta.iccProfile`, so a JPEG containing an `ICC_PROFILE` APP2 segment
   falls back. The check is one `std::search` over the compressed bytes; a false
   positive only costs the fast path.
5. **EXIF-rotated JPEGs keep Qt's decode on both paths.** libjpeg-turbo scales
   the STORED raster while `QImageReader::size()` reports the DISPLAYED
   geometry, so a rotated source would get its scale factor from the wrong axes;
   the transform itself needs Qt's image operators anyway. The m27 preview
   fixtures pin the exact preview edge, and they caught this.
6. **Scaled decode picks the built-in factor whose output edge is CLOSEST to
   `maxEdge`, accepted only within 1/8 of the request.** libjpeg-turbo scales by
   powers of two, so it can coincide with an arbitrary request only by luck.
   Accepting a wide miss either hands the viewer a coarser raster than it asked
   for (a zoom that re-materialized smaller visibly degrades -
   `m47_compare_lod_tests` C3) or a larger one than the caller's cap allows
   (`PreviewPanel::kPreviewMaxEdge`, pinned by `m27_lifetime_tests`). When no
   factor is close enough, Qt performs its exact rescale; full-resolution
   requests always take 1/1 and are therefore always fast.
7. **The compressed file is read with one sized `ifstream::read`**, not
   `istreambuf_iterator`.

## Consequences

- Measured B11 after the change: **104 ms p50** (170 ms baseline, -39%). The
  review's < 45 ms is not reachable at full 4K resolution: the IDCT over
  8.3 M pixels is the floor, and the remaining time is roughly one sized file
  read plus one SIMD IDCT plus one 24 MB write. A LOD request whose edge aligns
  with a power-of-two scale does reach that range because libjpeg-turbo skips
  whole DCT blocks.
- `m47_source_tests` T5 compares the clip path (still Qt's scanline reader)
  against the full decode (libjpeg-turbo) and now allows a 2-code difference:
  two different IDCTs on a lossy codec are not bit-identical by nature.
- `core/image/ImageBuffer.h` is untouched; the only new core source is the
  decoder wrapper, and `QtConvert.h` remains one of the two Qt-allowed core
  adapter headers (R5 is unaffected).
- A JPEG with an ICC profile or an exotic colour space is decoded by Qt exactly
  as before, so `m59_color_managed_tests` and `m59_phase0_metadata_tests` pin
  the fallback.
- `tests/core/test_jpeg_turbo.cpp` pins correctness against Qt's own decode
  (mean absolute per-channel difference <= 2 codes), the greyscale case, the
  scaled-factor choice, and the null-return fallback contract.
