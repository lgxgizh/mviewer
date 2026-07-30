# Issue-001 — MSVC ASan aborts on `std::shared_ptr` in mviewer_bench

- **Component:** toolchain / benchmark
- **Status:** resolved (M17)
- **Root cause:** MSVC 17.x AddressSanitizer mis-instruments `std::shared_ptr`
  internals, producing a `global-buffer-overflow` at process startup of
  `mviewer_bench` (main.cpp). The MSVC ASan runtime is simply incompatible with
  how the benchmark binary uses shared ownership.
- **Fix:** Run the benchmark / sanitizer checks under **LLVM clang-cl** with
  `-fsanitize=address,undefined` instead of MSVC ASan. This is encoded in the
  nightly `asan` + `ubsan` jobs (clang-cl 21).
- **Regression test:** `.github/workflows/nightly.yml` (Tier-2 `asan` / `ubsan`
  jobs run `ctest` under clang-cl sanitizers; a recurrence would surface as a
  sanitizer failure rather than a silent crash).
- **Detail:** see [MSVC_ASAN.md](./MSVC_ASAN.md).

## Lesson for future work
When adding sanitizer coverage to a Windows/MSVC C++ project, prefer
**clang-cl + LLVM sanitizers** over MSVC's built-in ASan for binaries that lean
on `std::shared_ptr` / STL internals. Document the incompatibility so it is not
"rediscovered" later.
