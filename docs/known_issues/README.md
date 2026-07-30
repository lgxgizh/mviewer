# Failure Database (Known Issues)

This directory is the project's **Failure Database** — a structured, versioned
record of non-trivial defects we have hit, why they happened, and how each is
prevented from returning.

Unlike a bug tracker, this DB is:

- **Linked to regression tests.** Every still-open issue MUST reference a
  committed regression test (or a CI job that reproduces it). The
  `scripts/known_issues_gate.ps1` (the *Bug Gate*) enforces this automatically.
- **Searchable before you code.** Before changing an area, grep here. If a past
  defect lives in that area, you will not repeat it.
- **Auto-checked.** CI runs the Bug Gate on every PR; an open issue without a
  linked, existing regression test fails the gate.

## Schema (`known_issues.json`, `known-issues/v1`)

```json
{
  "issues": [
    {
      "id": "Issue-001",
      "title": "short description",
      "component": "ui/mainwindow",
      "status": "resolved | open | investigating",
      "root_cause": "why it broke",
      "fix_milestone": "M17",
      "regression_tests": ["src/test_x.cpp", ".github/workflows/nightly.yml"]
    }
  ]
}
```

- `status: resolved` → skipped by the Bug Gate (fix + regression already landed).
- `status: open | investigating` → MUST have `regression_tests` that exist in
  the tree, or the Bug Gate fails.

## How to add an issue

1. Create `Issue-NNN.md` here using the template below.
2. Add the entry to `known_issues.json` (with `regression_tests`).
3. Land the regression test in the same change.

```
# Issue-NNN — <title>

- Component: <area>
- Status: open | investigating | resolved
- Root cause: <why>
- Fix milestone: <Mxx>
- Regression test: <path or CI job>

## Detail
<optional deep-dive, link to ADR / spec>
```

See `Issue-001.md` (MSVC ASan) and `Issue-002.md` (double `setupUi`) for worked
examples — both real defects that were fixed and now have regression coverage.
