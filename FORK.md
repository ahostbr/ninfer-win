# This is an unofficial Windows fork of NInfer

**Upstream: [Neroued/ninfer](https://github.com/Neroued/ninfer) — Apache License 2.0.**
All of the engine, every CUDA kernel, and the entire design are upstream's work. This fork adds a
native Windows build and nothing else. It is **not** affiliated with or endorsed by the upstream
maintainer, and no part of it has been submitted to or accepted by that project.

Report engine bugs to upstream only if you can reproduce them on Linux. Anything that happens only
on Windows belongs here.

## Status: UNVERIFIED. Read this before you run it.

Nobody has yet run a real model end to end on this build and confirmed the output. What has been
measured is the build and the test suite:

| | |
|---|---|
| configure / build / link | succeed, MSVC 19.44 + CUDA 13.2, `sm_120a` |
| `ctest` | **120 of 121 pass** |

The remaining failure is `ninfer_resource_manager_test`, and its cause is **not established**.
Treat it as an open Windows defect until someone shows otherwise.

What is measured: it fails **17 consecutive runs**, both directly and through `ctest`, on an
otherwise idle machine (3% CPU). It passed exactly once, during a full 121-test suite run. The
binary is byte-identical across both outcomes (unchanged mtime), so nothing in the code differs —
the single pass is the anomaly, not the failures.

Two explanations were tested and **both are wrong**: it is not machine load (it fails when idle and
passed when the machine was busy — the opposite of the guess), and it is not the invocation method
(`ctest` and direct invocation both fail). The assertion that fails is a planner ranking check, and
the planner does carry both a wall-clock allowance and a work budget
(`materialization_planner.h:168,198,291`) that could plausibly make its output machine-dependent —
but that is a hypothesis, not a finding, and the two hypotheses tested so far were both refuted.
A run of the same assertion against the same upstream commit on Linux would settle whether this is
specific to this fork.

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
