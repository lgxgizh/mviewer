# M54 — Perceived Performance & Instant Browse Baseline

Date: 2026-08-29
Milestone: M54
Platform: Windows x64, MSVC 2022, Qt 6.10.3
Baseline tree: `master` at `abef794` (M53 closure plus repository hygiene)

## Scope and method

M54 measures the real Browse path rather than starting after a completed
`listImages()` call. The test harness is the real `ThumbnailPanel` and
`PreviewPanel`, with a temporary fixture and the application scheduler/cache
path. `T0` is the call to `ThumbnailPanel::setDirectory()`.

The measured milestones are directory shell, first gallery row, first drawn
thumbnail, first-screen 50% and 90% thumbnail readiness, selected preview,
scan completion, and final gallery stability. Cold means a new temporary
fixture and cold thumbnail disk key; warm repeats the same fixture after the
first pass. The 50,000-entry baseline used a lightweight mixed directory
because the baseline question is enumeration/model latency, not decoding
50,000 large images.

The baseline recorder also exposed an important measurement limitation: the
old publication path did not expose a trustworthy independent scan-complete
event. Its scan/stability values were therefore not used as BEFORE values;
the first-row and first-screen milestones are the comparable latency data.

## Baseline results

All values are milliseconds from `T0`.

| Fixture | Run | First row | First thumbnail | Screen 50% | Screen 90% |
| --- | --- | ---: | ---: | ---: | ---: |
| 100 images | cold | 26 | 1,232 | 1,375 | 1,499 |
| 100 images | warm | 24 | 35 | 104 | 173 |
| 1,000 images | cold | 107 | 140 | 308 | 427 |
| 1,000 images | warm | 99 | 110 | 158 | 202 |
| 10,000 images | cold | 1,023 | 1,052 | 1,188 | 1,316 |
| 10,000 images | warm | 1,065 | 1,084 | 1,136 | 1,183 |
| 50,000 entries | cold | 4,937 | 4,960 | 5,129 | 5,251 |
| 50,000 entries | warm | 4,940 | 4,949 | 5,009 | 5,052 |

The 50,000-entry rows are an enumeration/model proxy from the pre-M54
lightweight mixed fixture and are not used for an apples-to-apples thumbnail
throughput claim. The 10,000-image rows are the primary comparison because
they use the same real-image shape as the M54 after run.

There was no reliable baseline p50/p95 sample set for selected preview,
fullscreen Next/Previous, viewport jump latency, peak queue, or peak memory.
Those are either new M54 observations or remain covered by the existing M47
large-source evidence and regressions.

## Reproduced bottlenecks

Source inspection and the end-to-end baseline reproduced these causes:

1. `ThumbnailPanel::setDirectory()` enumerated all entries, filtered them,
   computed sort keys, sorted the complete vector, built the complete display
   containers, and only then published the first gallery rows. First-row time
   therefore tracked full-directory work: roughly one second for 10,000 and
   nearly five seconds for the 50,000-entry proxy.
2. `ThumbnailCache::get()` and `put()` held the global cache mutex across
   filesystem checks and thumbnail payload I/O. A warm hit also best-effort
   touched the cache file timestamp, turning a read into a write-capable
   filesystem operation.
3. Viewport demand was visible-first followed by predictive work, but a rapid
   viewport change did not invalidate the old demand window. Historical
   predictive tasks could therefore remain in the scheduler queue.
4. Selected preview work used the Thumbnail priority and performed preview
   statistics before visual delivery. A selected image could wait behind
   gallery thumbnail work even when the user only needed a usable visual.
5. Existing M47 evidence already established the large Viewer display path:
   100 MP JPEG opens through a bounded LOD raster in about 0.5 s with roughly
   +53 MB RSS and no full `ImageFrame`; 100 MP Compare uses bounded pane
   rasters in about 0.9 s with roughly +82 MB RSS. M54 did not assume that a
   new viewer optimization was justified without a repeatable sequential
   navigation profile.

## Baseline gates

The clean M53 source-stable baseline passed the mandated local canonical gate
`117/117` before M54 source changes. This is the control qualification for
the performance work; M54 changes are required to preserve all of those
regressions and add the real Browse benchmark as a CTest target.

The baseline is evidence for this machine and configuration, not a
cross-machine hard threshold. M54 acceptance is based on the relative
change, the progressive-publication ordering contract, bounded demand, and
the preserved functional/architecture gates.
