# EXP1933: first fault is not the workload's later fault

Offline source/evidence analysis, 2026-10-24; no hardware action.

## Observed boundary

Saved evidence root: `.local/experiments/EXP1933-fault-map` (shared local
artifact store, not this worktree's `.local`). `evidence/EXP1933/` is R.

- `R/kmd-receipts-probe-end.txt:189`: two valid unmapped-read faults, first
  Explorer PID2420/fence0xe92e/VA0x1101a917c0, then
  PID9624/fence0x1430a/VA0x1112680000. VM1/UL1C(1) for both.
- `:190`: first-fault mapping captured under the active graph lease:
  NativePresent0, valid/write segment0 PTEs for allocation0xffffd18db4c12410,
  offsets0/0x1000/0x2000/0x3000, IPAs0x898c8e000/0x8f8250000/
  0x8f8251000/0x8f8252000. These cannot form a native 16-KiB leaf.
  This confirms missing native backing for the first fault only. It does
  not identify the allocation class, byte extent or CPU-map state.
- `:192`: later fence0x14688 never completes; two engine resets and one
  adapter reset fail quiescence with0xc0000483. Minidump
  `evidence/dump/102426-13953-01.dmp` has ARM64 DUMP_HEADER64 bugcheck0x116,
  P1=0xffffd18db410a010, P2=0xfffff800ac3aa150,
  P3=0xffffffffc0000483, P4=3 (decoded by tools/windows_dump_header.py).
- The first app trace/output is absent. PID9624 is the workload candidate,
  not independently identified from an app log. The second attempted app
  reports no D3D12 devices after reboot. No benchmark completion is proven.

## Why no driver behavior fix follows yet

The first-fault-only guard (`gpuva_g3_windows.c:21`) deliberately leaves the
second mapping unknown. Equating the two faults because their unit/VM is
identical would confuse process address spaces. D3D12 already selects private
CPU staging and CPU-invisible native canonical storage
(`umd_win32_screen.c:743-756`; owner `agx_d3d12_windows.cpp:401`). DWM's
separate retained G4 refusal is a CPU-visible Encoder allocation, but is not
this first-fault allocation. It cannot establish the second fault's owner.

The correct aperture solution needs coherent ownership, not a guessed IPA:
`gpuva_g3_paging_windows.c:684-706` can publish only aligned contiguous native
groups. Windows supplies logical PTEs and allocation offsets (pinned official
WDK26100 `d3dkmddi.h:4693`). Asahi `pgtable.rs:39` fixes UAT pages at16KiB.
The staged KMD shadow design (`T7f-aperture-shadow.md:405`) still lacks CPU
access mediation, ordered copies, storage accounting and recoverable late OOM.
Returning a guessed BuildPagingBuffer failure or mapping unrelated PFNs is
incorrect. Blanket disabling SystemDirect would violate the explicitly
protected C75c contract. Increasing the pool merely changes pressure and
reintroduces the earlier host-memory tradeoff; it does not repair mapping.

This is the README's explicit exception for a consecutive diagnostic candidate:
**no correct, evidence-backed behavior change for the later fault can currently
be selected**. It is not a claim that a correct driver solution cannot exist.
No new broad OOM census or receipt storm is proposed.

## Delivered correction and contract

Capture once into a fresh mapping record on every valid fault, before firmware
resume. Preserve `Wom1GpuFaultMapping` as the first valid mapping and add
`Wom1GpuFaultMappingLast` as the latest valid mapping. Both retain the v1,
256-byte ABI. A later busy/identity/uncertain result replaces latest with
explicit unknown; an invalid/unread FAULT_INFO preserves both. Decoder accepts
both exact names in raw binaries, JSON and registry exports. The latest valid
mapping may predate Wom1GpuFault.Last if the last event was invalid; compare
raw FAULT_INFO, fence and context before joining. This is not a full history.

Asahi `gpu.rs:1422-1460` reads fault info before recovery. Existing KMD graph
lease, active fence/context and VM checks remain the snapshot authority.
The worker/reset serialization covers capture and deferred registry publishing.
No new MMIO, allocation, raw-token dereference, broker operation or GPU decision
is added. Unknown CPU metadata is not evidence of absent hardware PTEs.

Windows retains residency ownership; KMD owns logical/native graph metadata;
m1n1 owns publication/TLB; Mu owns boot and ACPI. Initialization, runtime,
interrupts, DMA, power and recovery ownership are unchanged. Firmware gitlinks
are uninitialized locally (m1n1 21c3648d, Mu361a2134); installed experiment
manifest identifies m1n1 0d3a75b3. No firmware implementation change is proposed.

## Checkpoint and remaining unknowns

Claude-only KMD rebuild with the same G3/full-owner profile, unchanged UMD,
firmware/caps/signer, Wom1GpuFaultInfoRead=1 and Wom1PoolHalfSize=1; D3D12
HEAP_STAGING unset or1. One ARM64 GravityMark D3D12 10K run,1280x720, timeout90.
Collect both mapping values and all fault/reset receipts before exact cleanup.
Stop after loss of SSH/reboot; do not launch 2K on a failed adapter. Preserve
original app logs from durable storage even if initial scp fails.

Latest mapping must match the second valid event and be captured. Absent leaf
with scattered logical pages narrows to that allocation's backing policy;
invalid logical pages to lifetime/addressing; present native graph to native
publication/TLB/root or address interpretation. None alone proves hardware
coherence. Unknown/missing/mismatched latest capture fails the discriminator;
do not repeat this diagnostic unchanged. A GPU fault/reset is a workload failure
regardless of measurement success. Recovery stays the existing dump-first,
exact-package GPU-visible contract under Claude's ownership.

Still unknown: second-fault mapping/allocation/CPU ownership, actual shader
address correctness, why later firmware work cannot quiesce, full stack/module
attribution from the dump, native WDK compilation and hardware correctness.
No GravityMark behavior fix or workload success is claimed.
