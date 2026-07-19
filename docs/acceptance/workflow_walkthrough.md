# M12.1 Workflow Walkthrough (manual validation)

**Purpose:** scripted manual walkthrough of the three real-user scenarios from
`docs/acceptance/user_workflow.md`. Automation (QTest/clean-VM) is M12.3/M12.4;
this checklist is the M12.1 interim gate — run it on a **clean Windows VM with
no Qt install** to also exercise the TIFF-on-clean-Windows gap (G1).

## Scenario A — algorithm-result inspection
1. Open a directory of ~1000 JPEG/PNG images.
2. Confirm thumbnails appear without UI freeze (scroll test).
3. Select two images → Compare (sync zoom/pan/ROI/blink/diff).
4. Open Analysis → Histogram tab shows live data.
5. Hover an image → Pixel Inspector shows correct RGB.
6. Export → report (.json/.csv) + diff PNG; open the report and confirm it matches the on-screen diff.
7. **Accept if:** no crash, all buttons work, report opens.

## Scenario B — version comparison
1. Open `before/` and `after/` (or multi-select two dirs).
2. Load both into Compare.
3. Toggle sync zoom/pan; drag ROI; blink; view diff.
4. **Accept if:** both viewers stay synchronized (zoom/pan/ROI), diff renders.

## Scenario C — workspace persistence (validates G2)
1. In Scenario A/B, set an ROI on the active image and run an analysis (note the result text).
2. Save Workspace → `project.mvws`.
3. Close MViewer; reopen; Open Workspace `project.mvws`.
4. **Accept if:** gallery root restores, the active image re-opens with its ROI re-applied and the analysis result re-displayed.

## Clean-Windows TIFF check (G1)
- On a VM with **no Qt**, install via the M12.3 installer, open a `.tif`/`.tiff`.
- **Accept if:** TIFF decodes (proves `qtiff.dll` + `libtiff-6.dll` deployed).

## Record
For each scenario note: PASS/FAIL, build commit, Windows version, whether Qt was preinstalled. Attach screenshots to `testdata/screenshots/`.
