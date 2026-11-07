# RC27 system segment contract and implementation plan

Spec: `/Volumes/pwdev/public_windows/.local/handoff/codex/rc27/TASK.md`.
Base: da0d5dbd; offline work on codex/rc27 only.

## Decision

The production two-tier UMA path is not implemented by this change. Use the
task's explicit refusal fallback at the first incomplete requirement: a
KMD-contiguous, reclaimable segment with truthful SharedSystemMemory accounting.
An additional driver-populated memory segment is possible, but is not the same
contract as shared system memory. No segment, placement set or memory budget is
advertised speculatively. No claim that WDDM universally makes this impossible.

The existing hypervisor window can provide CPU visibility for a second memory
segment by partitioning its address range. Lack of a CPU aperture is therefore
NOT the blocker. The independent review specifically challenged that hypothesis.
The gate concerns accounting, fallible residency admission, and authoritative
backing across eviction, not whether the GPU broker accepts ordinary guest RAM.

## Sources and observed contract

Paths below are relative to this worktree; W means
`.local/reference/wdk26100/d3dkmddi.h`.

| Source | Contract / consequence |
| --- | --- |
| `investigation/EXPERIMENTS.md:63927`, `:63944` | Half-pool page-in exhaustion, then aperture spill with incomplete native coverage. |
| Same `:63980`, `:63995`, `:64019`, `:64038`, `:64050` | Small pool runs slowly; full pool OOMs; coalescing/descriptor savings do not resolve waits; persistent pool maps OOM at load. |
| `.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi/Kconfig:18`, `mmu.rs:1200`, `:1221`, `gem.rs:160`, `:195` | Linux native-page/DMA alignment and separate cached/WC CPU mappings. Windows 4 KiB backing is not equivalent. |
| `investigation/analysis/RC26-sysmem-broker.md:11` | Matching 0d3a75b3 source was already shown to accept eligible RAM outside the pool. This is supplied evidence, not a fresh inspection of another worktree. |
| `drivers/apple-agx/shared/include/apple_agx_pool_window_abi.h`, `render-admission/src/pool_windows.c:12`, `:330`, `:371` | Existing contiguous CPU window plus MAP_AT/UNMAP_RANGE can assemble disjoint partitions from contiguous chunks; no new hypervisor command is inherently required for a second memory segment. |
| W`:2684`, `:2720`, `:2733` | Descriptor3/4 describes a fixed address capacity; descriptor4 uses a CPU physical base or CPU host aperture. Production QuerySegment4/5, not descriptor3 alone, must be updated. |
| W`:2566`, `:2572`, `:2586` | Aperture, PopulatedFromSystemMemory and budget groups are separate flags. NonLocalBudgetGroup is not a request to replace VidMm's backing allocator. |
| `investigation/analysis/M2-f3-demand-pool.md:11` | Prior Microsoft documentation study: driver-populated segments count as DedicatedSystemMemory; MaxSharedSystemMemory subtracts that amount. This accounting rule is from the stored study, not a declaration in the WDK header. No fresh web query in this offline task. |
| W`:4951`, `:5041` | MapApertureSegment receives an MDL; v2 receives an ADL and CPU address. These fields do not establish a supported output for replacing CPU backing with private chunks. |
| W`:929`, `:935`, `:3856` | SetAllocationBackingStore receives a system-space pointer via const input; ShareBackingStoreWithKmd shares that pointer. Neither declaration lets the KMD return replacement backing. |
| W`:10008`, `:10044`, `:10107` | Physical-memory-object creation/ADL allocation can acquire memory, but the declarations contain no binding to a VidMm allocation/segment or residency lifetime. RequireContiguous is not a per-allocation VidMm page-size policy. |
| W`:5712`, `:5735` | CPU-host-aperture callbacks map memory-segment page numbers into a declared physical aperture. They do not transfer backing-store ownership or budget classification. |
| W`:3857`, `:4867` | NotifyEviction/NotifyAllocation is WDDM 3.2-only. It cannot be introduced into the pinned default WDDM 3.0 path by setting a reserved bit. |
| `drivers/apple-agx/render-admission/src/allocation_windows.c:536` | Creation-time physical promises ensure later native placement can be satisfied; reserve failure is returned as STATUS_NO_MEMORY here. |
| `drivers/apple-agx/render-admission/src/render_pool_slab.c:22`, `:29`, `:78` | Empty slabs remain held to cover native promises. Returning a private slot does not necessarily return a slab to Windows. |
| `drivers/apple-agx/render-admission/src/render_pool_demand.c:78` | Demand placement can fail to acquire backing. Removing the promise makes this failure reachable under pressure; another segment alone does not solve it. |
| `drivers/apple-agx/render-admission/src/gpuva_g3_paging_windows.c:1230`, `pool_windows.c:278` | Generic system paging maps cached; existing slabs are WC. Reusing private slab PFNs through that mapper would mix cache attributes. |
| `drivers/apple-agx/render-admission/src/lifecycle.c:515`, `:530` | G3 startup precedes pool/topology expansion but follows scanout startup. Refuse before AdmissionInterruptStart instead, avoiding late failure cleanup entirely. |

The local m1n1 and Mu gitlinks are uninitialized (21c3648d and 361a2134).
Live ADT/registers/interrupts, current generated ACPI and launch state are not
inspected: the user explicitly forbids hardware and other-worktree access.
Do not manufacture a pool-only broker limitation from this absence. RC26 and
EXP1959 supply the relevant current-platform observation; no firmware changes
or new assisted/standalone launch guarantees are made here.

## Mechanisms considered

1. **VidMm aperture/system memory:** truthful shared-memory route, but actual
   PFNs may scatter. Private replacement would separate persistent CPU and GPU
   bytes. MapApertureSegment alone also misses direct segment-0 GPUVA mappings.
2. **Second driver-populated memory segment:** partitioning the existing window
   can supply native-aligned backing and CPU visibility. It remains a private
   memory segment with dedicated-system accounting, and inherits the page-in
   promise problem. Clearing PopulatedFromSystemMemory would misdescribe RAM;
   setting NonLocalBudgetGroup only changes the residency budget group.
3. **Backing-store/physical-object APIs:** useful constituent APIs, but the
   inspected declarations do not provide the missing replacement/admission
   contract. A pointer notification or an ADL handle alone is insufficient.

The required remaining design is specific: establish a supported VidMm binding
for KMD-contiguous backing, a resource-exhaustion boundary before publication,
and honest memory accounting; then implement one content/lifetime owner used by
CPU Lock, physical/virtual paging, GPU aliases and teardown. A permanently
reserved second pool or permanent shadow duplicate would evade the RAM goal.
No prior production segment stage can be enabled before this first gate closes.

## Owners and preservation

VidMm owns allocation placement, residency and paging backing; KMD owns raw
MDLs, CPU mappings, copies and broker publication; UMD owns locks, holds and
fences. m1n1 owns stage-2 aliases, grants and TLB acknowledgement; Mu owns boot
RAM/reserves/ACPI. Initialization, IRQ, DMA, power and recovery ownership stay
with those layers. Asahi owns Linux GEM/DMA lifetimes, not Windows CPU mappings.
No external code is copied.

Preserve rc6/rc7 submission/CPU holds, rc11 clean-OOM versus uncertain-work
errors, rc18 original/native identity, and rc23/rc24 cache/retirement rules.
Eviction must copy authoritative bytes out, retire all GPU and queued CPU
references, acknowledge unmap/TLBI/revoke, and only then free backing. A live
persistent CPU map cannot be left pointing at freed or silently replaced pages.

## Implementation plan (native execution)

- [x] RED: execute the real StartDevice prefix through its first runtime-start
  boundary. Wom1SystemSegment=1 must return STATUS_NOT_SUPPORTED after successful
  arm consumption and before interrupt/memory/platform/scanout/G3/pool startup.
- [x] Add an exact-DWORD-one registry reader in lifecycle.c. Read only after
  context/output validation, PASSIVE check, firmware resource check and arm
  consumption, before starting any runtime owner.
  Missing/malformed/unreadable/zero values preserve default startup. Keep the
  switch independent of Wom1SysmemGpuMap; broker capability is not enablement.
- [x] GREEN: execute registry, startup/refusal/retry, existing allocation/open,
  private-lease, native-identity, real paging resolver/encoder/executor, UMD
  residency and KMD-header regressions. Verify default descriptor behavior.
- [x] Run `cd tests && python3 run_against_baseline.py` to BASELINE_OK without
  changing known_failures. Review, small implementation/documentation commits,
  and handoff REPORT.md with exact results. Integration owns both ledgers.

Review focus: registry reads at wrong IRQL; bypass through supported broker;
runtime ownership on refusal; accidental default placement/cache change; claiming
this fallback satisfies the full UMA task. No new paging retry status, private
allocator caller, segment advertisement, UMD mode or GPU access is added.

## Conditional hardware checkpoint (integration owner only)

Do not benchmark this refusal as a memory fix. After closing the above contract
and implementing the path, use base HEAD plus the completed fix, pinned WDK
build and recorded package/manifest hashes, with Wom1GpuFaultInfoRead=1,
Wom1SystemSegment=1, Wom1PoolHalfSize=1 and APPLE_AGX_D3D12_CPU_POOL_MAP=0.
First verify one persistently mapped upload/readback allocation: CPU/GPU exact
bytes, alias/partial update, page-out/re-page-in, actual RAM return, pressure
refusal and acknowledged teardown; inspect DXGI dedicated/shared sizes and both
budget groups. Only then GravityMark D3D12 2k, then 10k. Acceptance: no load OOM,
short paging waits, scene presents/min far above 8, correct frames and DWM alive.
Preserve the known-good GPU-visible recovery; integration collects evidence
before exact-package cleanup. No hardware experiment is run or preregistered.

## Review correction

The initial G3-local refusal passed its local replay but would run after
ScanoutStart. Review found that the pre-existing G3 failure unwind ignores
ScanoutStop failure (`lifecycle.c` before this change:481), while
`scanout_windows.c:648` can retain its owner on failed synchronization/release.
The final check is before AdmissionInterruptStart; it never enters that unwind.
The new regression first failed at runtime on the original StartDevice prefix
and then passed. It exercises the exact prefix with substitute firmware/arm
callbacks and an explicit stopping sentinel at the first runtime-start boundary,
not a full Windows startup/cleanup simulation. Original G3 code/tests were
restored unchanged. The broader pre-existing cleanup issue is not fixed here.

## Delivered result and verification

`lifecycle.c:24` implements the strict reader. `:258` rejects invalid IRQL
before G3 firmware/arm handling; `:285` refuses the request after the existing
arm gate, before the first runtime start at `:306`. StartDevice output counts
stay zero and the existing start-stage status records STATUS_NOT_SUPPORTED.
Missing/failed/malformed/non-one reads preserve the default path. This applies
to the G3 build; no new segment/capability is published on any path.

Implementation commit: 3f9dcc9b (full hash in handoff REPORT.md).
Final targeted command: 61 tests, OK. Additional lifecycle/display: 11 tests, OK
with pinned LLVM 22.1.8 clang/lli, --no-default-config and an explicit valid SDK
(the host's default clang configuration named a missing MacOSX27.sdk).
The earlier default attempt lacked lli; mixing Apple clang bitcode with Homebrew
lli then hit an unsupported stack-probing method. These were toolchain errors,
not new source/baseline exceptions; logs are preserved in the handoff.

Required command after the final source/test edit:

```text
cd tests && python3 run_against_baseline.py
ran 538 tests in 231 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

Baseline equivalence is not an all-green repository, native WDK build or hardware
result. The actual paging/allocation tests cover the preserved implementation;
there is no new system-segment resolver/encoder/executor to claim tested. All four
protected substrings and diff whitespace checks pass. No experiment/change ledger
or known-failure file was edited. All 39 historical OPEN review headings have
dispositions in the handoff, with the shared review SHA-256.
