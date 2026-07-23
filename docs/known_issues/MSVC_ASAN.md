# Known Issue: MSVC AddressSanitizer crashes on `shared_ptr` instrumentation

**Status:** Resolved (2026-07-24). The CI `asan` job in `.github/workflows/ci.yml`
has been switched from MSVC `/fsanitize=address` to clang-cl + LLVM ASan/UBSan
(the independent sanitizer verified in M17). The MSVC ASan job is no longer in
CI; the nightly `llvm-sanitizer` job remains as a secondary check.
**Severity:** Toolchain compatibility (MSVC ASan); one real product defect
(global-buffer-overflow in `mviewer_bench`) was found and fixed (see below).
**First observed:** 2026-07-20, CI run `29710165150` (ASan job, windows-2022).
**Owner:** Tech Lead.

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

## Resolution (M17 — 2026-07-20)

The independent sanitizer was stood up as a real, non-blocking nightly job
(`llvm-sanitizer` in `.github/workflows/nightly.yml`): clang-cl 22 + LLVM
ASan/UBSan, Release CRT (`/MD`), `/EHsc`, with the `clang_rt` asan/ubsan libs
passed explicitly (CMake links via `lld-link` directly, so `-fsanitize` alone
does not pull in the runtime).

**Result:** the CTest suite (33 tests) runs **clean under ASan/UBSan** — no
sanitizer errors in any core/UI test. `mviewer_bench --enforce` under ASan
surfaced **one real defect** the MSVC run could not reach:

- `global-buffer-overflow` at process init, reading the empty-string literal
  `""` (main.cpp:90, `smoke ? "[smoke] " : ""`) which the linker placed inside
  the redzone of an adjacent string literal. `operator<<(const char*)` strlen
  of the empty literal tripped ASan. **Fixed** by emitting `[smoke]` via an
  explicit `if (smoke)` instead of a `?:` with an empty literal. After the fix,
  `mviewer_bench --enforce` runs with zero sanitizer findings.

Conclusion: the MSVC ASan `shared_ptr` crash is a **toolchain limitation**
(confirmed — the independent clang-cl sanitizer passes the same code paths
cleanly). The one real defect found was in `mviewer_bench`, not the product
core/UI libraries, and is fixed.

> Note: under ASan instrumentation, wall-clock timings are ~3–5× slower, so the
> `<16ms` preloaded-switch (B8) and `<100ms` first-thumbnail budget assertions
> legitimately miss under the sanitizer build. These are environment-sensitive
> timing gates, not memory bugs; the sanitizer job is `continue-on-error: true`.

The plan above is now **realized** (nightly `llvm-sanitizer` job). Step 2's
outcome: clang-cl ASan passes the CTest suite cleanly (MSVC crash confirmed
toolchain-only) AND surfaced one real defect (fixed). MSVC ASan stays advisory.

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
