# ADR 040: bounded HDR, OpenEXR, and PFM decoding

- Status: proposed (B06 review; not implemented until native approval)
- Date: 2026-11-25

## Context and source adjudication

`HdrDecoder` tone-maps uncompressed OpenEXR scanlines and PFM images to RGB24.
The current decoder already stores each scanline offset as `uint64_t` and checks
a `file.size() - offset` span. The report's 32-bit offset claim does not describe
this tree. What remains is that the `uint64_t` value is narrowed to `size_t`
before that proof, so a 32-bit `size_t` host can truncate a high offset into a
small index. `kMaxHdrEdge` (16384) and `kMaxHdrPixels` (64 Mi pixels) already
exist and are not raised here. Grayscale PFM already writes the same sample into
R, G, and B; that red-channel defect is not reopened.

The defects that are still present are: `dataWindow` subtraction and the
following add-one are evaluated in `int`; the channel list has no count cap
before the scanline allocation; `readLine` is an unbounded `std::getline`;
`readExr` slurps the whole file; the PFM scale is not required to be finite and
non-zero; and the PFM flip allocates a second full RGB buffer.

## Decision

Widen both `dataWindow` edges to `int64_t`, compute `hi - lo + 1` in that
width, reject a non-positive span or a span above `kMaxHdrEdge`, and only then
narrow to `int`. A negative origin that still fits the edge ceiling stays valid.

Cap an OpenEXR channel list at 64 names. The cap is applied before each
`push_back` and checked again before the scanline buffer is allocated. The
scanline buffer is also limited to `kMaxHdrEdge * 64` floats. Before that buffer
or the RGB image is allocated, every offset is proved inside the file as
`uint64_t`, and each stored `dataSize` must cover the declared samples and fit
in the bytes that remain after the 8-byte scanline header. Overlapping offsets
remain accepted when each stored size fits. The `uint64_t` offset is cast to
`size_t` only after that proof. `dataSize` is compared as `uint64_t` with the
remaining span, not added to 8 in `size_t`.

`kMaxHdrEdge` and `kMaxHdrPixels` stay at their current values. This change does
not introduce a 256 MiB ceiling that would reject a legal RGB float image under
the existing pixel cap. The OpenEXR read limit is the encoded size of one
uncompressed RGB float image at that pixel cap, plus an offset table and
per-scanline headers at the edge cap, plus 1 MiB of attribute slack:

`kMaxHdrPixels * 3 * 4 + kMaxHdrEdge * 16 + 1048576` = 806,617,088 bytes.

The opened stream is sized with seek/tell. A larger file is rejected before
allocation. The read is exact, then one extra byte is probed and not stored.
Growth and a short read are errors. An unseekable stream fails closed. There is
no whole-file iterator fallback. A 64-channel 1x1 file still fits. A many-AOV
file can still fail this byte cap when its encoded size exceeds the RGB-float
budget, including on a 32-bit `size_t` host where the whole file must fit in
`size_t`. The existing PFM pixel check, which multiplies by the channel count,
is unchanged. The existing EXR pixel check, which is `width * height` only, is
unchanged.

PFM header lines store at most 4096 characters. A newline at that length is
accepted. A longer line is rejected without reading the rest. A final line
without a newline is kept, matching `std::getline`. The scale must be a finite
non-zero `double`, and the `float` factor after `fabs` must also be finite and
non-zero. That rejects 0, -0, NaN, infinity, and magnitudes that overflow or
underflow the factor actually applied. Ordinary scales such as 1, -1, and 2
remain. The sign still selects endianness. One raw row is read and stored
directly into the flipped destination row. No second full RGB buffer is
allocated. Grayscale samples are still written to R, G, and B.

Names stay truncated to 7 characters plus NUL. UINT samples and non-unit
sampling stay accepted. An incomplete channel record still ends the list.
Duplicate channel attributes still append, and the last `dataWindow` still wins.
Attribute sizes and the offset-table length use subtractive span checks so those
edited paths do not add a hostile size in `size_t`.

The HDR libFuzzer target compiles the production `HdrDecoder.cpp` and
`Utf8Path.cpp`, not a mock. Default builds replay the committed corpus through
the standalone driver. Required `build-linux` runs the sanitized unit test and
`fuzz_hdr_decoder` with `-runs=4000`, asserts that the log contains `#4000 DONE`,
and uploads `hdr-fuzz-evidence/` (including any crash inputs) with `if: always()`.
This proposal does not claim that iteration was observed. `.github/workflows/nightly.yml`
is not changed. Adding the target to `MVIEWER_FUZZ_TARGETS` does not create
nightly coverage, because the nightly job names its four targets explicitly.
The same `build-linux` job keeps the existing Compare/Viewer real-GL checks and
the Session ZIP sanitizer/fuzz steps.

## Explicit remaining limits

P1-13.3 is not touched. R-01 stays open: the Workspace target is still absent,
and the aggregate corpus and CI-iteration criteria stay pending until a real run
records them. A green proposal is not that run. The 4,000-iteration campaign is
a bounded execution, not proof against every input. A consistent image at the
existing pixel ceiling can still allocate the RGB float buffer (about 768 MiB);
the new check only skips that allocation when the scanline table does not fit
the file. No complexity cap is raised. `HdrDecoder.cpp` is at the 800-line warn
ceiling and does not cross it.
