# ExportJob specification

`mviewer::exportjob::run()` is the worker-side export contract for Convert,
Contact Sheet, PDF, CSV, JSON, HTML and Clipboard modes.

## Threading and cancellation

- Callers may supply `ExportJobConfig::cancel`.
- The worker checks the token before and between source operations.
- GUI code may consume the result only from its queued completion path.
- Clipboard ownership is GUI-only; decode, display conversion and encoding are
  worker operations.

`writeTextAtomically(destination, contents, cancelled)` provides the same
atomic destination contract for value-owned report bodies. The optional
cancellation callback is checked after the temporary file is fully written and
immediately before the final rename/replace; cancellation removes the
temporary file and never replaces an existing destination. Existing two-argument
callers retain the original behavior.

## Display fidelity

`preserveDisplayAppearance` requests repository-backed decode plus the display
materialization contract (including embedded ICC handling). It does not mutate
analysis pixels.

## Convert destinations

`destinationPath` is an exact destination for a one-source Convert job. Batch
Convert continues to use `outDir`, the selected format and the naming policy.

## Contact/PDF memory contract

`stagingMemoryBudgetBytes` bounds decoded staging memory for Contact Sheet and
PDF. The default is 512 MiB. If the next source would exceed the budget, the
job stops with a failure message instead of growing an unbounded in-memory
vector.
