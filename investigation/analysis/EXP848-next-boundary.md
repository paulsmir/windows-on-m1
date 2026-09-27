# EXP848: next boundary, offline analysis

Date: 2026-09-27. Base: `0e5bd13dfc800ace99f61bc7e34eff38f51a6670`, branch
`integration/ad04-windows-compiler`. Task: `.local/tandem/NEXT_TASK_ANALYSIS_848.md`.
No Air connection, launch, install, rebuild, or product-code change was made.
Only the Windows builder was contacted; CDB opened the existing DLL as an image,
not a running process. The saved ETL was copied to an isolated builder analysis
folder for offline decoding. Existing dirty `m1n1_windows` and `mu` were preserved.

**Findings:** c410ade6 is present in the projected EXP848 source but absent from
the compiler source actually selected for the UMD. The exact DLL still executes
the disassembler self-test. The AV symbol is real, not an identical-COMDAT alias.
The UMD log is capped at 128 ordinary records per DLL lifetime, so zero logged
Present is not proof of no Present. Create-device 1370 means **685 successful
entry/exit pairs**, not 1370 devices. Kernel ETL independently records repeated
device errors; these must be distinguished from the later shell AVs.

## Evidence and scope

Paths below use these explicit roots:

- `E` = `/Users/pavel/public_windows/.local/experiments/EXP848-r127-stale`.
- `H` = `E/hardware-evidence`.
- `A` = `E/offline-analysis` (commands, raw CDB output, decoded XML, analysis scripts).
- `P` = builder `C:\Users\pauls\EXP848-r127-stale\native-gpuva-source`.
- `C` = builder `C:\Users\pauls\AD04-fullcompiler-001\asahi-arm64-37c472b7142e4e52a35fccf1b41da45a`.
- `R` = builder `C:\Users\pauls\AD04-persistent-dwm-next\drivers\apple-agx\render-admission\umd\ARM64\Release`.

Raw offline analysis outputs are indexed by `A/manifest.json`, SHA-256
`679291d6eb06aa540d99e37d8ce22b40fe585df35a4c5c17f84a12ce0fc978f2`.
The decisive excerpts and all per-CreateDevice intervals are also committed with
this report, so the conclusion does not depend only on mutable builder files.

All nine files in `H/artifact-hashes.json` were freshly verified against their
recorded byte sizes and SHA-256 values. Relevant identities:

| Item | SHA-256 |
|---|---|
| EXP848 DLL, both host package and builder R | `9da080c8ccfdf9543676d836a0efe656a8350e9effb1d22d015b9de1003a1c1f` |
| Matching R PDB | `14a9cf87a06cfe32bee56d935d98f72623e5a950787e0450b9d51c88775d5ebd` |
| P/src/asahi/compiler/agx_compile.c | `7c2c4866b975b8a108518b58b17376ca2e24d56c0b6efdd1346a833cdf23a63e` |
| Actually compiled C/src/asahi/compiler/agx_compile.c | `72df6f94c2c7935f5579f790e3d940e27e79c835811d7c1d711e748fa21119eb` |
| 106-compiler-agx_compile.obj | `0910d661b14f2dbe66c1a8a038684ebceda253de59ae4d45641d96a4ade759cd` |
| H/umd.log, 87,216 lines | `2b83f5278ae66772f8985480634f9ba9ea0a7e9a7cdc41ece6090fab7f29d42d` |
| H/EXP801DxgBoot.etl, also verified on builder | `ab90d4e527f86438ede7d13202ba09d2e45cae3940100304e647169d80bcac14` |

The saved Application 1000 messages are in `E/checkpoint.log:5` (JSON following
PowerShell progress XML), not an Application.evtx or a user-mode crash dump:

| UTC | PID | Image / fault module | Exception / offset |
|---|---:|---|---|
| 09:43:10.948 | 1236 (0x4d4) | dwm.exe / dwmcore.dll | C0000005 / 0x11c020 |
| 09:43:28.677 | 1328 (0x530) | StartMenuExperienceHost.exe / UMD848 | C0000005 / 0xf0ad0 |
| 09:46:02.382 | 4524 (0x11ac) | Explorer.EXE / UMD848 | C0000005 / 0xf0ad0 |

UMD848 event timestamp `0x6ab8393b` and version `30.0.848.0` agree with CDB
`lmv`. No exception context, X0, LR, call stack, or faulting virtual address was
collected for these AVs. Static attribution below does not invent that data.

Source-first scope: the observed boundary is CPU-side UMD/compiler execution and
its build provenance. Inspected the exact projected and selected Mesa compiler,
ISA decoder, Meson source graph, UMD frontend/winsys/diagnostics, closure builder,
matching PE/PDB/map, current state, EXP848 ledger and saved ETL. The Windows ETW
schemas used are the builder's installed Microsoft-Windows-DxgKrnl provider;
no undocumented numeric reason is assigned a guessed meaning. No new MMIO,
interrupt, DMA, ACPI, power or driver-interface behavior is proposed. New live
ADT/register measurements and unrelated Asahi Linux/m1n1/Mu archaeology are
therefore outside this offline task. The last accepted EXP848/R110 launch and
ordinary Code28 recovery contract remains unchanged.

WHY CONTINUE COMPARISON: this is one bounded comparison of the **same EXP848**
projected source, actual compile argv, object and linked DLL; their two different
source hashes discriminate build provenance directly. No older working-reference
comparison or clean WDDM admission reconstruction is warranted at this boundary.

## A1. Is the self-test patch present in the projected builder source?

**Yes, but that file was not the compiler translation unit used by the closure.**
Fresh builder read (`A/cdb-initial.txt`, `A/closure.txt`):

```c
// P/src/asahi/compiler/agx_compile.c:3503
bool dump_shaders = agx_should_dump(nir, AGX_DBG_SHADERS);
#if !defined(NDEBUG) && !defined(_WIN32)  // :3504
bool selftest = !dump_shaders;
#else
bool selftest = false;                  // :3507
#endif
```

The projection is authored by
`drivers/apple-agx/mesa/scripts/build-native-asahi-state.py:3496`–3504.
However, `build-asahi-runtime-closure.py:196`–215 takes compiler translation
units from `C/inputs.json`, appending `Path(name)` from that evidence, and
`:372`–388 compiles those paths. Recording/checking the projected tree at
`:180`–194 does not select it for compilation.

The EXP848 closure `native-gpuva-arm64/result.json`, unit
`106-compiler-agx_compile`, records `/c C/src/asahi/compiler/agx_compile.c`,
source hash `72df6f94...`, the object hash above, exit 0, `/O2`, `/Gy`, and no
NDEBUG define. This was a fresh closure build, not merely an old object retained
by incremental MSBuild: `E/build-kmd.ps1:16`–20 removes both output trees,
prepares P and invokes the closure. The selection error persists after rebuilding.

The selected file has `#ifndef NDEBUG` at `C/.../agx_compile.c:3504` and the
Windows branch `fopen("NUL", "w")` at :3511–3512. A complete diff between the
selected and projected files has only the self-test guard and NUL conditional
hunk (`A/schema-diff.txt`, final hunk). Do not replace it with an unreviewed
whole-tree compiler swap; preserve the existing source-identity checks.

## A2. All relevant disassembler call sites and runtime reachability

Search over projected `.c/.cpp/.h/.py`, reconciled against the **actual closure
units and final linked map** (`A/builder-source.txt`, `A/closure.txt`):

| Source site | Call | In UMD848 / reachable? |
|---|---|---|
| P/src/asahi/compiler/agx_compile.c:3513 | agx2_disassemble | P translation unit is not selected. If selected on Win32, only explicit `agx_should_dump` remains. |
| P/src/asahi/compiler/agx_compile.c:3520 | agx2_disassemble, error retry | P translation unit not selected; its Win32 `selftest=false` makes this retry unreachable. |
| **C/src/asahi/compiler/agx_compile.c:3517** | agx2_disassemble | Actual UMD compiler source: dump **or automatic non-NDEBUG self-test**, reachable on shader compilation. |
| **C/src/asahi/compiler/agx_compile.c:3524** | agx2_disassemble | Actual UMD source: retry to stderr after an error during self-test. |
| src/asahi/isa/disasm.h:24 | agx2_disassemble_instr | Inline wrapper used by actual compiler (include path selects the original Mesa ISA directory); one inlined primary loop and one out-of-line retry wrapper are in the PE. |
| P/src/asahi/lib/decode.c:29 | agx2_disassemble | Debug command-stream decoder wrapper, **not compiled/linked** here. |
| P/src/asahi/lib/decode.c:326, :339, :802 | agx_disassemble | All three are in that excluded decoder. |
| P/src/asahi/isa/test/test-disassembler.c:81 | agx2_disassemble_instr | Unit-test-only source, not in closure. |

`disasm.h:9` is a declaration, not another call. `gen-disasm.py:160`–162
generates the exported decoder definition calling `_disassemble_instr`.
The actual generated source is
`C:\Users\pauls\AD04-fullcompiler-001\asahi-input\agx2_disasm.c`, unit
`148-compiler-agx2_disasm` (source SHA
`d5e1e0d4ee82a177a398dbc4fd8cc1a806e15c8b300f9e82f872b8d75612eb9b`).
`drivers/apple-agx/mesa/compiler-host/agx_vs_fs_fixture.c:253` is another
repository test call, also outside this UMD closure.

Exclusion of decode.c is explicit in the source graph: P/src/asahi/lib/meson.build
has `libasahi_lib_files` at :6 and **separate** `libasahi_decode_files` at :26–27;
the closure selects the former (`build-asahi-runtime-closure.py:317`–326).
Neither the actual unit list nor final symbols/map contains agx_disassemble or
agxdecode. Thus command-stream decoding is not an alternative caller in DLL848.
`agx_compile.c:37,53` gates explicit shader dumps via `AGX_MESA_DEBUG`; its value
in the crashed processes was not captured. The retained automatic self-test
requires no such environment setting.

## A3. CDB, callers, COMDAT folding, and what the AV actually proves

Command (x64 debugger, ARM64 PE opened with its matching private PDB):

```text
"C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe"
  -y R -z R\AppleAgxRenderAdmissionUmd.dll
  -c "lmv; ln 1800f0ad0; x /v *!*disassemble*; u 1800f0a84 L50; q"
```

`A/cdb-initial.txt` records private PDB symbols loaded and:

```text
(00000001`800f0a84) ...!agx2_disassemble_instr+0x4c
00000001`800f0ad0 3dc00000 ldr q0,[x0]
```

`x /v *!*` filtered for the exact entry address finds only
`agx2_disassemble_instr`. The map independently has exactly one entry there:

```text
R/AppleAgxRenderAdmissionUmd.map:605:
0001:000efa84 agx2_disassemble_instr 00000001800f0a84
  f native_runtime:148-compiler-agx2_disasm.obj
```

There is **no evidence of an identical-COMDAT alias at this address**. CDB labels
the static wrapper at `0x180080298` as `agx_compile_shader_nir+0xf508` because it
lacks a public symbol there; map :13293 identifies it as `agx2_disassemble`.
That nearest-public-symbol label must not be confused with function folding.

CDB `uf 180070d90; uf 180080298` (`A/cdb-functions.txt`) and an independent
scan of all executable PE sections for ARM64 direct B/BL targets
(`A/branches.py`, `A/branches.json`) give the complete direct incoming edges:

```text
1800751c8  BL 1800f0a84  // compiler primary inline disassembly loop
180076280 BL 180080298  // compiler error/self-test retry wrapper
1800802d8 BL 1800f0a84  // wrapper -> instruction decoder
```

The primary caller tests `agx_compiler_debug` bits at `180075124`–`18007513c`;
its default, non-dump branch **enters** `180075140`, loads `"NUL"` and `"w"`,
opens the sink, then reaches the loop and BL at `1800751c8`. CDB
`da 180330d54; da 180330d50` prints those two strings (`A/symbols.txt`).
Consequently the final binary, not only the saved command, proves self-test
was retained. Explicit dumps also reach that primary loop.

The decoder's `ldr q0,[x0]` corresponds to the unconditional 16-byte
`memcpy(tmp, code, sizeof(tmp))` in `src/asahi/isa/disasm-internal.h:183`–187,
with `BITSET_WORD tmp[4]`. `disasm.h:18` checks only `i < max_len`; :20 also
reads `code[i+1]` without a remaining-length check. There is a concrete unsafe
short-tail read contract. **It is not a FILE-pointer dereference at this PC**;
X0 is the instruction-code argument (the FILE pointer was saved from X1).

Real faulting function: **agx2_disassemble_instr**, with the compiler path as
its only statically observed incoming call family. Without the crash LR/X0,
we cannot distinguish primary self-test, explicit dump, or retry as the dynamic
caller, or prove whether the bad read was short-tail, stale pointer, or earlier
memory corruption. Claiming an exact crash stack from `cdb -z DLL` would be wrong.
The strongest explanation is retained self-test reaching the unsafe decoder,
not a missing `_WIN32` in P, not folding, and not `/dev/null` failure.

## A4. Minimal owning-layer fix and regression gate (proposed, not implemented)

1. **Fix compiler-input selection in build-asahi-runtime-closure.py.** When a
   compiler C translation unit has an explicitly accepted projected counterpart,
   select that counterpart and its final recorded SHA, preserving the pinned
   baseline and required compiler transforms. For this defect only
   `agx_compile.c` needs to switch. The full diff above contains no unrelated
   compiler lowering changes. Reject an ambiguous or unhashed selection instead
   of merely recording the correct file and compiling a different one.
2. Regression: drive the actual closure command construction with both the
   pinned baseline and the prepared tree present. Assert the `/c` path and
   `source_sha256` equal the prepared file, object provenance agrees, and archive
   membership contains the corresponding object. This must be RED on EXP848.
   Then compile/preprocess with Win32 and assertions enabled, shader dumps off;
   a real compiler shader test with a counting/fail-on-call disassembler hook
   must execute compilation with zero automatic disassembler invocations.
   Explicit opt-in dumps should remain distinguishable. ARM64 final-PE review
   must show the default path skipping disassembly; absence of the decoder
   symbol itself is not required because opt-in dumping remains supported.
3. Separately, the decoder owner should receive a guard-page regression:
   actual generated decoder/wrapper, each legal short instruction at the end of
   a readable page followed by PAGE_NOACCESS, plus truncated input. A length-aware
   decode or bounded copy must avoid reads past the supplied range and report
   truncation without treating padding as real instructions. This catches the
   underlying memory-safety defect if diagnostic dumping is explicitly enabled.
   It is not established that this exact tail condition was the EXP848 X0.

Existing `tests/test_g4_disasm_selftest.py:14`–18 only checks strings in the
projection generator. Fresh execution passed **1/1** while the exact DLL still
contains self-test. It therefore does not test the broken selected-source→object
contract. No fix or RED→GREEN claim is made by this analysis.

Ownership: the closure builder owns source selection; Mesa compiler/decoder owns
CPU compilation and diagnostic memory safety; UMD owns device lifecycle/error
reporting. KMD/VidMm retain DMA, GPUVA, scheduling and interrupts; m1n1/Mu retain
power, reservation and launch/recovery ownership. None should conceal the
compiler build defect. Smallest falsifiable next checkpoint is offline source
selection plus actual compiler/PE verification, with no hardware needed. A later
hardware checkpoint, separately authorized and preregistered, would require an
exact hash-verified candidate and absence of this AV with lifecycle evidence;
recovery remains the accepted ordered Code0 restart → hidden exact cleanup →
ordinary Code28 path, with immutable emergency artifacts preserved.

## B1. Per-process CreateDevice sequences and churn

The companion [EXP848-device-sequences.csv](EXP848-device-sequences.csv) records
**every one of the 685 observed CreateDevice entries**, its paired exit, PID/TID,
line numbers, draw count before the next entry for that PID, last non-OpenAdapter
record and next entry. The raw log lacks timestamps and device identities;
these are PID-local observed intervals, not asserted complete DDI lifetimes.
Interleaving of devices/threads cannot be resolved by guessing.

| PID | Name from saved evidence | CreateDevice pairs, all S_OK | Draw-before | Ordinary log lines |
|---:|---|---:|---:|---:|
| 1228 | unknown | 63 | 62 | 7,964 |
| 1236 | dwm.exe (Application 1000) | 2 | 0 | 128 |
| 5248 | unknown | 1 | 1 | 128 |
| 4524 | Explorer.EXE (Application 1000) | 1 | 0 | 128 |
| 5600 | unknown | 1 | 1 | 128 |
| 1328 | StartMenuExperienceHost.exe (Application 1000) | 54 | 54 | 6,910 |
| 2208 | unknown; OpenAdapter only | 0 | 0 | 4 |
| 7896 | unknown | 1 | 1 | 128 |
| 6512 | unknown; no basis to label it replacement DWM | 2 | 0 | 128 |
| 5628 | unknown | 202 | 201 | 25,746 |
| 6392 | unknown | 1 | 0 | 128 |
| 1872 | unknown | 357 | 357 | 45,696 |

The large repeaters are 1228, 1328, 5628, 1872. DWM has two **observed**
CreateDevice calls, not a proven hundreds-device churn. Do not equate all
repeaters with DWM or assign executable names from PID order. DxgKrnl Process
Start/Stop schema gives PID and DxgProcess, not a host image name; the enabled
trace has only the DxgKrnl provider, not an independent process/image timeline.

Typical visible interval: two OpenAdapter enter/exit pairs → CreateDevice enter
→ context/paging queue/render fence/screen creation → native Allocate/Reserve/
Map/Lock → CreateDevice exit S_OK → resource creation/bind/flush-state → one
`draw-before` → more allocations/locks → logging cap → next OpenAdapter pairs.
Every next CreateDevice is immediately preceded, in PID-filtered log order, by
`g4-open-adapter-exit S_OK`. That is not an error return or proof of the trigger.

Concrete examples (`H/umd.log`, original line numbers):

```text
15774 g4-native-reserve-va-cb hr=0x00000000 pid=5628 tid=5208 ...
15775 g4-open-adapter-enter  hr=0x00000000 pid=5628 tid=4056
# the next successful creation follows; omitted work is not observable
41648 g4-native-reserve-va-cb hr=0x00000000 pid=1872 tid=7716 ...
41649 g4-open-adapter-enter  hr=0x00000000 pid=1872 tid=9076
288   g4-native-reserve-va-cb hr=0x00000000 pid=1236 tid=1400 ...
# DWM's last normal record, exactly its 128th; later AV is known from event 1000
```

128-record blocks are exact: PID5628 has 201 blocks of 128 plus a final 18;
PID1872 has 357 blocks of 128; PID1328 has 53×128 plus 126. This is consistent
with DLL unload/reload resetting a module-static counter, but that reset
mechanism is not directly logged. It is not evidence that the last ReserveVA
call caused the next creation.

`umd_runtime_device.c:41`–64 uses `static volatile LONG records` and suppresses
**all non-`reject-`** records after 128. `runtime-set-error` at :85–88 is capped,
as are successful Draw/flush/Present and callback failures with ordinary names.
`agx_d3d10_windows.cpp:459`–480 has no normal DestroyDevice entry/exit receipt.
Frontend `SetError` becomes `reject-seterror` in the projection at
`build-native-asahi-state.py:405`–422 and is exempt from the cap. Therefore:

- No observed DestroyDevice, runtime-set-error, device-removed record, or flush
  failure in umd.log does **not** prove those paths never ran.
- No `reject-*`, including `reject-seterror`, was captured. This is stronger
  evidence against instrumented frontend rejection than against other errors.
- All 4,739 visible `flush-state` records have S_OK, but the record occurs inside
  FlushStatus, not at every eventual FlushRetire return
  (`agx_d3d10_windows.cpp:497`–535).
- The only nonzero logged HRESULT is 12,970 MapGpuVA `0x8000000a` = E_PENDING.
  The owning callback explicitly accepts it (`umd_gpuva_windows.c:78`–81),
  records a paging fence and returns a pending outcome. It is not an observed
  failed CreateDevice. Whether every later wait/submit succeeds is not in this log.

### Independent early ETL evidence

Offline `Get-WinEvent -FilterHashtable @{Path=<saved.etl>; Id=<explicit ids>}
-Oldest` decoded Device/Context Start/Stop and Present/error records. Reproducible
commands: `A/etl-focus.ps1`, `A/etl-correlate.ps1`; raw XML has original ETL
EventRecordID and UTC. `A/correlate.py` joins chronologically, scopes user context
handles by PID, and resolves reused device handles to the preceding Start.

The captured selected events cover 09:39:56.8666–09:41:51.4988Z, before all three
AV events above. The file is exactly the configured 512-MiB sequential maximum
(`E/autologger-stage.ps1:21`–25); do not extrapolate this early trace to the
09:50 end of the run. The initial header timestamp is 09:39:27.3341Z.

84 `VidSchMarkDeviceAsError` (ID467) records have `Reason=19`,
`FromUserMode=false`, all joined to adapter `0xffffe20e4b18c000`:
PID1228:25, Explorer4524:19, PID5316:18, StartMenu1328:18,
DWM1236:1, PID5248:1, PID5600:1, PID7896:1. PID5316 does not occur in the UMD
log; no speculative name is assigned. These are kernel device-error records,
not a decoded HRESULT or a proven SetErrorCb from UMD.

Example, all from the same chronological device history:

```text
record 47732 09:40:02.1232361 ID27 Device Start
  PID1228 hDevice=ffffb60b672e8820 adapter=ffffe20e4b18c000
record 52636 09:40:02.4391858 ID467
  DxgDevice=ffffb60b672e8820 FromUserMode=false Reason=19
record 58579 09:40:02.7637574 ID27 next Device Start, PID1228
  same reused hDevice address, different hThunkHandle
record 62743 09:40:03.0483914 ID467 Reason=19
```

The ETL has 208 Device Start, 190 Device Stop, 216 Context Start, 192 Context
Stop records globally. These show kernel lifecycle turnover, while the UMD log
cannot expose the last DDI/result before each recreation. The meaning and call
site of **Reason 19 remain unresolved**; its numeric value alone must not be
translated into a guessed residency, submission or compiler error.

## B2. Did DWM reach Present/Blt through our UMD, and why not?

**Not established for the Apple UMD.** Zero logged `_Present-entry`, g4-present,
present-callback, or reject-BltDXGI is insufficient after the 128-record cap.
The projected `DxgiFns.cpp:72`–74 logs `_Present-entry` through that capped
channel; :473–501 only guarantees an uncapped Blt record on a rejecting path.
`agx_d3d10_windows.cpp:667`–685 similarly caps g4-present entry/exit. A successful
Blt can be silent. DWM's log budget is exhausted before any visible Draw.

There **were** kernel Presents, on a different adapter: ID184 gives 79 for
DWM PID1236 and 77 for PID1228, every ReturnStatus=0. Temporal PID-scoped
Context→Device joins place all 156 on `0xffffe20e460df000`, not the adapter
`0xffffe20e4b18c000` whose devices receive Reason19. The first DWM sample is
record5061, 09:39:58.0692969Z, context handle `0x40000840` → Context Start
record2509 → Device Start record2492 → adapter `...460df000`. This prevents
misreporting fallback/other-adapter Present as an Apple frame. Adapter identities
are independently pinned by ID110/ID250 (`A/etl-adapter.xmls`): record36796 gives
adapter `...4b18c000`, LUID259626, ACPI-style vendor/device values; record36797
names its node **Apple AGX 3D node**. Record222 gives adapter `...460df000`,
LUID24161, VendorID5140 (`0x1414`) / DeviceID140 (`0x008c`), the Microsoft
software adapter. No observed ID184 Present joins to Apple in this early window.

No DWM-correlated Apple render/completion/Present or nonzero scanout is proven.
`RUN EXP848 AFTER` (`investigation/EXPERIMENTS.md:48922`) reports all-zero scanout.
The evidence supports an earlier device-error boundary and a later separate DWM
AV; it does not yet identify the exact DDI that blocks Apple Present. In
particular, the shell compiler AV cannot simply be assigned as DWM's dwmcore
AV cause without its crash stack.

## B3. Ranked hypotheses and the smallest next discriminator

1. **Kernel-origin device invalidation interrupts the Apple-side lifecycle.**
   This is the strongest churn explanation: repeated ID467 Reason19 precedes
   renewed Device Start, whereas all visible UMD CreateDevice returns succeed
   and instrumented frontend rejects are absent. It also occurs for DWM before
   the later dwmcore AV. Smallest next offline check: decode the existing ETL
   call-stack data around the first record52636, using the matching captured
   kernel image identities/public symbols, and identify the caller assigning
   Reason19. If the saved stack cannot identify it, propose one bounded
   lifecycle/error receipt channel exempt from success-chatter quota: monotonic
   sequence/time, process+device generation, create/destroy and module load/unload,
   final FlushRetire result, runtime SetError, and first submit/residency failure.
   Exercise that channel with >128 startup callbacks in a host mock before any
   later candidate. No instrumentation is implemented or hardware authorized here.
2. **Retained compiler self-test causes shell AV and some lost work.** Strongly
   supported for the shell by exact fault RVA, final binary and selected source;
   dynamic short-tail trigger remains unproven. Offline source-selection and
   real compiler test in A4 is the smallest gate. This cannot by itself explain
   kernel FromUserMode=false Reason19 or DWM's distinct fault module.
3. **A later flush/submission/presentation failure is hidden by diagnostics.**
   There are 677 visible pre-draw records but no trustworthy post-cap lifecycle
   result, and scanout remains zero. This is weaker and deliberately broad until
   the first kernel error is attributed. The bounded first-failure/lifecycle
   instrumentation above would distinguish it. No basis yet to change caps,
   firmware, segment policy, waits, recovery, or add a new Windows interface.

The top churn discriminator and the confirmed A1 build defect are separate:
fixing source selection is justified offline; claiming it repairs DWM/churn is
not. Stop this analysis at these boundaries rather than launching an unchanged
package or explaining old experiments again.

## Review dispositions and verification

Read `/Users/pavel/public_windows/.local/tandem/REVIEW.md` at thread start and
again before committing. The worktree-local `.local/tandem/REVIEW.md` does not
exist; the main-workspace file is the active review source. These dispositions
apply to this analysis scope, not a rewriting of historical experiment verdicts.

- REVIEW R113: DEFER — no staging or ordinary/full-owner transition in this offline task; retain the current-state restriction.
- REVIEW R111: DEFER — prior memory-cache issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R110: DEFER — prior ACPI issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R109: DEFER — prior FV dispatch issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R108: DEFER — prior firmware issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R107: DEFER — prior boot issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R106: DEFER — prior transition/reserve issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R105: DEFER — allocation matrix is a prior boundary; current evidence crosses CreateDevice, no matrix rerun.
- REVIEW R104: DEFER — prior page-size issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R103: DEFER — prior allocation issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R102: DEFER — prior allocation issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R100: DEFER — prior allocation-map issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R99: DEFER — prior device-creation issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R98: DEFER — prior frame-size issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R97: ACCEPT — one exact-artifact comparison only; no older-reference archaeology.
- REVIEW R96: DEFER — prior TA/3D builder issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R95: DEFER — prior GPUVA audit issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R94: DEFER — prior GPU object ownership issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R91: DEFER — prior scanout issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R90: DEFER — R90a already retracts image-content inference; EXP848 uses saved zero-scanout evidence, no display experiment.
- REVIEW R88: DEFER — prior G5 issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R86: DEFER — prior broker capacity issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R85: ACCEPT — pin actual selected source/object/DLL provenance; EXP848 proves recording a projected hash alone is insufficient (A1–A3).
- REVIEW R74: DEFER — prior CDD context issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R71: DEFER — prior paging issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R69: DEFER — prior live-bind issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R65: DEFER — prior G4 ABI issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R64: DEFER — firmware reserve authorization does not authorize hardware or firmware edits in this task.
- REVIEW R63: DEFER — prior reserve issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R57: DEFER — prior paging-status issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R55: DEFER — prior PnP restart issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R54: DEFER — no hardware series is run; current user explicitly requires offline-only analysis.
- REVIEW R49: DEFER — prior caps issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R48: DEFER — prior serial ownership issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R47: DEFER — prior firmware profile issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R45: DEFER — prior cleanup issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R40: DEFER — prior watchdog issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R37: DEFER — prior recovery issue is outside the EXP848 compiler/churn boundary; no new claim or change is made.
- REVIEW R64: DEFER — G4 command ABI implementation is outside CPU compiler/diagnostic analysis; no new ABI is selected.

Verification performed: nine original evidence hashes/sizes match; builder and
host DLL hashes match; CDB loads the matching private PDB; full-symbol/map and
executable-section branch scan agree; parsed 87,216 UMD lines and paired all
685 CreateDevice entries; existing self-test projection test passes 1/1 while
not covering the discovered build defect. Companion CSV rows are checked against
the original log. No product tests/builds are claimed to establish a fix.

This report is a correction to interpreting `RUN EXP848 AFTER`: “1370
create-device” is a record count, and “DWM/present NOT reached” means **not
proven on Apple**, not that Windows emitted no Present. It also invalidates
using the presence of c410ade6 in a projected file as proof it was executed.
The EXP846 short no-AV window remains an observation; it is not retrospective
proof of self-test removal from EXP848's later binary.
