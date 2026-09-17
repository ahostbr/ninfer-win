# This is an unofficial Windows fork of NInfer

**Upstream: [Neroued/ninfer](https://github.com/Neroued/ninfer) — Apache License 2.0.**
All of the engine, every CUDA kernel, and the entire design are upstream's work. This fork adds a
native Windows build and nothing else. It is **not** affiliated with or endorsed by the upstream
maintainer, and no part of it has been submitted to or accepted by that project.

Report engine bugs to upstream only if you can reproduce them on Linux. Anything that happens only
on Windows belongs here.

## Status: the engine runs; treat it as lightly exercised.

**A model HAS now been run end to end on this build.** `qwen3.8-27b` NVFP4 on an RTX 5090:
coherent output, clean stop-token finish, **65.1 tok/s decode** (340.1 prefill), first token ~67 ms
after engine ready, 19.0 GiB of weights loaded in 4.5 s. `ninfer-serve` was then driven over HTTP —
`/health`, `/v1/models`, a chat completion, a streamed completion and a tool-schema call all
answered correctly.

That is **six requests and one prompt on one artifact**. Concurrency, long contexts, prefix reuse,
vision, `/v1/responses` and the Anthropic surface are all still unexercised here. It is no longer
unverified; it is lightly exercised.

| | |
|---|---|
| configure / build / link | succeed, MSVC 19.44 + CUDA 13.2, `sm_120a` |
| `ctest` | **121 of 121 pass** |
| one model, end to end | coherent, 65.1 tok/s decode on a 5090 |

### Fixed: the planner test that was measuring the machine

`ninfer_resource_manager_test` used to fail here — 20 consecutive runs on one unchanged binary,
with a single pass hours earlier. **The cause is now established, and it was not what the earlier
revision of this file guessed.**

`test_candidate_search_prefers_deep_reuse_without_eviction` asserts WHICH plan the search returns.
The search is bounded by budget gates that read the wall clock, so on a machine where the search
executes more slowly the planner stops expanding sooner and returns a one-step eviction instead of
the two-step preserving closure. The assertion was therefore a measurement of this machine's speed.

Instrumented, every run reported `stop_reason = insufficient_expected_gain` with
**`budget_exhausted = 0`** — the search was never cut off by the time budget, which is what rules
out the obvious story. Raising the allowance from 50 ms to 60 s did **not** help and produced the
*shallower* plan, which rules out "the budget is simply too small". What moves is the wall-clock
terms *inside* the economic gates.

The fix uses a seam the planner already had: `MaterializationPlanner` takes its clock as a template
parameter, and `materialization_budget.h` says why — *"Integer timestamps make wall-budget decisions
reproducible without sleeping in policy tests."* **No test in the file used it.** That one test now
instantiates the planner with a frozen clock, so the search is bounded by its work limit alone.
Measured: **20/20 failures before, 30/30 passes after.**

⚠️ **What that gives up, said plainly:** with a real clock a slow enough machine genuinely does get
the eviction plan, and that is the budget behaving as designed rather than a defect. This test no
longer observes that behaviour. It was the wrong instrument for it — a test that fails on a slow
machine and passes on a fast one reports the machine — but the behaviour is real and has no test of
its own.

### Fixed: the NVFP4 illegal instruction

An earlier revision of this fork crashed with `cudaErrorIllegalInstruction` on the default NVFP4
prefill path. **That is fixed** (`d48aa25b`) and the test passes. It is described here because
anyone who cloned before that commit has the broken version, and because the cause is worth knowing
if you are porting this yourself:

MSVC cannot take the `alignas(128)` TMA descriptors as a by-value kernel parameter (C2711), so the
Windows path stages them in device memory. The staging copied from a **host stack local**. Under
CUDA graph capture a memcpy becomes a graph node that records its *source address*, so every
`cuGraphLaunch` replay re-read a dead stack frame and `cp.async.bulk.tensor` executed against a
garbage tensormap. Upstream is immune: it passes the descriptors as a `__grid_constant__` by-value
parameter, whose bytes are captured into the kernel node. Staging is now a pinned host mirror plus a
persistent device buffer, both outliving any graph that holds them.

This defect is present in the approach taken by upstream PR #233, from which this workaround was
derived.

### Coverage removed on Windows

CUDA fault injection is gone. Upstream builds it on the GNU linker's `--wrap`, which `link.exe`
does not implement. MSVC's `/ALTERNATENAME` would satisfy the symbols and let the test link and
pass while redirecting nothing, so it was deliberately **not** used. The allocation,
event-creation, event-record and upload-failure paths are therefore unexercised on Windows. The
build says so at configure time and the test says so when it runs.

## What was changed, per Apache-2.0 §4(b)

Base: upstream `5b4303c0`. 9 commits; 48 files changed, 1010 insertions(+), 99 deletions(-).

* **Build** — MSVC flags (`NOMINMAX`, `/utf-8`, `/Zc:preprocessor`), FFmpeg and libcurl resolved
  through vcpkg in manifest mode, `windows` and `windows-dev` CMake presets, UTF-8 application
  manifest. The `ffmpeg` dependency sets `default-features: false`; without it vcpkg pulls
  `avdevice`, `avfilter` and `swresample`, which this build never asks for. The `zlib` feature is
  requested because FFmpeg gates its PNG decoder on it (upstream issue #136).
* **Artifact I/O** — `src/artifact/file_io.cpp` reimplemented on `CreateFileW` plus positional
  `ReadFile`, with `FILE_FLAG_NO_BUFFERING` standing in for `O_DIRECT`. Handles are opened
  `FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE`, because POSIX `open()` places no lock
  and callers rely on that.
* **128-bit arithmetic** — MSVC has no `__int128`. Products are accumulated in two 64-bit limbs
  (`src/core/math_util.h`). Verified under MSVC against a schoolbook oracle that does not use
  `_umul128`: 400,121 cases, 0 mismatches.
* **NVFP4 TMA kernels** — MSVC rejects the over-aligned descriptors as a by-value kernel parameter
  (C2711), so on MSVC only they travel as a device pointer. Kernel bodies are byte-identical to
  upstream.
* **Tests and tooling** — POSIX shims for `getpid`, `pipe`/`dup2`, `mkdtemp`, `aligned_alloc`
  (allocator *and* deleter, since `free()` cannot release `_aligned_malloc`), `localtime_r` in the
  vendored jinja source, and `sysconf`/`posix_fadvise`/`fdatasync`/`pread`/`pwrite` in the Python
  tools.

## Prior art

Two open upstream pull requests attempted this before and are unmerged. This fork was written
against the current tree rather than rebased from either, because upstream restructured
(`src/targets/qwen3_6` → `src/models/qwen3_5`, artifact v3) in the days after they were authored —
but both were read closely and the approach owes a great deal to them:

* [#233](https://github.com/Neroued/ninfer/pull/233) by **troubadour-hell** — MSVC + vcpkg. The
  reference for this work, including the C2711 descriptor workaround.
* [#59](https://github.com/Neroued/ninfer/pull/59) by **pelebel** — MSVC + CUDA + Ninja.

## Building

See [README.md](README.md) → "Windows (MSVC)". You need Visual Studio 2022, CUDA 13.1+, and vcpkg;
dependency setup costs about 1.8 GB peak and 0.7 GB once build trees are deleted.
