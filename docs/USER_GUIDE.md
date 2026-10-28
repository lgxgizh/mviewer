# MViewer User Guide

English | [简体中文](USER_GUIDE.zh_CN.md)

A professional visual analysis platform for imaging algorithm engineers (Browse → Compare → Analyze → Report → Export → Workspace).  
In the app: **Help → User Guide** (shown in the current UI language). Full key table: **F1**.

---

## 1. Browse

1. **Ctrl+O** opens an image folder; the thumbnail grid loads asynchronously.
2. **Click** to view large; **Ctrl+click** / **Shift+click** to multi-select. **Ctrl+Shift+A** Cancel selection (deselect).
3. **View zoom**:
   - Scroll to zoom freely; double-click toggles between "Fit to Window" and 100% actual size.
   - With focus in the gallery, **Ctrl++** / **Ctrl+-** change thumbnail size and **Ctrl+Alt+9** resets it. **Ctrl+0** always fits the window.
   - Quick presets (View menu): **50%**, **100%**, **200%**, **400%**, **800%** (pixel-grid level).
4. **Geometric transforms** (overwrite the original file; JPEG is re-encoded at quality 95):
   - **Ctrl+R** / **Ctrl+Shift+R**: rotate 90° clockwise / counterclockwise and overwrite the original.
   - **Ctrl+Shift+H** / **Ctrl+Alt+V**: flip horizontally / vertically and overwrite the original.
   - With no image selected the menu items are disabled and the status bar explains why (never a silent no-op).
5. **Channel isolation and analysis**:
   - **Shift+1…6**: original RGB / red (R) / green (G) / blue (B) / luminance (Y) / value (HSV-V).
   - **Shift+C**: copy the pixel color under the cursor (Hex, RGB, Float 0-1 and HSV formats).
6. **Color management (CMS)**:
   - "View → Color Management (CMS)": when on, colors are converted with the display ICC profile; when off, RGB passes straight through (same as FastStone).
7. **Animations and multi-frame containers**:
   - GIF, animated WebP and multi-page TIFF are supported.
   - **Space** plays/pauses, **←** / **→** step one frame.
8. **Workspace and search**:
   - **F6**: focus browse mode (hide the side panels and view full window).
   - **Ctrl+Shift+F**: global search panel.
   - **F7**: analysis panel (histogram, statistics and quality metrics computed instantly). The panel analyzes the current image only; two-image comparison lives in Compare.
   - When the viewer is full screen, the context-menu "Analyze" leaves full screen first and then opens the panel in the main window.

---

## 2. Compare

1. **Entering Compare**:
   - Select **2–8 images** in the thumbnails and press **P** or **C** (with fewer than 2 the status bar explains why).
   - While browsing a single image, **Space** quick-compares it in place with the next image.
2. **Two-image compare modes** (2 images only):
   - **B** Flicker (blink)
   - **S** Split (side by side)
   - **W** Swipe (wipe)
   - **O** Overlay (transparent overlay)
   - **K** Checkerboard interleaved compare
   - Overlay opacity and checkerboard size sliders appear automatically with their mode.
3. **Difference and alignment**:
   - **H**: difference highlight heatmap.
   - The bottom status strip computes and shows **PSNR / SSIM** live.
   - Click "Align" to register the images with an integer-pixel translation before metrics are computed.
4. **Region of interest (ROI)**:
   - **Right-drag** on an image to draw an ROI.
   - A right-click without dragging opens the pane menu (including "Export Adjusted Image..."); dragging still draws an ROI.
   - The box header shows the reference label (A / B); the HUD shows the region's pixel mean, variance, extremes and histogram live.
   - Multi-level synchronized selection and TSV measurement export are supported.
   - While **Space** or **1–8** is held, the ROI box and its statistics chip follow the image the pane shows.
   - **Ctrl+C** / **Ctrl+S** copy / save exactly what is on screen, ROI boxes and statistics chips included.
5. **Peek and layouts**:
   - Hold **1–8** to temporarily preview image N in the pane under the cursor; release to restore. Without a pane under the cursor the focused pane is used, otherwise the first pane. Out-of-range digits only show a hint. Layout presets are **Ctrl+2 / Ctrl+4 / Ctrl+8**. **Ctrl+1** is 100% actual size, **0 / Ctrl+0** fits the window.
6. **Synchronization**:
   - **Z** / **D**: toggle sync zoom / sync pan.
   - "Fit window (F)" on the tool row (next to Analyze and Export Report; shortcut **F** / **Ctrl+0**): with "Uniform Pixel Scale" checked, every pane fits at one shared scale; unchecked, each pane fits its own resolution so images with the same field of view appear at a similar size. Peek, flicker or image swaps keep that on-screen size.
   - **R**: synchronized crosshair.
   - **L**: pixel link markers (compare the RGB and Δ of corresponding pixels across images).
   - **X**: swap the A / B panes.
   - The images to compare are chosen in Browse; Compare no longer steps to a previous / next group.
   - **Esc**: clears the ROI selection first; press Esc again to exit Compare.
7. **Rotate / flip in Compare is a preview**:
   - Side-panel buttons or **Ctrl+R** / **Ctrl+Shift+R** / **Ctrl+Shift+H** / **Ctrl+Alt+V** change only the current pane's display and **do not overwrite the original file**.
   - The target is the pane picked for editing, otherwise the pane under the cursor / the locked reference; without a target the status bar says so instead of silently changing pane 1.
8. **Per-pane adjustments and export**:
   - The Adjust panel sets brightness, contrast, gamma, white balance and USM sharpening (amount 0%–500%, radius 0.1–20 px, threshold 0–255, optional luminance only) for the current pane. Off by default. Preview, difference and histogram use the same adjustments and **do not overwrite the original file**. "Reset Adjustments" also turns USM off.
   - "Export Adjusted Image..." (pane context menu or Adjust panel) writes the current pane at full resolution with brightness/contrast/gamma, white balance, USM, rotate/flip and crop applied to a new PNG or TIFF. The default name is `<original>_adjusted.png`, saved next to the source or in the last export folder.

---

## 3. Batch and export

1. **Batch format conversion and rename**:
   - Press **Ctrl+Shift+B** to open the batch processing dialog.
   - Supports format conversion (PNG, JPEG, WebP, TIFF and more), batch resize, color space conversion and flexible rename templates.
2. **Batch analysis export**:
   - Press **Ctrl+Alt+A** (or gallery context menu Batch Analysis Export).
   - Runs the active Analyzer pipeline over the selected images and writes CSV / TSV in one step.
3. **Analysis report**:
   - "File → Export Report...": generates a standalone HTML report with previews, difference heatmaps, ROI statistics and PSNR/SSIM charts.
4. **Image metadata inventory in Export Images**:
   - The "Image metadata inventory CSV / JSON / HTML" modes in "File → Export Images..." write each image's file name, size and channel means; this is not the analysis report above.
   - With these modes or "Copy to clipboard", format, quality, resize, watermark, crop and rename are hidden. Inventories still need an output folder; the clipboard writes no file and needs none.

---

## 4. Workspace and project persistence

1. **Save and open a workspace** (`.mvws`):
   - Stores the open folder, the selected compare set, ROI coordinates and analyzer state.
   - Restores the working context quickly across sessions or machines.
2. **Project files** (`.mvproj`):
   - Store the full set of dataset roots, several workspaces and custom analysis pipeline configuration.
3. **Crash recovery**:
   - If the app closes unexpectedly, the next start detects the open recovery marker and offers to restore the last session.

---

## 5. Language

- **Tools → Preferences... → UI language**: Follow system (default), English or 简体中文.
- The language switches immediately for open windows; a few dialogs pick it up the next time they open.

---

## 6. Split browse

- **View → Split Browse View** or **Ctrl+Shift+S** opens a second gallery on the right. The right pane header has back / forward / up buttons, a path box, **Browse...**, **Clone Left** (show the left directory on the right), **Swap** (exchange directories and selections) and a close button.
- Click a pane to make it active; the active pane has the highlighted frame and drives the status bar.
- **Delete**, rotate and flip act on the union of the selections in both panes.
- **Edit → Copy to Other Pane** (**Ctrl+Alt+C**) and **Move to Other Pane** (**Ctrl+Shift+M**) send the active pane's selection to the directory shown in the other pane. They are disabled when both panes show the same directory. A confirmation names the source pane and the target directory (it can be turned off in Preferences). Files with the same name are saved as `name_1.ext`. Copy and move from the menu can be undone with **Ctrl+Z**.
- **F6** toggles focus browse mode (hides the side panels so the gallery fills the window).

---

## 7. USM sharpen compare

- Select an image and press **Ctrl+U** (or **Tools → USM Sharpen Compare...**). The dialog opens maximized with the original on the left and the sharpened result on the right.
- **Preset template** offers several starting points (standard, light detail boost, portrait skin protection, landscape and architecture, strong edges); changing Amount, Radius or Threshold switches to **Custom**. **Sharpen Luma Channel Only** avoids color fringes.
- Hold **Space** to show the other side in the pane under the mouse; release to restore.
- **0** fits the window, **1** is 100%, **2** is 200%. Zoom and pan stay synchronized when **Synchronize zoom/pan** is checked.
- **Ctrl+C** copies the sharpened result; **Ctrl+S** saves it as a new file. The original is never overwritten.

---

## 8. RAW files

- By default MViewer shows the preview JPEG embedded in the RAW file, which is fast. The status bar marks whether you see the embedded preview or a full decode.
- **View → Decode Full RAW** (**Ctrl+Shift+D**, also in the viewer context menu) decodes the sensor data for the current image; run it again to cancel a decode in progress.
- **Preferences → RAW display** chooses: prefer embedded preview, full decode when the preview is smaller than 90% of the sensor, or always decode full RAW.

---

## 9. File operations

- **Delete** moves files to the system recycle bin. **Ctrl+Z** / **Ctrl+Y** undo and redo deletes, renames, copies, moves, rotations and flips; the Edit menu shows what will be undone.
- Rotate and flip **overwrite the original file**. For JPEG, MViewer first tries to change only the EXIF orientation tag, which keeps the pixels and all other EXIF data intact; other formats are re-encoded. A confirmation can be enabled in Preferences, and the change can be undone.
- In the full image viewer, **Delete** and **F2** (rename) act only on the image being shown, not on the gallery selection.

---

## 10. Sort and filter

- The sort bar sorts by file name, date, size, resolution, type, rating, camera or lens; the arrow button switches ascending / descending. Equal items keep file-name order.
- **Search** filters by file name; **Include Subfolders** and **Metadata** widen the search.
- **Advanced Filter** shows camera, lens, ISO, tag, rating and flag/color-label filters plus a file-type filter. **Clear Filters** / **Reset Filters** remove every condition.

---

## 11. Preferences

**Tools → Preferences...**

- **General**: interface theme, UI language (switches immediately), default view mode, default sort, thumbnail size, slideshow interval and loop, confirmations before delete / copy or move / modifying the original, GPU rendering, color management, automatic update check, RAW display.
- **Compare**: compare window defaults.
- **Analyze**: default analysis overlay (zebra, false color, single channels) and the zebra threshold.

---

## 12. Update check

- With **Automatically check for updates on startup** enabled, MViewer looks for a newer release a few seconds after start and shows a non-blocking notice only when one exists.
- **Help → Check for Updates...** always checks on demand, even when the automatic check is off.
- **Skip this version** in the notice hides that release until a newer one appears.

---

## 13. Same key, different window

| Key | Browse | Viewer | Compare | USM |
|---|---|---|---|---|
| **1** | 100% zoom of the preview | 100% (actual pixels) | Hold to preview image 1 | 100% |
| **Ctrl+1** | Grid view mode | Grid view mode (gallery) | 100% actual size | — |
| **R** | — | Toggle ROI select mode | Sync crosshair | — |
| **Ctrl+R** | Rotate 90° clockwise (writes the file) | Rotate 90° clockwise (writes the file) | Preview-rotate the current pane (file not written) | — |
| **Space** | Quick-compare with the next image | Quick compare (animations: play/pause) | Hold to show the other side | Hold to show the other side |
| **Ctrl+C** | Copy the selected files (paste in File Explorer) | Copy the image pixels | Copy what is on screen (incl. ROI chips) | Copy the sharpened result |

---

## 14. Shortcut reference

### Compare window

| Key | Action |
|---|---|
| **Z** / **D** | Toggle sync zoom / sync pan |
| **Space** (hold) | Show the other side under the cursor; release to restore |
| **B** / **S** / **W** / **O** / **K** | Flicker / Split / Swipe / Overlay / Checkerboard |
| **H** | Difference highlight and heatmap |
| **R** / **L** | Sync crosshair / pixel link |
| **X** | Swap the A/B pane images |
| **F** | Fit all images to the window |
| **1…8** | Hold to preview image N (pane under the cursor; otherwise the focused pane or pane 1). Out-of-range digits only show a hint |
| **Ctrl+1** | 100% actual size |
| **0 / Ctrl+0** | Fit to window (same as F) |
| **Ctrl+2 / Ctrl+4 / Ctrl+8** | 2 / 4 / 8 pane layouts |
| **]** / **[** | Diff threshold +5 / -5 |
| **.** / **,** | Overlay opacity +5% / -5% |
| **Shift+1…6** | Single channel: RGB / R / G / B / Y / HSV-V |
| **Ctrl+R** / **Ctrl+Shift+R** | Preview-rotate the current pane (file not written) |
| **Ctrl+Shift+H** / **Ctrl+Alt+V** | Preview-flip the current pane (file not written) |
| **Ctrl+C** / **Ctrl+S** | Copy / save what is on screen, including ROI boxes and statistics chips (also while Space is held) |
| **Ctrl+Shift+C** | Copy the focused image's full path |
| **Esc** | Clear the ROI selection or exit Compare |
| **?** | Bottom hint strip |

### Browse window

| Key | Action |
|---|---|
| **Ctrl+O** | Open folder |
| **Ctrl+Shift+O** | Open a single file |
| **P** / **C** | Compare the 2–8 selected images |
| **Space** | Quick-compare the current image with the next (or play/pause an animation) |
| **F7** | Toggle the analysis panel (single image; in full-screen viewer, "Analyze" leaves full screen first) |
| **Ctrl+Shift+F** | Toggle the global search panel |
| **Ctrl+I** / **I** | Toggle the EXIF metadata overlay |
| **Ctrl+R** / **Ctrl+Shift+R** | Rotate 90° clockwise / counterclockwise (overwrites the original; preview only in Compare) |
| **Ctrl+Shift+H** / **Ctrl+Alt+V** | Flip horizontally / vertically (overwrites the original; preview only in Compare) |
| **Ctrl++** / **Ctrl+-** / **Ctrl+Alt+9** | Gallery focus: enlarge, shrink, or reset thumbnail size. **Ctrl+0** always fits the window |
| **Ctrl+C** | Copy the selected files to the clipboard (paste them in File Explorer) |
| **Ctrl+Shift+C** | Copy the current image's full path |
| **Shift+C** | Copy the pixel color at the cursor |
| **F6** | Toggle focus browse mode |
| **S** | Start the slideshow |
| **Ctrl+Shift+B** | Open the batch processing dialog |
| **Ctrl+Shift+A** | Cancel selection (deselect) |
| **Ctrl+Alt+A** | Batch Analysis Export |
| **←** / **→** | Previous / next image |
| **F1** | Full keyboard shortcut table |
