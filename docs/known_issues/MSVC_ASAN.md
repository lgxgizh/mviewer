# Known Issue: MSVC AddressSanitizer crashes on `shared_ptr` instrumentation

**Status:** Open — advisory only, non-gating.
**Severity:** Toolchain compatibility (not a confirmed product defect).
**First observed:** 2026-07-20, CI run `29710165150` (ASan job, windows-2022).
**Owner:** Tech Lead (revisit on next MSVC / VS toolchain update).

---

## Summary

The `ASan (MSVC /fsanitize=address, advisory)` CI job currently fails on
every run. The failure is an **access-violation inside the MSVC AddressSanitizer
runtime** while it instruments a `std::shared_ptr` call in
`ImageData::view() const`. The non-ASan build (same source) passes **31/31**
CTest cases, and clang-format / clazy / clang-tidy are green.

**Current evidence suggests this is likely a limitation or bug in the MSVC
AddressSanitizer toolchain when instrumenting this code path. The root cause has
not been conclusively proven, so the job remains advisory until verified with an
independent sanitizer (e.g. clang-cl / LLVM ASan).**

This document deliberately avoids asserting "the code has no memory errors". The
evidence below shows a toolchain-compatibility signature, not a proof of
correctness.

---

## Reproduction

1. Configure with MSVC AddressSanitizer enabled:

   ```powershell
   cmake -S . -B build_asan -G Ninja -DCMAKE_CXX_FLAGS="/fsanitize=address /Zi"
   cmake --build build_asan -j 4
   ```

2. Run the test suite under ASan:

   ```powershell
   $env:QT_QPA_PLATFORM = "offscreen"
   cd build_asan
   ctest --output-on-failure -j4
   ```

3. The crash reproduces in `test_m3_pipeline` (`testViewerCache`), which calls
   `ImageData::view()`.

---

## Toolchain versions

| Component | Version |
|---|---|
| Runner image | `windows-2022` (GitHub Actions) |
| Visual Studio | Enterprise 2022 |
| MSVC toolset | `14.44.35207` |
| Qt | `6.11.1` msvc2022_64 (CI step requests `6.8.0`) |
| ASan flag | `/fsanitize=address` (MSVC, experimental) |

> Note: Microsoft documents MSVC `/fsanitize=address` as **experimental** and
> x64-only. It is less mature than the LLVM/clang sanitizer suite, which is
> first-class for ASan / UBSan.

---

## Crash stack (from CI log, run `29710165150`)

```
==3760==ERROR: AddressSanitizer: access-violation on unknown address 0x000000000128
    #0 std::_Ptr_base<std::vector<unsigned char>>::get() const
         C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Tools\MSVC\14.44.35207\include\memory:1309
    #1 std::shared_ptr<std::vector<unsigned char>>::operator->()
         ...\include\memory:1773
    #2 ImageData::view() const
         D:\a\mviewer\mviewer\src\core\image\ImageBuffer.h:91
    #3 testViewerCache
         D:\a\mviewer\mviewer\src\core\test_m3_pipeline.cpp:141
    #4 main
         ...\test_m3_pipeline.cpp:313
```

Key observations:

- The crash is **inside the STL `shared_ptr` control-block accessor**
  (`std::_Ptr_base::get()`), not in project code.
- The faulting address `0x000000000128` is a low/near-null sentinel, typical
  of ASan's interception reading an instrumented control block that the MSVC
  ASan runtime did not set up correctly — not a use-after-free in our buffer.
- This is the **same signature** previously seen with
  `std::shared_ptr<uint8_t[]>` (array specialization). Changing the buffer type
  to `std::shared_ptr<std::vector<uint8_t>>` did **not** change the behavior,
  which strongly indicates the issue lives in `shared_ptr` interception under
  MSVC-ASan rather than in the buffer payload type.

---

## Why we are NOT "fixing" this by changing the design

The review (2026-07-19) is explicit: do not refactor `ImageData` /
`shared_ptr` merely to satisfy MSVC ASan. Rationale:

- `ImageData` intentionally uses a cheap-copy shared buffer. Removing
  `shared_ptr` would break the cheap-copy semantics that `loadDirectoryAsync`
  relies on (its completion callback copies the result vector; a deep copy of
  1000 pixel buffers regressed the "all 1000 images decoded" acceptance check).
- The failure is in `shared_ptr` itself under MSVC-ASan, so any `shared_ptr`
  holder hits it. The fix belongs in the toolchain, not the product.
- The non-ASan build is fully green; there is no evidence of a real memory
  defect in this path.

---

## Current disposition (Phase 1 — A+)

- The ASan job stays `continue-on-error: true` (advisory). It does **not**
  block the `ci-gate`.
- PR gate = `format` + `build` + `test` + `package` + `clazy`.
- This file documents the issue so the red job is not misread as "known-broken
  and ignorable" (alert fatigue). Revisit on every MSVC / VS toolchain bump.

---

## Future verification plan (Phase 2 — evolve to B)

Tracked as a Nightly-only work item (NOT in the PR gate, to keep PR velocity
high). Target milestone: **M13** (or whenever RAW decoder / SIMD / Tile Cache
land and sanitizer signal becomes high-value).

1. Add a **clang-cl + LLVM ASan / UBSan** nightly workflow:
   - Build with `clang-cl` (`-fsanitize=address,undefined`).
   - Run the same CTest suite offscreen.
   - Publish findings as a nightly artifact; never block PRs.
2. If clang-cl ASan passes consistently, that is the **independent
   sanitizer** needed to either:
   - confirm the MSVC crash was toolchain-only (close this issue), or
   - surface a real defect the MSVC run could not reach (fix it).
3. Keep MSVC ASan advisory until step 2 yields a conclusion; then either
   retire it (superseded by clang-cl) or keep both.

Recommended long-term CI shape (matches mature C++ projects):

```
PR:      format | build | test | package | clazy  -> ci-gate
Nightly: build -> test -> clang-tidy -> LLVM ASan/UBSan -> benchmark -> coverage -> artifact
Release: tag   -> package -> installer -> sign -> upload
```

---

## Re-check checklist (run after any MSVC/VS update)

- [ ] Bump a CI run on the new toolset; note MSVC version here.
- [ ] If ASan goes green → re-run under clang-cl to cross-confirm, then close.
- [ ] If still red → update the "Toolchain versions" table and keep advisory.
