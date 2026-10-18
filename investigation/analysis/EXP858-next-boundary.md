# EXP858 — first local-copy QUERY, deferred destruction, and Explorer attribution

Offline analysis, 2026-09-28. Authority: main `.local/tandem/NEXT_TASK_ANALYSIS_858.md`.
Base `d84c9590`; package858 source `f031ef2a527e55ee0bdb76c79e7e7b1801e51e3e`.
All 535 packaged source entries match this worktree. All 13 saved guest originals
match their recorded SHA-256. No Air connection, firmware/driver change, package
build, installation, or new hardware experiment occurred. The builder only decoded
saved ETL/dumps and compiled the offline ETL reader.

**Finding:** the captured native submission attempts reach R145's first local-copy
QUERY, then abandon the transfer before any recorded UPLOAD or SubmitCommandCb.
The strongest next target is this UMD/KMD copy boundary, not Draw admission,
shader allocation, the former ParseUnmapped parser failure, or GPU execution.
The precise rejecting predicate and HRESULT/NTSTATUS were not retained. Do not
invent an E_INVALIDARG/OOM result or change placement/firmware on this evidence.

## Evidence and coverage

Raw evidence lives under main-repository `.local/experiments/`:

- `EXP858-r145-staging/hardware-evidence/{umd.log,EXP801DxgBoot.etl,WER-explorer.exe.2788.dmp}`;
  exact package858 DLL/PDB are in its parent directory.
- `EXP858-offline-analysis/`: decoder commands, XML, JSON, native ETW stacks,
  CDB output, input verification, and exact extraction programs.
- Tracked [`summary.json`](../evidence/EXP858-next-boundary/summary.json) contains
  hashes, counts, selected timestamped records and lifetime-join examples.
  [`analyze.py`](../evidence/EXP858-next-boundary/analyze.py) reproduces the census
  from those saved decoded inputs; every selected event count is checked against
  the independent native ProcessTrace reader.

Native decoder output (`raw-stacks.txt:1`, SUMMARY near its end):

```text
HEADER lost=0 bufferslost=6 start=134350434083122715 end=134350435533085310
SUMMARY status=0 events=1471211 extended=1471209 stacks=1471209 reason_events=3
```

Selected decoded events span `04:30:37.2612426Z–04:32:01.3688337Z` (84.108 s).
The ETL is exactly 512 MiB, the configured sequential AutoLogger maximum
(`EXP858-r145-staging/autologger-stage.ps1:25`). Its header end is 04:32:33.308531Z;
this is not coverage through the 04:40:41 final counter. Six lost buffers and
seven overwritten live allocation-start pointers qualify negative findings and
the allocation census. Event IDs below are DxgKrnl IDs; `record` is the XML
EventRecordID, whereas `seq` is the native decoder's sequence (they differ).

## 1. Submission boundary and per-process reconstruction

### The apparent last Draw/Map boundary was the log budget

`umd_runtime_device.c:41–82` has a DLL-static `records` counter. Lines 59–63
drop every non-`reject-` record after 128 entries, even if its HRESULT is a
failure. `g4-submit-command-cb`, staging operations, `draw-after`, and ordinary
runtime-error records have no exemption. Refusal-prefixed messages remain enabled.

| PID | Recorded CreateDevice entry/exit pairs | draw-before | UMD lines | Relevant evidence |
|---|---:|---:|---:|---|
| 1232 (DWM) | 2 | 0 | 128 | Budget exhausted during startup; ETL later contains 25 copy QUERY calls |
| 1224 | 64 | 62 | 8,026 | ETL representative sequence below; 11 QUERY calls in captured interval |
| 4720 | 1 | 1 | 128 | 1 QUERY call |
| 2788 (Explorer) | 1 | 0 | 128 | 49 QUERY calls; later saved full dump |
| 5680 | 44 | 43 | 5,605 | 2 QUERY calls |
| 5756 | 1 | 1 | 128 | No matching copy QUERY stack in captured interval |
| 7780 | 1 | 1 | 128 | Same coverage limitation |
| 2112 (replacement Explorer) | 1 | 0 | 128 | Later than captured ETL |
| 8148 | 405 | 405 | 51,840 | Exactly 405 blocks of 128 records; later than captured ETL |

PID2456 has four adapter-only records. PID5880 has five copy QUERY calls in ETL
but no records in this UMD log. Thus the UMD file is not a complete process/device
inventory. DLL unload/reload can reset the counter; its blocks are not DWM restarts.
The 520 pairs are 520 entries **and** 520 exits, not 520 total lines.

For example, PID8148 lines14408–14421 create a device successfully; lines14515–
14518 enter Draw; lines14519–14531 allocate general/encoder/shader BOs and reserve
the final shader VA successfully. That is record128 in the block. Line14532
starts another load's output. There is no evidence that Map failed there.
`0x8000000a` on earlier Map results is the documented successful asynchronous
`E_PENDING`; the code explicitly handles it (`umd_gpuva_windows.c:95–104`).

### ETL and exact858 symbols identify the new boundary

Join Device(ID27) → Context(ID30) → QueuePacket/DmaPacket(ID178/175) by ordered
lifetimes. Apple is adapter `ffffdc0895be0000`, registered at04:30:42.1553542Z;
the software render adapter is `ffffdc0893143000`. Apple has 8,109 queue packets
of type8 and 8,106 DMA starts of type1 (paging). All 223 type0 render DMA starts
and their queue submissions belong to the software adapter. No Apple render
DMA start is recorded in the captured interval. Sixty-eight type6 queue packets
have no context-start join; they are not counted as Apple render execution.

There are 93 MakeResident start/stop pairs, all returning NTSTATUS `0x103`
(259, pending), zero trim bytes. There are also exactly **93 copy_escape DDI
entry stacks**, by PID:2788=49,1232=25,1224=11,5880=5,5680=2,4720=1.
Every parent return address is `00007ffd7200be04`, the QUERY callsite;
none is the UPLOAD/DOWNLOAD callsite. Extraction:
`read-copy-calls.cpp` → `native-copy-calls.ps1` → `copy-call-stacks.txt`.
This is an address-qualified count for exact858, not an invented packet identity.

The first representative sequence, PID1224/TID5908, is particularly explicit:

| UTC | Evidence | Meaning |
|---|---|---|
| 04:30:43.3429545 | ID338, record50549, NumAllocations13 | MakeResident starts |
| 04:30:43.3430044 | ID339, record50585, Status259, fence7037 | Asynchronous residency accepted |
| 04:30:43.3430178 | ID297, record50594, fence7037 | CPU paging wait |
| 04:30:43.5147585 | Native seq53477, profiler106/function0x7fb | That wait returns; it did not remain stuck |
| 04:30:43.5148304 | Native seq53482, profiler105/function0x7e0 | Escape enters through copy_escape |
| 04:30:43.5274880–43.5275050 | Native seq53586/53587, profiler105/106/function0x139e | Miniport Escape wrapper bracket |
| 04:30:43.5275384 | Native seq53591 | Escape returns |
| 04:30:43.5275742 | Native seq53594 | Evict from the failed-submit branch |
| 04:30:43.5275827–43.5276427 | ID367 | Resident usage drains 1,703,936→0 bytes |
| 04:30:43.5383896 onward | ID42 then ID39 | Unlock and allocation termination requests |

`boundary-stacks.txt` contains the raw stack after the QUERY:

```text
00007ffd7200af60  copy_escape+0x90
00007ffd7200be04  transfer_slot+0x9c
00007ffd7200bc14  submit+0x194
00007ffd7200c990  AgxWin32GpuvaSubmit+0x300
```

The subsequent eviction has `evict+0x68` / `AgxWin32GpuvaSubmit+0x360`.
`dump-boundary.txt` resolves these with package858's PDB and shows actual call
instructions: at `7200bdf4`, Operation is zero/QUERY; `7200be00` calls copy_escape;
at `7200be0c/14/1c`, failure or zero generation branches to cleanup.
`7200c9ec` invokes Evict on the failed-submit path. The next private escape is
`AgxWin32AsahiBatchRelease+0xd0`, reached through `DrawIndexedInstanced+0x108`.
Thus execution advanced beyond the logged Draw, through native submission
preparation, to copy preflight and rollback. It is not a Draw-before dead end.

Source ownership:

- `umd_gpuva_windows.c:243–276`: QUERY, generation checks, staging CPU address/
  optional Lock, then the first UPLOAD. `:239` collapses Escape's HRESULT to a
  boolean. `:251` further combines it with two output-generation checks.
- `umd_gpuva_windows.c:357–365`: `transfer_held(false)` precedes SubmitCommandCb.
  On failure the callback is never invoked; readback requires later successful
  submission and completion (`:379–383`).
- `agx_win32_gpuva.c:139–152`: failed transport Submit evicts and returns false.
- `callbacks.c:246–257` dispatches COPY magic to KMD
  `gpuva_g3_windows.c:517–627`. KMD owns handle/context/process checks, live
  ResidentPte/allocation identity, segment2 and range/provenance validation.

**First failing call/status:** the first observed unsuccessful operation is the
local-copy pre-upload transaction beginning with
`pfnEscapeCb(APPLE_AGX_G3_COPY_QUERY)`, followed by false from the transport submit.
The captured return branch proves transfer failure; it does **not** retain W0,
the request/response fields, or the underlying HRESULT/NTSTATUS. QUERY rejection
is strongest, but successful QUERY with invalid generations/CPU staging state,
or a runtime-rejected staging Lock that never entered the kernel, cannot be
excluded. The exact first failing callback and HRESULT remain **undetermined**.
No evidence supports naming allocation OOM, SetError, or device removal as this
transaction's first error. There are no `reject-*` records in the UMD file.

The earliest explicit ETL failure is unrelated to the new copy path: DWM Present
at04:30:41.4441881Z returns `0xc00002b6` (STATUS_DEVICE_REMOVED), then Reason6/
FromUserMode=true on device`ffffa2857cc9f010`. That device belongs to the software
adapter, before Apple registration. WDK26100 `d3dkmthk.h:5402` defines user reason
`0x80000006` as DRIVER_ERROR. Do not turn this into an Apple submit failure.
Two later Reason14 events are on PIDs7616/7728; no symbolic reason is guessed.

### Explorer's paging-wait snapshots are a separate observation

The 04:33:37Z dump contains two threads in `WaitForSynchronizationObjectFromCpuCB`
called by `wait_paging` → `AgxWin32GpuvaBind`:

| Thread | Caller | BO token / VA / bytes | Waited fence | CPU fence in dump |
|---|---|---|---|---|
| 57/TID7208 | ResourceCopyRegion → util_blitter → u_upload | 15 /0xf80000/1MiB | 0x1b67 | 0x1b67 |
| 61/TID5252 | XAML DrawDummyText → Draw → agx_fast_link | 20 /0x280000/64KiB | 0x1b6c | 0x1b6c |

Both runtime owners have LastScreenError0, DrawTerminal0, ScreenClosing0,
no native transaction, and GPUVA Held=NULL/RenderFence0/Terminal0.
`dump-detail.txt`, `dump-owners.txt`, `dump-runtime.txt` retain the values.
The targets are already visible at the CPU fence addresses. A single snapshot
cannot prove duration, a missed wakeup, or an unsignalled fence; the ETL ends
before this dump and cannot join these particular owner lifetimes. This does
not overturn the earlier completed-wait → QUERY → rollback sequence.

## 2. Committed memory: observable deferred destruction, not a proven staging leak

Replay ID33/34 global allocation starts/stops, ID36 device allocation joins,
ID39 termination requests and ID27/28 device lifetimes in time order. Do not
subtract destruction **PID** from creation PID: most stops run on PID4.
Seven starts reuse a still-live pointer without its stop in the retained trace;
the replacement starts a new lifetime. No unmatched Apple stop exists.

Observed Apple totals:14,578 starts;14,043 matched final destructions;528 last
observed live records totaling492,707,840 bytes (469.8828125MiB). This is an
event-derived allocation census, **not** the GPU performance counter.

| Last-observed allocation shape | Count | MiB | Attribution limit |
|---|---:|---:|---|
| Flags0x8000, local-write-set2, no section at creation | 198 | 55.3125 | Canonical-shaped, including imported targets; no Mesa class field |
| Flags0x8001, aperture-write-set1 | 203 | 26.375 | CPU staging-shaped; no token/borrowed-owner join |
| Flags0x20008000, local-write-set2, section-backed | 85 | 353.75 | VidMm/global records; do not invent a meaning for reserved bit29 |
| Page tables, primary/shadow and other shapes | 42 | 34.4453125 | Separately counted in JSON |

Of the third row, **80 allocations/333.75MiB have both a termination request
and their owning device's stop recorded**, yet no final ID34. Typical size is
4MiB; others include4.1875MiB and15.875MiB. These retained bytes cannot be
explained solely by a missing UMD DestroyDevice call or forgotten staging
deallocation. They are observed deferred global destruction. Another4MiB has a
termination request with its device still present; one4MiB stopped-device record
lacks a captured termination request. Three4MiB records remain on active devices.

There are also7,030 canonical-shaped and6,728 staging-shaped **completed**
destruction pairs. Across all14,043 pairs, median lifetime is0.870784s, maximum
22.132963s. The earlier assertion that allocations simply are never freed during
churn would be false. The reason some global allocations stay deferred is not
recorded: queued paging/reference retirement and held CPU mappings are candidates,
not a proved driver or VidMm defect.

Source supports paired ownership: `umd_win32_screen.c:579–620` allocates CPU
staging after canonical allocation and rolls back both owned handles on failure;
`:888–914` deallocates canonical then owned staging, skipping only borrowed
display ownership. `agx_d3d10_windows.cpp:250–291,459–480` owns native/runtime
teardown and terminal retention. `gpuva_g3_windows.c:621–627` releases acquired
handle references on its common exit. These checks do not prove all real cleanup
callbacks succeeded; their statuses are missing.

Microsoft explicitly documents asynchronous VidMm destruction until prior queued
work finishes in [Allocation usage tracking](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/allocation-usage-tracking).
This makes the ID39→delayed-ID34 distinction material. It does not justify setting
AssumeNotInUse or forcing free while ownership remains uncertain.

The later counter rises to3,242,876,928 bytes at601.3s, while sampled local resident
usage peaks26,853,376 bytes. The ETL stops before the first90.7s counter sample;
therefore it cannot allocate the full later1.65→2.06→3.02GiB increase among BO
classes or distinguish finite deferral from an unbounded leak. Verdict:
**deferred destruction demonstrated in the captured interval; an R145 staging
leak and the cause of the full600s commitment growth are not established.**

## 3. Explorer crash

Exact saved full dump, Microsoft public symbols, and matching package858 PDB:

```text
Exception c0000005, PC00007ffda35ba614 = msvcp_win+0xa614
msvcp_win!std::locale::_Locimp::vector deleting destructor+0x64
Windows_CloudStore!Utf8StringToWideString::dynamic atexit destructor
Windows_CloudStore!_dyn_tls_dtor+0x68
ntdll!ImageTlsCallbackCaller / LdrpCallTlsInitializers / LdrShutdownThread
ntdll!RtlExitUserThread / TppWorkerThread
```

`ldr x8,[x0]` reads facet object`0x019dafa0`; its first qword is
`00000000a366f128`. The faulting `ldr x8,[x8,#0x10]` tries to read
`0xa366f138`. Restoring the missing upper bits would yield
`00007ffda366f128`, which the exact Microsoft PDB names as
`std::time_put<unsigned short,...>::vftable`. This is evidence of an invalid,
apparently truncated facet vptr, not a valid object dispatch.

Immediate cause: corrupt facet pointer consumed during CloudStore TLS locale
destruction on a worker thread. **Original corrupting writer is unknown.**
No Apple frame is on the exception stack; Apple is loaded and two other threads
are in its paging waits. Neither fact proves nor excludes a previous UMD memory
overwrite. No original allocation/free/write history or verifier/PageHeap history
exists in this dump. Thus “our UMD caused it” and “Windows is proven responsible”
are both unsupported. This is distinct from the EXP855D NULL shader BO crash.

## 4. Ranked hypotheses and smallest next work

1. **KMD local-copy QUERY rejects real allocation/process/PTE provenance.**
   Strongest:93 first-QUERY callsites, no upload callsites; the representative
   request reaches the miniport Escape bracket then immediately rolls residency
   back; this path is new in R145 and precedes the old parser failure. Inspect
   `gpuva_g3_windows.c:533–604` only after a receipt names the first failed guard.
2. **UMD pre-upload preparation fails after QUERY** (zero returned generation,
   invalid CPU mapping, or runtime-side staging Lock refusal). Same source window
   and absent return fields permit it, but there is no captured staging Lock
   between QUERY and rollback. It ranks below a QUERY validation refusal.
3. **Paging completion/wakeup or retirement delays amplify churn/commitment.**
   Deferred global allocations and the later wait snapshots justify measuring it.
   It cannot explain the representative earlier attempt as a permanently blocked
   wait, because that wait returned and QUERY executed.

No behavioral fix is justified yet. The smallest instrumentation proposal is one
bounded, first-failure **copy-preflight** receipt, outside the128 startup budget:

- UMD records exact HRESULT (before converting it to bool), QUERY/Lock/UPLOAD
  stage, owner generation, token, canonical/staging handles, GPUVA/bytes,
  mapped/borrowed flags, process/mapping generations, and paging target/current.
  Capture the first failure or first success of each stage per owner, with an
  explicit dropped-record count and UTC/QPC; do not merely raise the chatter cap.
- KMD returns or durably records a first-failed-guard identifier and NTSTATUS for
  this COPY request: envelope, acquire-handle, owner/context, allocation intent,
  busy/generation, graph access, local view, or per-page allocation/offset/segment
  join. Record the failing page's identities, not raw user memory or GPU data.
  Use a versioned diagnostic sidecar if the existing COPY ABI cannot carry it.
- Add allocation/deallocation and DestroyDevice first-failure counters keyed by
  the same owner/token, including outstanding owned bytes and holds at teardown.
  A later allocation census must cover the same timestamps as the memory counters.

Deterministic regression design: extend the real Windows callback replay
(`tests/r145_screen_replay_windows.cpp`,
`drivers/apple-agx/render-admission/umd/tests/umd_gpuva_contract_windows.c`) and
real KMD copy replay (`tests/g3_r145_copy_cases.c`). Exhaust128 startup records;
inject distinct QUERY HRESULT, zero-generation response, Lock failure, upload
failure and successful copy. Assert the first failing stage and **original** code
survive, successful-call ordering/return values are unchanged, failure performs
no SubmitCommandCb, rollback releases both identities correctly, and diagnostic
storage is bounded. KMD tests mutate each provenance guard independently and
check its receipt before any store. These are proposed tests, not executed fixes.

Ownership remains: UMD staging/lifetime/synchronization; VidMm residency/paging/
deferred destruction; KMD copy authorization/translation; m1n1 hardware/grants;
Mu reservation/ACPI. Source-first references inspected include Asahi
`mmu.rs:486` (native-page-aligned mapping), current m1n1 `hv_agx_gpuva_v5.c:38–57`
(epoch/envelope ownership), Mu T810X `MemoryInitPeiLib.c:487` (reserved-memory
HOB), and pinned WDK26100 plus Microsoft
[Map callback](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_mapgpuvirtualaddresscb),
[CPU wait](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_waitforsynchronizationobjectfromcpucb),
and [Deallocate callback](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_deallocatecb)
contracts. Asahi's mapping contract supplies no Windows callback result; Mu/m1n1
cannot explain an erased UMD HRESULT. EXP857's one logged E_INVALIDARG submit
at UMD line653 and ParseUnmapped receipt remain its verdict; EXP858 regresses
to a newly identified pre-upload boundary in the recorded attempts.

Smallest later falsifiable checkpoint, **not authorized here**: one first-copy
receipt resolving QUERY vs CPU-preparation failure, on identical R143 firmware,
before any GPU execution claim. Recovery remains EXP858's accepted immutable
hidden exact cleanup after Code0, then ordinary GPU-visible Code28. No new live
state measurement, firmware change or Air run is needed for this analysis.

## Verification and review

Evidence hashes, independent decoder/XML counts, callsite disassembly, and
allocation/device lifetime joins were checked. No runtime source changed, so
there is no behavioral RED→GREEN claim or new full-suite result; the recorded
1154-test baseline15F/38E/2S is not re-labelled green. Ledger schema and diff
checks are run before the commits. All OPEN tandem items are answered in
[`EXP858-review-dispositions.md`](EXP858-review-dispositions.md).
