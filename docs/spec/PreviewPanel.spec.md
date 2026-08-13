# PreviewPanel Specification

## M36 display and identity contract

Thumbnail, scaled preview, ImageViewer and Compare surfaces use the same
display materialization semantics: embedded ICC is converted on a worker or
render-materialization path, while analysis-domain bytes remain unchanged.
Thumbnail cache schema 3 stores display-ready PNG payloads; older schema
entries never satisfy a request.

## M35 two-stage presentation contract

Selection changes maintain separate requested and presented identities.

1. If `ThumbnailPanel::thumbReady(path)` already holds a `QPixmap`, MainWindow
   passes it to `PreviewPanel::setImage`. The target thumbnail is presented
   synchronously; this stage performs no disk access or decode.
2. One cancellable Thumbnail-pool task obtains the <=512 px display-ready
   preview, using the existing Preview cache or a scaled decoder that returns
   source metadata in the same read. The accepted current generation atomically
   replaces the thumbnail and updates known source dimensions/statistics.
3. On a cold miss the previous presented frame remains visible until the target
   is usable. Labels always describe `presentedPath`, never a requested image
   that has not arrived.
4. Empty-path folder transitions clear synchronously. Failed/rejected upgrades
   preserve any usable presentation. Stage 1 shows only known dimensions/file
   size; it never presents square-thumbnail dimensions or `0 KB` as identity.

Every delivery must match both `requestedPath` and the monotonically increasing
request generation. Existing TaskScheduler cancellation and QPointer lifetime
guards remain mandatory. No full-image decode may run on the UI thread.

Observable quality states are `None`, `Thumbnail`, and `Preview`. Tests use
these states to distinguish selection-to-first-usable from selection-to-upgrade
latency without imposing a machine-dependent cold-decode millisecond cap.
