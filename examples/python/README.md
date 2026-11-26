# Python script bridge examples

Three ready-to-use scripts for the **Python Script** dialog (Tools menu). Each one
is self-contained, uses only the standard library, and works with the way the
bridge launches interpreters: `python -I <script> <request.json> <response.json>`.

## Protocol

Request (JSON file, path in `argv[1]`):

```json
{ "image": "C:/data/frame.png", "args": { } }
```

Response (JSON file the script must write to `argv[2]`):

```json
{ "ok": true, "message": "3 candidate(s)", "boxes": [[x, y, w, h, "label"], ...] }
```

`boxes` are in image coordinates. `{"ok": false, "message": "why"}` reports a
failure without an exception reaching the viewer.

## The examples

| Script | What it reports | Useful args |
|---|---|---|
| `hot_pixels.py` | isolated hot / dead pixels, one 1x1 box each | `delta` (default 48), `maxBoxes` (default 200) |
| `noise_estimate.py` | tiles whose Laplacian energy is an outlier | `grid` (4), `thresholdSigma` (2.0) |
| `roi_metrics.py` | per-tile mean vs the frame mean (flat-field / shading) | `grid` (4), `maxSpread` (24.0) |

Example output from `hot_pixels.py`:

```
ok: 2 isolated pixel(s) at delta=48 (64x48)
  box 10,10,1,1,hot@10,10=255
  box 30,20,1,1,dead@30,20=0
```

## Notes for writing your own

- `_imagedata.py` is a small pure-stdlib PNG reader (non-interlaced, 8-bit
  grey/grey+alpha/RGB/RGBA, all row filters). Import it after inserting the
  script's own directory into `sys.path`, exactly as the examples do - `-I` keeps
  the interpreter away from both `PYTHONPATH` and the script directory.
- Only PNG input is supported by the helper. Decode failure should return
  `{"ok": false, ...}`, which the dialog prints verbatim.
- The interpreter ignores `PYTHONPATH` / user site-packages and the environment is
  scrubbed of `PYTHON*` variables; a venv still works if you point
  `MVIEWER_PYTHON` at its interpreter.
- Output is capped: the response file at 8 MiB, stderr at 64 KiB.
