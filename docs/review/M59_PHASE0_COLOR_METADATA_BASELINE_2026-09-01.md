# M59 Phase 0 — source metadata truth baseline (2026-09-01)

## Scope

M59 closes the contract between source pixels, source metadata, and the
presentation target.  Phase 0 freezes the pre-change behavior before any
implementation work.  The release-gate executable is
`m59_phase0_metadata_tests`; it compares the cheap `MetadataReader` probe,
`QtDecoder` full decode, and `FrameSequenceReader` static-frame path against
the same fixtures.

The authoritative expectations are:

- `bitDepth` is bits per channel (8 for the ordinary RGB fixtures), never the
  packed `QImage::depth()` value;
- EXIF orientation is the exact 1–8 value, including transpose (5) and
  transverse (7);
- `colorSpace`, ICC presence, and the base64 ICC bytes are equivalent across
  probe, full decode, and frame-sequence metadata.

## Baseline command

```text
powershell -ExecutionPolicy Bypass -File D:\mviewer\build.ps1 Release
build_msvc\bin\m59_phase0_metadata_tests.exe
```

The target compiled successfully before the M59 implementation.  Its
deterministic output was:

```text
FAIL: probe reports bits per channel, not packed depth
FAIL: full decode reports bits per channel, not packed depth
FAIL: probe reports canonical AdobeRGB label
FAIL: probe preserves exact EXIF orientation 1-8
FAIL: probe and full decode agree on EXIF orientation
FAIL: probe preserves exact EXIF orientation 1-8
FAIL: probe and full decode agree on EXIF orientation
FAIL: probe preserves exact EXIF orientation 1-8
FAIL: probe and full decode agree on EXIF orientation
FAIL: probe preserves exact EXIF orientation 1-8
FAIL: probe and full decode agree on EXIF orientation
FAIL: probe preserves exact EXIF orientation 1-8
FAIL: probe and full decode agree on EXIF orientation
FAIL: frame-sequence metadata uses the same color-space label
FAIL: frame-sequence metadata preserves the embedded ICC profile
FAIL: frame-sequence metadata exposes identical ICC bytes
FAIL: frame-sequence preserves exact EXIF orientation
FAIL: frame-sequence preserves exact EXIF orientation
FAIL: frame-sequence preserves exact EXIF orientation
M59 Phase 0 failures: 19
```

The failures are intentional RED evidence, not an infrastructure failure.
The fixture directory and Qt image plugins were present, the new target
linked, and the executable reached every assertion.

## Existing boundaries carried forward

M48 already proves that source-backed Viewer and Compare display paths apply an
embedded ICC profile without mutating source pixels.  M57 proves frame/page
identity, duration, and persistence.  M58 proves the large-directory query
snapshot/latest-wins data path.  M59 extends those contracts with one metadata
truth source and an explicit presentation target; it does not change the
source/analysis pixel contract.

Native physical-monitor ICC retrieval, HDR output, and mixed-DPI behavior stay
`MANUAL/BLOCKED` until a Windows-host qualification can provide reproducible
hardware evidence.  The application contract therefore requires an explicit
target context and a deterministic sRGB fallback.
