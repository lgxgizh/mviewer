# MViewer — Agent Development Rules

## Build System

**Single entry point**: `build.ps1`

```powershell
powershell -ExecutionPolicy Bypass -File D:\mviewer\build.ps1 [Release|Debug|Test|Clean]
```

**Do not** invoke these directly:
- `vcvars64.bat` / `vcvarsall.bat`
- `cmake`
- `ninja`
- `cl.exe`

Always use `build.ps1` unless you are explicitly debugging the build system
(debugging changes must be reverted before commit).

**NEVER** modify `build.ps1`, `CMakePresets.json`, or `.github/workflows/ci.yml`
unless the user explicitly asks. Agents must not "improve" the build/CI — it
breaks local↔CI parity.

## Local Verify Policy

Build → Test → Commit → Push.

**Never** use CI as the primary build verifier.
Always verify locally first: `.\build.ps1 Test`.

## Code Style

- C++20, 4-space indent, 100-column limit
- Headers must compile standalone (include what you use)
- No Qt types in `domain/` or `core/` headers
- Use `std` types in core; Qt allowed only in UI layer

## Project Architecture

```
UI (Qt Widgets) → Application → Core → Domain
```

- **Domain**: Zero dependencies (pure `std` types, no Qt)
- **Core**: Qt-free headers; `.cpp` internals may use Qt
- **UI**: Qt 6 Widgets boundary only

## Git

- Branch: `master`
- Commit messages: imperative mood, describe what changed
- No commit without local build + test passing

## Documentation

- Update `docs/spec/` for API changes
- Update `docs/adr/` for architectural decisions
- Update `CHANGELOG.md` for user-facing changes
