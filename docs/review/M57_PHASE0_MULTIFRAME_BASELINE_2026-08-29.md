# M57 Phase 0 — Multi-frame / Multi-page baseline

Date: 2026-08-29
Runtime: Qt 6.10.3, Windows Release build
Fixture owner: `m57_multiframe_decoder_tests`

## Scope and method

The baseline was recorded before the M57 sequence capability was introduced.
The test creates deterministic, local fixtures at runtime, then probes them
with the deployed Qt image plugins and the existing path-only image pipeline.
The fixtures are not checked into the repository and are removed by the test.

The three primary fixtures are:

- `animated_3frame.gif`: 4×4, red/green/blue frames, 100/200/300 ms delays.
- `animated_3frame.webp`: 4×4 lossless red/green/blue animation, 100/200/300 ms
  ANMF durations.
- `multipage_3page.tiff`: three 4×4 RGB pages, red/green/blue.

The GIF and WebP fixtures use per-frame content and timing. The TIFF fixture
uses separate IFDs and page-local pixel offsets, so page identity is explicit.

## Observed Qt handler baseline

The direct `QImageReader` probe produced this Release-build output:

```text
baseline gif format=gif imageCount=3 supportsAnimation=1 loopCount=-1
baseline gif frame=0 jump=0 read=0 delay=100
baseline gif frame=1 jump=0 read=0 delay=100
baseline gif frame=2 jump=0 read=0 delay=100
baseline webp format=webp imageCount=3 supportsAnimation=1 loopCount=-1
baseline webp frame=0 jump=0 read=0 delay=0
baseline webp frame=1 jump=0 read=0 delay=0
baseline webp frame=2 jump=0 read=0 delay=0
baseline tiff format=tiff imageCount=3 supportsAnimation=0 loopCount=0
baseline tiff frame=0 jump=1 read=1 delay=0
baseline tiff frame=1 jump=1 read=1 delay=0
baseline tiff frame=2 jump=1 read=1 delay=0
```

This is an important plugin behavior finding: qgif and qwebp advertise a
frame count and animation support, but this runtime does not make
`jumpToImage()` a valid selection operation for these fixtures. qtiff supports
random page selection for the three-page fixture. Animation timing is exposed
through the sequential reader's `nextImageDelay()`.

## Existing application behavior before M57

The application contract was `path → ImageData`; it had no frame/page index in
`ImageMetadata`, `ImageFrame`, repository keys, Compare sessions, workspace
state, or export jobs. Consequently:

- Gallery could list GIF/WebP/TIFF as one filesystem item when the installed
  format capability admitted the file.
- Thumbnail and Preview were single-image decodes, effectively representative
  frame/page zero.
- Viewer opened the first decoded image and had no playback or page control.
- Compare, Analyze, Pixel Inspector, and export consumed that same path-only
  frame with no way to name another frame/page.
- Workspace restore could restore a path, but could not restore a frame/page or
  playback state.

In other words, GIF/WebP were container-readable but not multi-frame-aware in
the product model; multi-page TIFF was page-readable by qtiff but exposed as a
single first-page image by the application model. The old behavior must not be
described as zero-latency animation: animation navigation was unsupported.

## Support truth and implementation consequence

The existing format listings mixed static decode support with the presence of
an image plugin. M57 therefore adds a separate `FrameSequenceInfo` capability
and keeps the gallery item singular. Static formats continue through the same
repository path with a one-frame sequence. GIF/WebP use bounded sequential
frame selection when the Qt plugin requires it; TIFF uses page selection with
sequential fallback. The deployment check now treats qgif, qwebp, and qtiff as
runtime requirements for the M57 real-plugin test.

## Known baseline limitations

This Phase 0 record is a deterministic offscreen/runtime audit. Physical
native-GUI feel, target-machine installer execution, 100MP multi-page TIFF RSS,
and long-session playback were not available in this environment and remain
explicit manual qualification items in the M57 closure report.
