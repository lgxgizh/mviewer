# Manual acceptance wizard

Native Windows qualification rows stay **MANUAL** or **BLOCKED** until a human
runs them on a real desktop. Automated `.\build.ps1 Test` results do not convert
those rows to PASS. `scripts/manual_acceptance.ps1` is the guided way to execute
and record them.

Checklist data lives in `docs/acceptance/manual_acceptance_checklist.json`
(one entry per MANUAL/BLOCKED item, with source report, row, steps, and
expected result).

## Purpose

- Walk a tester through the hardware-only rows left open by M46, M51, M57,
  M59, and M61 review reports.
- Collect host evidence (OS build, monitors, GPU, volumes, long-path policy,
  MViewer version) automatically.
- Offer **BLOCKED** when a probe shows a missing prerequisite (for example one
  volume, or fewer than two monitors).
- Save progress after every item so a session can resume.

## Prerequisites

- Windows desktop with a mouse and keyboard (PowerShell 7 or Windows
  PowerShell 5.1).
- A shipping `MViewer.exe` (Release `build_msvc` output or a portable package).
- Item-specific hardware as listed in the checklist, including:
  - 100 / 125 / 150 / 200% display scaling
  - two monitors with different DPI (mixed-DPI)
  - a second writable volume
  - installed ICC profiles, and an HDR monitor where that row is in scope
  - UNC/removable storage for the network matrix
  - clean-machine installer artifacts under `dist/`

Linux `pwsh` can validate the checklist (`-DryRun -NonInteractive`) but cannot
complete the hardware rows.

## How to run

From the repository root:

```powershell
pwsh -File scripts/manual_acceptance.ps1
```

Windows PowerShell 5.1:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/manual_acceptance.ps1
```

Examples:

```powershell
# Print items and exit
pwsh -File scripts/manual_acceptance.ps1 -List

# Point at a portable package and a sample folder
pwsh -File scripts/manual_acceptance.ps1 `
  -Exe D:\dist\MViewer\MViewer.exe `
  -SampleDir D:\photos\sample

# Run a subset
pwsh -File scripts/manual_acceptance.ps1 -Only M46-16,M46-19,M46-22

# Resume a previous session
pwsh -File scripts/manual_acceptance.ps1 `
  -Resume artifacts/manual_acceptance/20260817-140000/results.json

# Schema + environment only (no prompts; used by the self-test)
pwsh -File scripts/manual_acceptance.ps1 -DryRun -NonInteractive
```

Default `-Exe` search order: `build_msvc/**/MViewer.exe`, then a portable
package under `dist/`. Default `-OutDir` is
`artifacts/manual_acceptance/<yyyyMMdd-HHmmss>/`.

For each item the wizard shows title, source, numbered steps, and expected
result. It can launch MViewer, then records Pass / Fail / Blocked / Skip plus
an optional note and an optional primary-screen screenshot.

Exit codes: `0` all executed items passed, `1` any FAIL, `2` usage or
checklist validation error. Skip and Blocked do not fail the run.

## Outputs

| File | Contents |
| --- | --- |
| `results.json` | Machine-readable session, including environment and per-item verdicts |
| `report.md` | Markdown table in the review-doc style: `#`, Item, Result, Evidence |
| `environment.json` | OS, monitors, GPU, volumes, long-path flag, MViewer version |
| `screenshots/` | Optional primary-screen captures named by item id |

## Attach results to a review report

1. Copy `report.md` into the matching review document (or link the artifact
   directory from the sign-off section).
2. Keep `results.json` next to it so a later run can `-Resume`.
3. Paste screenshots into the Evidence column or store them beside the report.
4. Do not mark a MANUAL row PASS solely because CTest is green.

Self-test (cheap, no compile):

```powershell
pwsh -File scripts/manual_acceptance_test.ps1
```

## Source reports

| Report | MANUAL / BLOCKED rows covered |
| --- | --- |
| [M46 native Windows qualification](../review/M46_NATIVE_WINDOWS_QUALIFICATION_2026-08-17.md) | Rows 10-17, 19-20, 22-23, 26 |
| [M46 reliability soak](../review/M46_RELIABILITY_SOAK_2026-08-17.md) | Remaining hardware risks (same rows) |
| [M51 native launch / release contract](../review/M51_NATIVE_LAUNCH_RELEASE_CONTRACT_WINDOWS_QUALIFICATION_2026-08-28.md) | Clean-machine install, UNC/removable, GPU/driver, native dialogs |
| [M57 multi-frame / multi-page](../review/M57_MULTIFRAME_MULTIPAGE_CLOSURE_2026-08-29.md) | Item 16: playback feel, hostile disposal, 100MP multi-page TIFF |
| [M59 color-managed presentation](../review/M59_COLOR_MANAGED_PRESENTATION_CLOSURE_2026-09-02.md) | Monitor LUT, HDR, mixed-DPI, compositor screenshots |
| [M59 phase 0 baseline](../review/M59_PHASE0_COLOR_METADATA_BASELINE_2026-09-01.md) | Physical ICC / HDR / mixed-DPI boundary |
| [M61 professional linked ROI](../review/M61_PROFESSIONAL_LINKED_ROI_CLOSURE_2026-09-02.md) | Item 15: pointer feel, overlay contrast |
