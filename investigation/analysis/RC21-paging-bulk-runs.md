# RC21 — coalesce resolved system-memory paging runs

Offline candidate on codex/rc21, base 4405397f. No hardware verdict.

## Evidence and decision

The supplied EXP1956 observation reports ~13.8 s between trimmed MakeResident
and the next UMD event, following release of 0x0eb70000 bytes of references.
235 MiB / 13.8 s is ~17 MiB/s, but released-reference bytes are not measured
physical page-in/out bytes. This is an effective-rate estimate, not a memcpy
measurement. RC17's report explicitly retains that distinction. RC13's original
per-operation retry interpretation is corrected by RC16: aggregate slot39 must
not be added to individual slots. RC16 doubled private capacity, RC17 bounded
trim overshoot, yet long waits remain. Neither report measures PFN contiguity.

WHY THIS HYPOTHESIS:
1. The actual encoder previously allowed multi-page records only if both
   endpoints were memory segments 2/3. Every system endpoint forced 4 KiB,
   even when the logical PTEs named consecutive physical pages.
2. Each such record maps, copies, barriers and unmaps system memory. With 64
   records a buffer carries only 256 KiB. Capacity alone leaves that per-page
   work intact, consistent with RC16's retry reduction without clear speedup.
3. The real encoder regression reproduces 16 records for a contiguous 64 KiB
   transfer. The corrected encoder emits one with the same exact endpoints.

Decision: extend the existing bounded physical-run optimization to resolved
segment-0 backing. Keep record count, ABI, public/private sizes and scheduler
unchanged. Do not invent discard permission, change fill semantics, or add a
GPU blit: the paging process deliberately has no GPU-visible leaves.

## End-to-end source evidence (paths relative to drivers/apple-agx)

- `render-admission/src/paging_windows.c:38`: BuildPagingBuffer validates the
  memory runtime; :52 routes VirtualTransfer/VirtualFill to the G3 encoder.
  :86 handles physical Transfer with an MDL, direction/offset/size plan; :13
  encodes the plan and MDL into one private record. :134 handles DiscardContent
  only when VidMm explicitly supplies it. :227 wraps status/profile receipts.
- `render-admission/src/gpuva_g3_paging_windows.c:1022`: virtual encoder. :1051
  retains rc8's Src64KBPages/Dst64KBPages acceptance and rejects reserved bits.
  :1060 checks total and VA bounds; :1071 retains the 64-record capacity and
  both public/private buffer limits. :1089 starts at exact MultipassOffset.
- Same file :973 resolves the CPU logical PTE's GuestIpa and access permission,
  not NativeIpa. :1007 resolves an aperture offset to its original backing.
  :1117 checks transfer direction. :1142 extends a run only after each next
  logical page resolves, keeps its segment identity, and is physically adjacent
  on both endpoints. Unequal VA page offsets conservatively stay page-sized;
  aperture offsets are not coalesced across unresolved entries. :1176 retains
  pool lifetime accounting. :1187 returns exact continuation progress.
- `render-admission/src/gpuva_g3_windows.c:3267`: SubmitCommandVirtual paging
  admission validates context, flags, fence, DMA/private sizes and record set;
  :3318 queues the private records through AdmissionCpuQueueSubmit.
- `render-admission/src/work_queue_windows.c:3`: serial CPU queue admission;
  :106 copies records into the active packet; :134 queues the paging worker.
- `render-admission/src/paging_windows.c:315`: PASSIVE worker executes each
  record in order. Virtual records use AdmissionG3ExecuteVirtualPaging (:343),
  physical records use AdmissionMemoryRuntimeExecutePaging (:347).
- `render-admission/src/gpuva_g3_paging_windows.c:1193`: existing local/pool
  CPU views are reused; resolved system runs get one page-rounded MmCached
  mapping at :1229. :1286 performs one RtlMoveMemory per run; :1288 fills the
  requested pattern without changing phase. :1299 barriers before matching
  unmaps. There is no explicit hypervisor command or GPU job in this executor.
- `render-admission/src/memory_runtime_windows.c:1061`: physical Transfer
  already uses MmGetSystemAddressForMdlSafe and AppleAgxPhysicalPagingExecute
  on the complete range; mapping release is conditional on ownership. It
  does not need a new MDL-range format for this fix.
- `render-admission/src/paging_windows.c:357`: completion recorded only after
  successful execution; :368 synchronizes notification. :275 selects
  DMA_COMPLETED with the submission fence, or a fault, then :301 notifies
  DxgKrnl and :311 queues a DPC. Fence notification was already once per
  buffer; this patch does not remove or move it.

## Primary contracts and ownership

Pinned WDK `.local/reference/wdk26100/d3dkmddi.h:4711` defines virtual fills;
:4720 the three directions; :4727 the two page-size flags and reserved bits;
:4741 the virtual transfer addresses/size. Neither struct supplies an MDL.
The encoder must derive actual contiguous backing from its logical tables,
not infer it from page-size flags. Both private-capacity query expressions
remain the rc16 64*sizeof(record), 8192 bytes on the target ABI.

Asahi Linux `.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi/gem.rs:84`
uses an owned GEM vmap; Mesa `src/gallium/drivers/asahi/agx_blit.c:618` uses
compute for ordinary buffer copies and :662 has a CPU fallback. These are
not Windows VidMm paging callbacks; their GPU blits cannot be transplanted
into a CPU-only paging context. No external implementation was copied.

VidMm owns residency, locked backing lifetime and request ordering. KMD owns
logical translation, CPU mapping/copy, queue completion and error propagation.
UMD owns reference selection and waiting on the paging fence. Initialization,
power, DMA exposure, interrupt routing, stage-2, firmware and recovery remain
with their existing owners. m1n1/Mu submodules are unpopulated here; the pinned
m1n1 object is also absent from the local submodule store. No new platform or
hardware contract is inferred. Current source and the supplied saved hardware
observations suffice to test the CPU record defect; native execution remains
an integration gate.

## Cost model and limits

Before: for B aligned bytes, N=B/4096 records/maps/unmaps/barriers, roughly
ceil(N/64) CPU queue/worker/completion cycles. At 235 MiB this is 60,160
records and 940 buffers per direction. Dividing the observed 13.8 s by those
counts gives 229 us/page or 14.7 ms/buffer **only if all time and bytes belonged
to this one-way transfer**. These are equivalent amortizations, not isolated
measurements. If equal eviction and page-in bytes were copied, both counts
would double and those equivalents halve.

After: a physically contiguous run R reduces mappings and records by R/4 KiB,
up to 1024x at 4 MiB; 64 KiB groups give 16x. At 235 MiB, ideal continuous
backing uses 59 records (one buffer); 64 KiB runs use 3760 (59 buffers).
Scattered PFNs remain unchanged. Per-page PTE validation still runs, as does
bulk memory traffic; cache attributes and write-combined reads are unchanged.
No measured Windows map latency or PFN histogram is available. A larger
mapping can fail under system-PTE pressure; that failure remains explicit.

The optional host benchmark runs real encoder/executor on ordinary host
arrays with ASan/UBSan. OS map/unmap are checked stubs, not Windows VM work;
its bytes/s is not a guest performance prediction. Operation counts are the
useful deterministic result: 256 MiB uses 64 buffers/maps/unmaps for 4 MiB
runs, versus 1024 buffers and 65536 maps/unmaps under the old page policy.

## Regression and smallest hardware checkpoint

Tests cover all directions/page hints, exact 65 scattered-page continuation
at the rc16 capacity, original versus native backing identity, >4 MiB runs,
partial-page boundaries, unequal offsets, fills, map failure and exact map/
unmap sizes. Source-only tests also retain paging resubmission and CRT guards.
The G3 integrated broker replay cannot run without its pinned m1n1 sources;
its coalescing expectations are updated but that suite is not claimed green.

Integration owner: build base 4405397f plus RC21 commits with the pinned WDK,
record package/SYS/manifest hashes and retain known-good recovery. Keep
Wom1GpuFaultInfoRead=1 and Wom1PoolFraction=3; otherwise preserve EXP1956's
profile. Run 2k first, then 10k after successful scene rendering with no
content corruption/removal/DWM regression. Primary metrics: scene presents/min
(EXP1956 reference 6.4/min) and longest paging wait (~13.8 s reference), using
matching Ready-to-end windows. Capture per-arm paging profile/DDI deltas and
VidMm ETW bytes/timing, plus images. Fewer records without shorter waits rejects
per-page overhead as the dominant cause. Collect evidence before ordinary
GPU-visible exact-package recovery. No hardware run was performed here.

WHAT IS STILL UNKNOWN: real PFN run lengths, physical bytes copied, native WDK
compile, Windows mapping/worker latency, cache-limited copy bandwidth, and the
actual effect on scene throughput. This is a correctness-tested optimization
candidate, not a hardware-proven resolution of the 17 MB/s estimate.
