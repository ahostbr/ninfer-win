# Handoff — FellPeak — NInfer Windows port — 2026-09-16

Seat: FellPeak `d4971cf3-de7e-468d-8b37-c46acc555fb2`, tier worker. Card **T750** (Model Hub
hazards, queued). No other card held; Ryan tasked this seat directly in-session.

| | |
|---|---|
| repo | `E:\SAS\REPO_CLONES\ninfer-win` |
| branch / tip | `win/msvc-port` @ `db189659`, dirty **0** |
| base | upstream `Neroued/ninfer` **5b4303c0**; 11 commits, 48 files, +1010 / −99 |
| remotes | `fork` → `https://github.com/ahostbr/ninfer-win` (**PUBLIC**, pushed, branches track it); `origin` → upstream, **fetch only** |
| read-only, never touched | `E:\SAS\REPO_CLONES\ninfer` (NeonRelay's clone) |

Re-check everything above:
`git -C E:/SAS/REPO_CLONES/ninfer-win log --oneline 5b4303c0..HEAD && git -C E:/SAS/REPO_CLONES/ninfer-win status --porcelain | wc -l`

---

## IN-FLIGHT

**1. Artifact downloaded and VERIFIED.** `C:\ninfer-models\qwen3_8_27b_nvfp4.ninfer`
- size 23,719,715,844 bytes — matches the published size exactly
- SHA-256 `74d2c57145e6ff11d1d2faa79594477f9bc903a611af1fb20218189fbbb77d82` — **matches** (31 s)

That condition of the load approval is satisfied. Re-check any time:
```powershell
(Get-FileHash "C:\ninfer-models\qwen3_8_27b_nvfp4.ninfer" -Algorithm SHA256).Hash.ToLower()
# must equal 74d2c57145e6ff11d1d2faa79594477f9bc903a611af1fb20218189fbbb77d82
```
🔴 **Do NOT use the size/hash in `C:\Projects\LiteTUI\artifacts\ninfer-report.md`.** It says
21,492,695,040 / `bb336052…dbd81b32`; **both are wrong** and appear nowhere in the HF repo. The
authoritative values come from `SHA256SUMS`, `artifact-manifest.json` and the model card, which
agree. I put the wrong ones into a subagent brief today after being warned off that file; the agent
caught it. The file is recorded as defective at
`C:\Projects\LiteSuite\Docs\handoff-neonrelay-2026-09-16.md:92`.

**2. End-to-end run — APPROVED, ARTIFACT READY, NOT YET DONE.** This is the FIRST action after
compaction: everything it needs is in place and only the GPU checks remain.
Approval: Ryan via liteask **a-a5ed4da0**, answer msg `eba42fe6`, verbatim **"yes"** — this
artifact, ONCE, on the 5090. Relayed by Sentinel `858eb4ce` in msg `f831820d`.
Conditions to satisfy **at load time, not from this document**:
```powershell
lms ps                 # must be empty
nvidia-smi --query-gpu=memory.used --format=csv,noheader   # nothing resident but desktop (~7 GiB)
```
If anything is resident, **stop and ask again** — do not unload someone else's model. One model at
a time; unload when done and say so.
Launch (chat template ships in-tree, see below):
```powershell
$env:PATH = "C:\vcpkg-work\installed\x64-windows\bin;$env:PATH"   # REQUIRED: DLLs live here
E:\SAS\REPO_CLONES\ninfer-win\build-win\apps\ninfer.exe `
  C:\ninfer-models\qwen3_8_27b_nvfp4.ninfer `
  --prompt "Explain prefill and decode in three sentences." `
  --max-context 16384 --max-new 256
```
Owed to Sentinel afterwards, one line each: **first token · tok/s · is the output coherent** · the
exact command.

---

## OWED

### Mine
- The end-to-end run above, and its report to Sentinel `858eb4ce`.
- **T750** — not started. Two hazards, both verified in-code by me (not relayed):
  - `LocalRuntimeKind` (`packages/contracts/src/localRuntime.ts:31`) is consumed at **28 sites in
    12 files as loose `if` chains** — no `switch`, no `never`, no `assertNever`. A third literal
    compiles clean and silently takes `else` in 26. Only `:31` and the Effect schema at `:498` are
    compile-stops. The dangerous ten: 5 in `apps/server/src/provider/Layers/LocalAdapter.ts`,
    5 in `packages/shared/src/localLlmPicker.ts` — they decide residency/selection, not flags.
  - `apps/desktop/src/litesuite/services/llm/gpu-scan.ts` parses nvidia-smi CSV **positionally**
    (`parts[0]`=name, `[1]`=total, `[2]`=free). Adding `compute_cap` in its natural place shifts
    them; `parseInt("12.0",10)` = 12 **passes the NaN guard**, so every GPU would read as 12 MiB
    VRAM with no error. Append `compute_cap` **last** (`parts[3]`) or parse by field name.
    Re-check: `sed -n '28,60p'` on that file.

### Theirs
- **HubScout** (subagent) — finished, report consumed. Nothing owed.
- **LinuxBaseline** (subagent) — **stood down by me**; the question it existed for was answered by
  `d48aa25b`. Left intact in WSL `Ubuntu-26.04`: `~/ninfer-upstream` @ `5b4303c0`, plus cmake 4.2.3,
  ninja 1.13.2, ffmpeg dev, libcurl. Open only if a Linux **performance** reference is wanted;
  blocker is toolkit choice (distro CUDA is 12.4 — no `sm_120a`; NVIDIA's `ubuntu2604` channel has
  13.3.0/13.3.1/13.4.1 but **no 13.1**; CUDA 13.x needs gcc-14, not the distro's gcc 15).
- **ArtifactFetch** (subagent) — download complete and hash verified. Nothing owed.

### Ryan's
- Whether to ship at all given the open failure below, and the **Model Hub brief** (Sentinel says
  that work waits on a separate brief).
- The repo is **public and pullable by end users**. Anyone cloning between `8305818c` and
  `d48aa25b` has the NVFP4-crashing build.

---

## ABSENT BY DECISION, and the gate that defends it

- **CUDA fault-injection coverage is gone on Windows.** Upstream builds it on GNU `ld --wrap`
  (`tests/artifact/tests.cmake`), which `link.exe` lacks. MSVC's `/ALTERNATENAME:__real_X=X` would
  have satisfied every symbol and let the test **link and pass while redirecting nothing**.
  Refused. Gate: declared three times — `message(STATUS)` at configure, a comment at the mechanism,
  and a `SKIPPED … NOT covered on this platform` line at run time. Allocation, event-creation,
  event-record and upload-failure paths are unexercised here.
- **Benchmarks are OFF** in the build cache (`NINFER_BUILD_BENCHMARKS=OFF`). Not laziness: they are
  8.94 GB of ~164 MB executables and they filled E: to **0.25 GB free** today. A bare
  `cmake --build build-win` can no longer resurrect them. Re-check:
  `grep NINFER_BUILD_BENCHMARKS E:/SAS/REPO_CLONES/ninfer-win/build-win/CMakeCache.txt`

---

## CAVEATS RIDING THE GREEN LINES

**"120 / 121 tests pass" is true and does not mean the engine works.** No model has ever been
loaded, no token generated, no output inspected. Every number in this document is a build or
unit-test measurement.

**The one failure is not explained.** `ninfer_resource_manager_test` — 17 consecutive failures,
directly and via ctest, machine idle at 3% CPU; **one** pass, during a full-suite run; binary
byte-identical throughout (mtime `17:54:02`). The single pass is the anomaly. Cause **not
established**; treat as an open Windows defect. Re-check:
`E:/SAS/REPO_CLONES/ninfer-win/build-win/tests/ninfer_resource_manager_test.exe`

**`C:\vcpkg-work\` is load-bearing and backed up nowhere.** 0.69 GB; the Windows build resolves
FFmpeg/libcurl through it via `VCPKG_ROOT`, and the runtime DLLs the binaries need are in
`C:\vcpkg-work\installed\x64-windows\bin`. Rebuilding it costs ~10 min and ~1.8 GB peak. It is
outside every repo.

**Disk.** E: is at **11.2 GB** with a 13 GB `build-win` on it; it hit 0.25 GB today. C: is at
115.5 GB and holds vcpkg-work plus the 22 GB artifact. Anything large goes on C:.

---

## MY CORRECTIONS AND RETRACTIONS TODAY

Recording these because each was published or acted on before being tested.

1. **"vcpkg and Ninja are not installed."** False, twice over. Both ship with VS 2022. I asked Ryan
   to authorise installing something already present, and put the claim in commit `a43f6180`'s body
   where it is now permanently wrong. Cause: `PATH` probe + `find -maxdepth 3`, neither of which can
   see a tool 6 levels deep under `Program Files`. Pattern `…-1789585661`.
2. **The mutation test that "passed" all three arms.** The build wrote `attn_test.exe` relative to
   cwd, so rebuilds landed in `C:\Projects` while I re-ran a stale binary. All-arms-green is the
   signature of arms not reaching the binary. Pattern `…-1789580199`.
3. **`grep "&tma\."` returned 0 and I called the rename done.** It could not see the orphaned
   *declaration*; the NVFP4 path could not build on any platform for two commits. Fixed `e5d26c7d`.
   Pattern `…-1789589241`.
4. **A tensormap proxy fence** proposed as the NVFP4 fix. Tested, did not change the failure,
   reverted. The real cause was graph-capture lifetime (`d48aa25b`).
5. **The resource-manager failure explained twice, wrongly** — as load-dependence (refuted at 3%
   CPU; it *passed* under the heaviest load of the day) and as a ctest-vs-direct difference
   (refuted 3/3). Both were published to a public README before being tested. Corrected in
   `9b2b8a0f` and `db189659`.
6. **Quoted `ninfer-report.md`'s artifact size and hash** into a subagent brief hours after being
   warned that file was defective. The agent caught it. See IN-FLIGHT §1.

Common shape in 2, 3, and the stale-exe repeat at 18:5x: **an instrument that cannot reach its
subject reports clean.** The countermeasure that worked was mechanical, not vigilance — assert the
artifact changed (mtime) before believing any result.

---

## WHAT THE PORT ACTUALLY FIXED

| sha | what | gate run |
|---|---|---|
| `a43f6180` | the port, written against current master — **not** rebased from PR #233, because upstream restructured (`src/targets/qwen3_6` → `src/models/qwen3_5`, artifact v3) in the 36 commits after it | build |
| `5d8ce423` | vcpkg `ffmpeg[zlib]` — FFmpeg gates its PNG decoder on zlib (upstream #136) | dry-run closure |
| `9112141d` | the `attention_pairs` case that exercises the limb carry-in; 4 mutation arms | 4 arms, 3 killed |
| `e5d26c7d` | stale alias — NVFP4 path could not build **on any platform** | nvcc, both TUs exit 0 |
| `a748f5f1` `e29e4e64` | 14 MSVC blockers in tests (`mkdtemp`, `aligned_alloc` allocator+deleter, `constexpr std::sqrt`, missing `<array>`, `near` macro) | 345-TU sweep |
| `6ef367d5` | **`FILE_SHARE_READ`** — POSIX `open()` takes no lock; callers rewrite a file a `Reader` holds. Compiled clean through 345 TUs, linked, passed 117 tests | ctest 119/121 |
| `d48aa25b` | **the NVFP4 illegal instruction.** MSVC descriptor staging copied from a **host stack local**; CUDA graph capture records a memcpy's *source address*, so every `cuGraphLaunch` replay read a dead frame. Pinned host mirror + persistent device buffer. Same latent defect fixed in the swiglu launcher, whose test was **passing** | test exit 0 on a verified-rebuilt binary |
| `8305818c` `9b2b8a0f` `db189659` | public fork docs: Apache-2.0 §4(b), upstream + PR credits, honest status | — |

`d48aa25b` is inherent to **upstream PR #233's approach**, which this port inherited. Their PR
reports real inference at 160 tok/s, so it is latent in what they are proposing upstream.

Compatibility facts, both verified here:
- `tools/chat_templates/qwen3_8.jinja` **ships in this tree** (landed in `98dada0e`) — not a
  separate fetch, despite the model card linking it to a GitHub blob.
- Our base clears the artifact's `runtime.minimum_revision`:
  `git merge-base --is-ancestor 98dada0e 5b4303c0` → true.

---

## RE-CHECK EVERY CLAIM

```bash
# port state
git -C E:/SAS/REPO_CLONES/ninfer-win log --oneline 5b4303c0..HEAD
git -C E:/SAS/REPO_CLONES/ninfer-win diff --stat 5b4303c0..HEAD

# full suite (~9.5 min) — needs the vcpkg DLLs on PATH
#   $env:PATH = "C:\vcpkg-work\installed\x64-windows\bin;$env:PATH"
ctest --test-dir E:/SAS/REPO_CLONES/ninfer-win/build-win --output-on-failure

# rebuild (benchmarks stay off via the cache)
cmake --build E:/SAS/REPO_CLONES/ninfer-win/build-win --parallel

# reconfigure from scratch if build-win is lost
#   VCPKG_ROOT=C:\vcpkg-work\vcpkg, from a Developer Command Prompt, Ninja on PATH from
#   "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
cmake --preset windows-dev -DVCPKG_MANIFEST_MODE=OFF \
  -DVCPKG_INSTALLED_DIR=C:/vcpkg-work/installed \
  -DCMAKE_PREFIX_PATH=C:/vcpkg-work/installed/x64-windows
```

Patterns recorded today: `…-1789580199`, `…-1789585661`, `…-1789589241`, `…-1789598416`.
