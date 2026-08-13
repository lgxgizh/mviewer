# PreviewPanel Specification

## M35 two-stage presentation contract

Selection changes maintain separate requested and presented identities.

1. If `ThumbnailPanel::thumbReady(path)` already holds a `QPixmap`, MainWindow
   passes it to `PreviewPanel::setImage`. The target thumbnail is presented
   synchronously; this stage performs no disk access or decode.
2. One cancellable Thumbnail-pool task obtains the <=512 px preview, using the
   existing Preview cache or scaled decoder. The accepted current generation
   atomically replaces the thumbnail and updates source dimensions/statistics.
3. On a cold miss the previous presented frame remains visible until the target
   is usable. Labels always describe `presentedPath`, never a requested image
   that has not arrived.
4. Empty-path folder transitions clear synchronously. Failed/rejected upgrades
   preserve any usable presentation.

Every delivery must match both `requestedPath` and the monotonically increasing
request generation. Existing TaskScheduler cancellation and QPointer lifetime
guards remain mandatory. No full-image decode may run on the UI thread.

Observable quality states are `None`, `Thumbnail`, and `Preview`. Tests use
these states to distinguish selection-to-first-usable from selection-to-upgrade
latency without imposing a machine-dependent cold-decode millisecond cap.
