# EXP1932: preserve the fault address’s current mapping

## Evidence and decision

EXP1932 BEFORE is at `investigation/EXPERIMENTS.md:62546`. Saved evidence root:
`.local/experiments/EXP1932-trim-private/evidence/EXP1932`.

- `kmd-receipts-probe-end.txt:189`: one valid unmapped read, VA
  `0x1112680000`, VM1, PID4104, fence `0x13d29`, 44 ms from kick.
- Same file `:118`: 929 successful updates omitted 13331 native groups;
  retained events 922–929 belong to DWM/Explorer and do not cover that VA.
- Same file `:187`: a different failure: G4 Parse/Unmapped in Explorer
  PID5168, VA `0x11034f0000`, a 512-KiB Encoder allocation, flags6,
  CpuVisible1. Its four system IPAs descend by 4 KiB; no native leaf exists.
- Same file `:161`: half-pool 1344 MiB, no reserve/floor/commit failures;
  `:188,191`: one firmware recovery, later deadline expiry and three failed
  resets at quiescence. First run app output/UMD trace and dump are absent.

The workload-stability hypothesis is rejected. The claimed private-map trim
benefit is inconclusive without its trace. Do not attribute the PID4104 fault
to Explorer’s known unsupported mapping, or revert correct lifetime policy
without evidence. The previous run changed behavior, so one receipt-only
experiment at this fault boundary satisfies the offline handoff rule.

## Windows / AGX contract and ownership

`wdk26100/d3dkmddi.h:4693` defines the update’s process/allocation identity,
allocation offset, VA and PTE entries. VidMm owns residency and these ordered
updates; the KMD owns the CPU logical shadow and native graph. In
`gpuva_g3_paging_windows.c:684`, four equal-permission, same-segment, aligned,
contiguous 4-KiB pages are needed for a native 16-KiB leaf. Successful logical
updates can omit native leaves (`:751`, `:803`).

`gpuva_g3_windows.c:2835` / `:2964` establishes the graph lease and active
process/fence; completion releases ownership at `:3110`. Primary graph reads
are `shared/src/apple_agx_gpuva_g3_graph.c:629,653`. Asahi obtains fault info
before recovery (`asahi-linux-asahi/drivers/gpu/drm/asahi/gpu.rs:1422,1443`),
with G13 address/VM fields in `regs.rs:374`. No external code is copied.

m1n1 owns broker publication/TLB and Mu owns boot exposure; neither changes.
Their local submodules are uninitialized; this is a KMD CPU-metadata diagnostic,
not a new claim about firmware state. Saved EXP1932 used m1n1 0d3a75b3 and
Mu artifact SHA256 1c7d87d6fd4321feee605b5f9440dab6a350bc7a3964fb2c62b34602f565f3e5.

## Implementation

`render_gpu_fault.h:51` defines separate `Wom1GpuFaultMapping` v1 (256 bytes).
Existing fault/reset ABIs remain unchanged. `gpuva_g3_windows.c:13` records the
first valid read, attempts the existing state mutex without waiting, validates
active fence/context membership/VM slot, and copies identity, generations,
lease, native leaf and the fault’s aligned four logical PTEs. Context tokens
and allocation handles are never dereferenced. Unknown outcomes are retained;
no allocation, broker call, new MMIO or admission decision is introduced.

`backend_platform_windows.c:2222` captures before resume; `:3722` publishes
through the existing deferred registry writer and flush. `:4932` resets the
snapshot with the runtime. `tools/decode_wom1_gpu_fault.py:389` distinguishes
unknown native state and unavailable logical PTEs from an observed absent
leaf. Native state means acknowledged **software graph**, not a hardware read.

`tests/test_g4_fault_mapping.py:175` compiles actual capture/recovery bodies
with actual graph helpers and runs them under ASan/UBSan. Covers first-event
retention, wrong fence/context/VM, unavailable/busy/uncertain state, missing
table/shadow, scattered/contiguous pages, native absence/presence and exact
wire decoding. Removing recovery capture makes the executable fail.

## Smallest checkpoint / recovery

Claude rebuilds the KMD in the normal package (native WDK compile required),
keeps the EXP1932 UMD behavior and full-owner/half-pool profile, sets
`Wom1GpuFaultInfoRead=1`, `Wom1PoolHalfSize=1`; T13 staging enabled. Run the same
ARM64 D3D12 10K windowed 1280x720 workload once, with synchronous UMD trace.
On failure collect first, do not launch 2K after the adapter/boot has changed.
Decode Wom1GpuFaultMapping together with Wom1GpuFault/GpuReset. Matching
fence/raw FAULT_INFO and `captured` status distinguish an omitted logical group,
an invalid mapping, and a native graph/hardware disagreement. Busy/unknown is
inconclusive. No fault is not benchmark-completion proof. Return through the
existing exact-package GPU-visible recovery; Codex performs no hardware action.

## What is still unknown

Faulting allocation/type and its residency history; shader-address correctness;
actual hardware PTE/TLB state; native WDK build; real persistence before a
bugcheck; Windows stop code/parameters; full benchmark image and completion.
No correct behavior fix follows uniquely from the saved PID4104 evidence.
The separate Explorer aperture defect remains open and must not be hidden by
disabling protected SystemDirect or weakening the physical-headroom guard.
