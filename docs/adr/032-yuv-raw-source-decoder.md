# ADR 032 — Raw YUV / Y4M viewing and compare (scoped format-freeze exception)

Date: 2026-11-03
Status: Accepted

## Context

Algorithm and codec engineers inspect raw `.yuv` dumps and `.y4m` clips and
compare an encoder output against its source. MViewer had no YUV decoder: the
post-M22 direction in `AGENTS.md` froze new formats, and
`docs/acceptance/user_workflow.md` listed YUV decode as out of scope.

Raw YUV is different from a normal image format:

- A `.yuv` file has no header. Width, height, pixel format, matrix and range
  must come from the file name, the folder name, the file size, or the user.
- One file holds many frames.
- Engineers need the stored Y/U/V sample values and per-plane metrics, not only
  an RGB picture.

YUView (GPL-3.0) was studied as a reference product. Only its observable
behaviour and the public specifications it implements were used. It also has
known weaknesses to avoid: chroma size computed with `w / 2` (wrong for odd
sizes), 64-bit frame sizes stored in `int`, and a y4m parser that ignores
`mono` and `XCOLORRANGE` and indexes a truncated last frame.

## Decision

1. **Scoped exception to the format freeze.** YUV viewing and YUV compare are
   in scope. This is the only new format family the exception allows. The
   rest of the post-M22 freeze stays in force.
2. **Clean-room implementation.** All code is written from public
   specifications: ITU-R BT.601, BT.709 and BT.2020 (matrix coefficients and
   quantization ranges), the YUV4MPEG2 header format, and the common FFmpeg
   pixel-format names. No YUView source (or any other GPL source) is copied,
   transliterated or used as a template. MViewer's licence is unchanged.
3. **Module layout.** The Qt-free YUV core lives in `src/core/image/yuv/`:
   - `YuvFormat` — format descriptor, plane geometry, overflow-checked byte
     math (`int64_t`), canonical text form such as
     `1920x1080:yuv420p10le:bt709:limited:left`.
   - `YuvParamInference` and `Y4mHeader` — parameter inference and a strict
     y4m header parser.
   - `YuvUnpack` and `YuvToRgb` — packed and semi-planar input normalized to
     planar samples, then a scalar reference conversion to RGB24, with SSE2 and
     AVX2 paths chosen at run time.
   - `YuvDecoder` (`src/core/image/decoder/`) — implements `IDecoder` and
     `ISourceImageCapabilities`.
   Multi-frame access goes through a small hook in `FrameSequenceReader`.
   Parameter editing is a UI dialog that writes a per-file parameter store.
   Per-plane metrics live in `src/core/analysis/YuvMetrics`.
4. **Registration.** `YuvDecoder` is added to
   `DecoderRegistry::resetToDefaults()`, after `RawDecoder` and before
   `QtDecoder`. This one-line change to the frozen registry was chosen over
   startup `registerDecoder()` for three reasons:
   - Many tests and `SelfTest` call `resetToDefaults()` to restore the stock
     lineup. A startup-only registration would vanish in those runs.
   - `ImageFormats` derives the browse and open-dialog suffix list from the
     registry. The default lineup must therefore include YUV for `.yuv` and
     `.y4m` to be listed everywhere, including headless tests.
   - It needs no Application-layer bootstrap code and keeps the architecture
     gate unchanged.
   The registry API and ordering rules are not otherwise changed.
5. **Parameter resolution order:**
   1. the y4m header;
   2. parameters the user saved for the file;
   3. the file name (`_1920x1080_`, `1080p`, `cif`, `yuv420p10le`, `nv12`,
      `bt2020`, `full` …);
   4. the parent folder name;
   5. the file size against a list of common resolutions.
   When the matrix or range cannot be inferred, the default is **BT.709,
   limited range** for every resolution. Chroma siting defaults to left (MPEG-2
   style) for 4:2:0.
6. **Pixel formats (first release).**
   - Planar: 4:2:0 (I420, YV12), 4:2:2 and 4:4:4 at 8, 9, 10, 12, 14 and
     16 bits, little or big endian.
   - Semi-planar: NV12, NV21, NV16, NV24; P010, P016, P210 and P410
     (MSB-aligned).
   - Packed 4:2:2: YUYV (YUY2), UYVY, YVYU.
   - Luma only: gray at 8–16 bits.
   - Y4M: `C420jpeg`, `C420paldv`, `C420mpeg2`, `C422`, `C444`, `Cmono` and the
     `p10`, `p12` and `p16` variants, plus `XCOLORRANGE=FULL|LIMITED`.
   Deferred: V210, 4:1:1, 4:4:0 and 4:1:0, AYUV, interlaced field handling, a
   correlation-based resolution guess, and GPU conversion.
7. **SIMD.** The scalar converter is the golden reference. SSE2 and AVX2 paths
   for I420, NV12 and P010 are written in-house and chosen at run time through
   `CpuFeatures`. Their output must match the scalar path bit for bit. libyuv
   is not added.
8. **Browse thumbnails.** `.yuv` and `.y4m` show the first frame as their
   thumbnail. When parameters cannot be inferred, the decoder returns no pixels.
   The thumbnail then uses the standard placeholder and the viewer offers the
   parameter dialog. The decoder never guesses an arbitrary size.
9. **Safety.**
   - All size math uses checked `int64_t` arithmetic.
   - A frame is capped at 1 GiB, and each dimension at 65536.
   - A truncated last frame is not counted as a frame.
   - Odd sizes round chroma dimensions up.
   - The y4m and file-name parsers reject malformed input without undefined
     behaviour and are fuzz-tested.
10. **Compare.** Two YUV sources are compared per plane (Y, U, V: MSE, PSNR,
    SSIM, plus a 6:1:1 and a 1:1:1 average) when their sizes, subsampling and
    bit depths match. Otherwise compare falls back to the existing RGB metrics
    and the UI states why.
    Implemented (batch 8) as `core/analysis/YuvMetrics` plus a separate
    "Compare YUV (PSNR / SSIM)" dialog for two selected files; the compare
    workspace itself keeps its RGB metrics.
11. **Layout picker and extension mapping (amendment, 1.0.163).** NV12 and
    I420 frames have the same size, so a semi-planar file inferred as planar
    decodes silently wrong. The parameters dialog therefore exposes the format
    as separate controls (subsampling, layout, component order, bit depth,
    byte order, MSB/LSB alignment), and Preferences → YUV holds a user-editable
    extension → pixel-format map (`core/image/yuv/YuvExtensionMap`, persisted
    as `yuv/extensionMap`). Mapped extensions are YUV for decoding, browsing,
    sequence candidates and thumbnails. Inference priority: y4m header >
    saved per-file parameters > file-name tokens > extension map > defaults.
    Unsaved raw files key their cache entries on a fingerprint of the map, so
    editing it never serves stale pixels.

## Consequences

- `AGENTS.md` and `docs/acceptance/user_workflow.md` now list YUV as in scope
  by this ADR. Other new formats still need their own ADR.
- `DecoderRegistry::resetToDefaults()` gains one entry. Tests that count the
  default lineup must include `YuvDecoder`.
- `ImageRepository` appends `yuvCacheKeySuffix()` ("|yuv=<params>") to the
  cache key of a `.yuv`/`.y4m` path, and drops a remembered key whose suffix no
  longer agrees, so editing a file's parameters never serves stale pixels.
  `.yuv` and `.y4m` are frame-sequence candidates (each frame is a page).
- New files stay within the complexity caps. YUV logic goes into new
  translation units, not into `compareworkspace.h`, `FrameSequence.cpp` or
  `AnalysisEngine_metrics.cpp`.
- Work lands as a sequence of small PRs. Each one has its own unit tests,
  golden conversion vectors where relevant, and a CHANGELOG entry.
