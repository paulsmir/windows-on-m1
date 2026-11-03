# Q21: late successor admission spends its time in fresh residency

Offline base `2fa2598e28815ad7752b711616b1e3070777d7fd`. The implemented
change is default off and has no hardware verdict. Handoff `q21-indirect-hold`
contains DESIGN.md, raw input hashes, decoded receipts, verification and REPORT.md.

Follow-up: [Q21b / EXP2114](D3D12-Q21b-map-paging-hold.md) shows that enabling
early reclamation did not remove the measured hold. Its successor instead
spends 21.560 ms on a fresh Encoder BO's map paging fence. The original
EXP2113 findings below are preserved; the follow-up distinguishes the two
wait stages and explains why the current mapping dependency must remain.

## What the trace establishes

EXP2113's head already uses a GPU-generated counted indirect stream. Its
Execute returns while the render is pending. The next observed operation spends
**21.970 ms in `resident_before_map`**, then 54 us in MapGpuVirtualAddress.
The evidence localizes the delay to fresh BO residency preparation, not to a
required CPU read of the indirect count. It does **not** separate MakeResident
callback time from the paging-fence wait inside that stage or identify the BO.

Evidence paths in this section are relative to the shared handoff folder
`.local/handoff/codex/q21-indirect-hold`; QPC frequency is 24 MHz.

| Boundary | QPC / value | Evidence |
|---|---:|---|
| Application pair begins | 1501405959 | `tool-q3pc-std.txt:30` |
| Head replay begins / ends | 1501625139 / 1501663431 | `tool-q3pc-umd.log:541` |
| Counted draw selected GPU stream | path 3, MaxCount 2, reason 0 | UMD `:526` |
| Head KMD submission | 1501688139, fence 3974 | stdout `:32` |
| Head owner queue bridge | ordered=1, owner fence 24 | UMD `:530` |
| Head submit call ends | 1501690011 | UMD `:541` |
| Head Execute timing / last profile | replay 1595 us, submit 1107 us / 1501692025 | UMD `:539,545` |
| Fresh bind timing receipt | 1502224647; residency 21970 us, map 54 us | UMD `:546`, last line |
| Head KMD completion | 1502219516 | stdout `:32` |
| Successor KMD submission | 1502235783, fence 3976 | stdout `:32` |
| Successor KMD completion | 1502253873 | stdout `:32` |
| Exact counted outputs | IA vertices 1048576, primitives 524288 | stdout `:35` |

Head KMD residence is **22.140708 ms**. Successor submission is **0.677792 ms**
after completion. The bind's approximate beginning, calculated from the receipt
QPC minus (21970+54)*24, is 1501696071: 0.3305 ms after head submission and
21.8102 ms before head completion. This is an estimate because the receipt QPC
is its emission time and microsecond durations truncate; it is not an exact
CPU-wait entry timestamp. The preceding 9.133 ms between application pair start
and head replay is separate and does not explain the missing successor.

`measure-gpuva-map-time`'s source is
`drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c:481-485,505-564`.
The first duration includes `residency_request` (MakeResident, possible trims)
and the accepted paging-fence wait. Its size word zero means **less than one
MiB**, not a zero-byte allocation. Map returns E_PENDING here, so a later map
paging dependency is also possible; that later wait is not measured by this
receipt. There is no completed successor Execute/profile record in the supplied
546-line log. Claiming an exact successor RenderFence or a specific allocation
from this capture would exceed the evidence.

The decoded `decisions-pair.json` retains one PID-8404 reason-66 aggregate:
count 3065, QPC 1484201197..1503509958, fences 3680..3985. It only partially
overlaps the measured pair, so 3065 is **not** the pair's exclusive poll count.
Reason 66 means no pending successor (`backend_platform_windows.c:5416`).
The progress ring is sampled/overflowed; stdout's explicit timing comparison
independently proves this successor was late. No reason-5 candidate is shown.

## Alternatives and the causal boundary

Paths in the rest of this note start at `drivers/apple-agx/`, unless marked.

| Candidate hold | Source / evidence | Conclusion for this pair |
|---|---|---|
| CPU count/argument read | `d3d12/agx_d3d12_ddi.cpp:6165` records buffer placements; `mesa/winsys/agx_d3d12_windows.cpp:4790-4904` passes both GPU resources; `mesa/scripts/native-asahi-indirect-stream.py:193-216` selects stream lowering | The measured path3/reason0 uses GPU inputs. CPU expansion for root/IA signatures and unsupported native count fallbacks exists, but is not selected here. |
| Indirect producer flush | `mesa/winsys/agx_win32_indirect_stream_emit.h:154-177,232-311` flushes writers, registers BO reads and launches the stream kernel with AGX_PREGFX | Producer ordering is retained; this is not a CPU readback. It happens before the head submission. |
| Synchronous final flush or owner retirement | `mesa/winsys/agx_d3d12_windows.cpp:5096-5142` flushes with flags 0, queues the O-to-Q wait, CPU-retires only if unordered | Head ordered=1 and its completed Execute timing precedes GPU completion by about 22 ms. Not the following delay. |
| Q19 tracking-window RenderFence | `mesa/winsys/agx_win32_gpuva_batch.c:533-543` waits only at the selected capacity; Q20 now defaults D3D12 to 16 | A real possible wait in general, but this trace directly records 21.970 ms inside residency, not this wait. Missing successor buckets cannot be assigned zero. |
| New allocation/residency/paging | `umd_gpuva_windows.c:427-449,505-532,545-564,891-933` | This is the measured long stage. The exact callback/wait split and resource identity remain unknown. |
| KMD prelude scheduling | `render-admission/src/render_job_window.c:95-111` refuses an otherwise eligible prelude pair with reason5; `backend_platform_windows.c:5416` emits reason66 when no successor exists | The successor has not arrived. The prelude gate is a later boundary and cannot explain its late submission. |
| KMD process quiescence during paging | `render-admission/src/gpuva_g3_paging_windows.c:1663-1697,1812-1847` waits for JobInFlight/LeaseToken before TLB/table mutation | A source-proven dependency of fresh paging, not proof that this exact 21.970 ms ran there. |

The raw `Wom1G3PagingWait` at receipt file line 188 decodes as 15 waits,
54 iterations, 0 timeouts, maximum 177429 ticks = 7.392875 ms, at QPC
454749291 (operation1, graph process3, active fence891). That is earlier than
the measured pair and smaller than 21.970 ms. The receipt is asynchronously
exported (`receipts.c:2233-2237,2389-2394`) and is not a per-call stack trace.
It supplies no positive attribution of this interval to the KMD wait loop.
VidMm callback/paging scheduling can also delay residency behind submitted
work. Do not label this an ExecuteIndirect-specific KMD hold.

## The confirmed allocation-order defect and fix

Primary Mesa `.local/reference/mesa/src/gallium/drivers/asahi/agx_batch.c:89-126`
allocates two pool slabs and an encoder in `agx_batch_init`. Its selection
routine at `:305-340` prefers a free slot before reclaiming submitted batches.
Our projection inserts `AgxWin32AsahiBatchBegin` only after those allocations
(`mesa/scripts/native-asahi-batch-lifecycle.py:141-148`). That Windows helper
finally cleans completed batches (`mesa/winsys/agx_win32_gpuva_batch.c:546-585`).

Consequently, a completed batch can still hold an exactly reusable BO when the
new batch asks the cache for it. The new BO incurs allocation, residency and
VA binding before the old batch releases its storage. This is a deterministic
ordering defect even when free batch slots remain and there is no memory
pressure. It can make a later job wait for an unrelated pending job through
fresh residency; hardware must determine whether it caused this capture's
unidentified allocation.

The existing cache retains the whole BO, including VA and residency
(`mesa/winsys/agx_win32_asahi_bo.c:317-370`). It requires matching size, flags
and class and excludes handles in both newest and older in-flight sets. No
larger cache or speculative spare allocation is needed to fix the ordering.

Set **`APPLE_AGX_D3D12_RECLAIM_BEFORE_BATCH=1`** before process creation.
Only exact `1` enables it. `agx_d3d12_windows.cpp:496-500,899` samples the
setting once; `agx_win32_asahi_bo.h:83-84` carries it in the per-owner backend.
Other frontends' zero-initialized backends stay off. Rebuild every translation
unit that includes this internal struct; mixed old/new archives are invalid.

`native-asahi-batch-lifecycle.py:167-184` adds a bounded cleanup pass **after
matching an active framebuffer and before selecting/allocating a new batch**.
It reuses the existing `agx_cleanup_batches` with Poll(0); pending completion
returns false before retirement (`agx_win32_gpuva_batch.c:1347-1368`). Cleanup
still snapshots timestamps, releases the capsule, finishes query bookkeeping,
and unreferences pools in its existing order. Failed polling or release cannot
make the submitted slot free. A cache miss retains ordinary allocation/paging.
The old BatchBegin cleanup is preserved for the default path and completions
that occur after the early pass.

This does not reduce draw counts, read count data on the CPU, overwrite busy
storage, remove a required wait, change the owner queue bridge, or enable
prelude pairs. Its intended outcome is earlier CPU admission when reusable
completed storage would otherwise be returned too late. It does not promise
`next_submit < head_complete` for arbitrary allocation pressure.

## Windows contract and layer ownership

Pinned WDK `.local/reference/wdk26100/d3d12umddi.h:2468-2474` defines MaxCount
and GPU argument/count placements. Microsoft's local DirectX-Graphics-Samples
`dxgs-68907e31/Samples/Desktop/D3D12ExecuteIndirect/src/D3D12ExecuteIndirect.cpp:625-644`
submits compute-produced arguments and uses GPU queue Signal/Wait;
`:763-817` transitions to INDIRECT_ARGUMENT and passes a GPU counter to
ExecuteIndirect. Its `compute.hlsl` appends surviving commands on the GPU.

The [ExecuteIndirect contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-executeindirect)
and [indirect drawing overview](https://learn.microsoft.com/en-us/windows/win32/direct3d12/indirect-drawing)
describe argument/count buffer consumption and MaxCommandCount clamping.
[ExecuteCommandLists](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists)
requires GPU ordering between separate Execute calls, not CPU retirement before
early enqueue. Links are for integration review; this offline task used local
primary headers/sample implementations and made no network requests.

The CPU must still wait before reading or overwriting pending data. The
[paging callback contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_makeresidentcb)
returns a paging dependency; pinned SDK `d3dukmdt.h:1475-1488,1596-1610` and
`d3dumddi.h:3838-3850` define paging fences and the null-event CPU wait. Current
CPU preparation/native readiness needs completion. Deleting that wait or the
pending owner-to-runtime bridge would violate the contracts analyzed in Q11/Q18.

Asahi/UMD own batch lifetime and replay; VidMm owns placement/residency;
VidSch/KMD own GPU scheduling, DMA validation, interrupts and recovery.
Firmware, m1n1, Mu, ADT, ACPI, power and initial mappings are unchanged and are
outside this offline causal boundary. The saved EXP2110/2113 launch contracts
are the references. No external implementation was copied into tracked code.

## Performance estimate and integration checkpoint

The task's EXP2110 frame budget has four prelude renders with approximately
0.53 ms launch cost each: **2.12 ms/frame** is the render-launch opportunity
if a separately qualified scheduling change hides all four. Against the
provided 27 ms HD budget, that idealized scenario is 27 -> 24.88 ms, or +8.5%
throughput. Against EXP2110's score-derived periods (35.6/28.7 FPS), the same
2.12 ms scenario gives about 38.51/30.56 FPS and proportional scores about
6435/5114 versus 5949/4803. Hiding all 4.4 ms of launches is a different, much
more optimistic scenario. These launch intervals are not measured CPU idle
and cannot be added to the 21.970 ms residency wait as a frame saving.

**Expected gain from this patch alone is unproven and can be zero.** It leaves
the prelude gate off. Earlier admission may improve preparation latency but
cannot itself authorize overlap for prelude pairs. Do not claim 2.12 or 4.4 ms
as this patch's measured saving or infer the exact allocation from a launch gap.

WHY THIS HYPOTHESIS:
1. Head Execute already returned; the later recorded bind spends 21.970 ms in
   residency during the head's lifetime.
2. The host regression reproduces an avoidable fresh allocation when completed
   batch storage is reclaimed after allocations, and eliminates it with early
   reclaim while a newer batch remains pending.
3. The existing GPU indirect stream already honors counts; replacing its
   semantics or removing its queue-ordering edges has no supporting evidence.

Integration owner must first native-build/link x86, x64 and ARM64, pin source,
package, manifest, firmware and known-good recovery hashes, and preregister.
With EXP2113 settings and **Prelude still off**, vary only this UMD switch.
Q3PC must show exact pixels, compute words and IA counts (1048576/524288),
path3/reason0, no reset/removal and `head_submit < next_submit < head_complete`.
Require a PID/fence-matched reason5 candidate. Exact output with a late successor
is INCONCLUSIVE. Early arrival is not physical overlap and does not permit
lifting the prelude gate left after EXP2100's firmware crash.

Then compare GravityMark HD/FHD 100k temporal0 with EXP2110 5949/4803 under
identical settings; collect full UMD/stdout, BO identity where available,
completion/decision/reset evidence and images. If the long residency stage
remains, identify its allocation from the same behavior experiment rather
than repeating archaeology. A genuinely new allocation or pending storage
needs another design, not a weakened paging check. Preserve the integration
owner's dump-first exact-package rollback to ordinary GPU-visible Code28;
emergency-hidden recovery remains the fallback. No hardware action here.

## Verification limits

The new test compiles actual projected selection/cleanup and real BO-cache
functions with deterministic allocation/completion callbacks. RED fails on the
original allocation order; GREEN checks early reuse, default off, matching
active batches, pending/unknown completion, failed cleanup, multiple completed
slots and render/compute keys. Existing real GPU-stream kernel tests cover zero,
clamped, indexed/nonindexed counts and statistics; existing runtime tests cover
real fence retirement and held sets. Simulated query totals in the new test
are lifetime checks, not a new GPU execution oracle.

Full results and artifacts are recorded in REPORT.md. Local Windows-target
COFF compilation covers all three architectures, including complete projected
batch/state units. It does not establish native MSVC /W4 /WX, UMD DLL linking,
package signing, on-device output, this capture's exact wait leaf or performance.
