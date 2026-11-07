# T7d: aperture backing and the native-leaf boundary

## Decision

**Blocked for a production fix.** None of the three proposed options supplies
a documented, coherent replacement for scattered system backing in the current
implementation. Do not repeat T7c, change page-size inputs into outputs, or
publish a contiguous GPU alias of unrelated physical pages. This branch adds
an executable safety regression and this analysis; it does not fix GravityMark.

Base: `b57cd075`, branch `codex/t7d-aperture-native-leaf`. Work is offline in
the assigned worktree. No hardware, guest, builder, package, firmware or other
worktree operation was performed. The task's supplied EXP1915/1916 failure and
EXP1917 rejection are the current observations; no new live ADT/register/trace
measurement was taken. Only the requested EXP1027/EXP1266 matches and directly
referenced source notes were consulted, not the complete experiment ledger.

## Evidence and option comparison

Paths below are relative to this repository unless stated otherwise. The WDK
and Asahi paths are the provided local primary-source copies; no network fetch
or external implementation copying was performed.

### 1. Paging-time backing replacement / broker remap

- `drivers/apple-agx/render-admission/src/gpuva_g3_paging_windows.c:564`
  validates the logical input PTEs. At `:593` the candidate records the actual
  resolved IPA, access flags, allocation and offset for each 4 KiB page.
  At `:643` the publication loop requires all four entries to have the same
  flags and segment, a 16 KiB-aligned first IPA and adjacent physical pages.
  At `:660` an incomplete group is sent to the graph as IPA zero (invalid).
  At `:738` the acknowledged logical pages are retained for CPU paging.
  This is deliberate representability enforcement, not an omitted store.
- `drivers/apple-agx/shared/src/apple_agx_gpuva_g3_graph.c:470` takes one
  `guest_ipa` for a native leaf, checks its alignment at `:484`, and registers
  that backing via `grant.AuxIpa` at `:535`. It does not accept four independent
  4 KiB PFNs for one native leaf. The graph's acknowledged-unavailable branch
  is also fail-closed; changing the completeness check cannot add translation.
- `drivers/apple-agx/render-admission/src/memory_runtime_windows.c:988`
  (`AdmissionMemoryRuntimeMapAperture`) reads VidMm's MDL PFNs and records
  their addresses in the software aperture (`:1016`–`:1026`). Allocating a
  new contiguous buffer there does not replace VidMm's MDL or CPU mappings.
  `gpuva_g3_paging_windows.c:896` resolves aperture pages for CPU paging;
  virtual transfer/fill resolves logical pages at `:991` and snapshots the
  aperture at `:1020`. Logical and GPU backing must continue to name the
  same bytes, including after later CPU writes and GPU writes.
- `drivers/apple-agx/shared/include/apple_agx_pool_window_abi.h:35` defines
  the pool-window page as 0x4000. MAP_APPEND and MAP_AT (`:37`–`:51`) map a
  source IPA range; they are not a 4 KiB scatter/gather gather operation.
  `drivers/apple-agx/render-admission/src/pool_windows.c:261` uses contiguous
  chunks and validates PFNs before presenting those ranges to the broker.
  The existing alias window works because each native page already has
  suitable backing, even if different native pages are scattered.
- `.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi/pgtable.rs:39`
  defines UAT_PGBIT=14. `mmu.rs:1090` obtains the GEM scatter/gather table;
  `.local/reference/mesa/src/asahi/lib/agx_bo.c:355` rounds BO size/alignment
  to AIL_PAGESIZE before calling the OS allocator at `:379`. A native leaf
  cannot concatenate unrelated physical 4 KiB pages merely because its VA
  or allocation size is aligned.

**Verdict:** a one-time bounce copy is incorrect for writable SystemDirect.
For example, after copying A to B at UpdatePageTable, a CPU write through the
existing LockCb mapping changes A while the GPU reads B. A GPU write changes B
while CPU readback/paging still reads A. Map/unmap aperture calls are mapping
lifecycle events, not a notification for every CPU or GPU access. A complete
solution would need a new ownership/synchronization contract, dirty-range and
alias handling, copy-in/copy-out around all relevant accesses, fence ordering,
and retention through eviction/cancellation/reset. None of that exists in this
leaf publication path. A broker-only alias of the same pages cannot reduce the
GPU's physical page granularity.

The worktree's m1n1 and Mu submodules are uninitialized. The pinned m1n1 object
`21c3648d32423def46086e258e0995c737dc9284` is absent in the local shared submodule
object database too. No substitute revision was used. Consequently current
m1n1 implementation, generated ACPI and live launch contracts were not
revalidated. The conclusion about the **current exposed interface** above is
grounded in the checked-in ABI and its KMD callers, not a claim that a future
hypervisor or hardware mechanism has been implemented or tested.

### 2. Make segment 3 CPU-visible

This is already implemented:

- `drivers/apple-agx/shared/src/apple_agx_physical_topology.c:81`–`:83` sets
  pool Use64KPages, CpuVisible and PopulatedFromSystemMemory to true.
- `drivers/apple-agx/render-admission/src/memory_windows.c:30` forwards
  CpuVisible into the descriptor. QuerySegment4 sets the pool's translated
  address at `:195`; QuerySegment5 does the same at `:121`.
- `.local/reference/wdk26100/d3dkmddi.h:2732` documents
  CpuTranslatedAddress as the CPU physical base for a CPU-visible segment
  without SupportsCpuHostAperture. This is the existing pool-window shape,
  not a missing flag. CPU visibility does not impose native-page contiguity
  on a *different*, aperture-resident allocation.
- `drivers/apple-agx/render-admission/src/allocation_windows.c:531`–`:538`
  already prefers segment 3, while retaining the aperture for CPU-visible
  classes. It reserves backing at `:525`, preserves CpuVisible at `:544`,
  and sets EvictionSegmentSet=0 at `:541`.
- `investigation/EXPERIMENTS.md:53196`–`:53197`: EXP1027 tried local-only
  CPU-visible class allocations. The result was 378 AllocateCb failures
  with 0x80070057 and DWM fallback, not a successful direct-memory path.
- `investigation/EXPERIMENTS.md:55153`–`:55163`: EXP1266 specifically tried
  segment **3** alone. Every CPU-visible class allocation was refused;
  class 1 / 64 KiB also failed and GL fell back to GDI Generic.
- The EXP1266 match at `investigation/EXPERIMENTS.md:55175` records the
  subsequent VidMm diagnostic: CPU-visible allocations must include an
  aperture in the supported set under the observed WDDM >= 3.0 conditions.
  Treat that as recorded build-specific evidence, not a universal public WDK
  guarantee. T7c/EXP1917 repeats the actual refusal at the current boundary.

**Verdict:** no new change to test here. Reasserting CpuVisible is a no-op;
removing the aperture repeats three rejected admission experiments. Reserving
pool bytes does not by itself constrain VidMm's choice of resident segment.

### 3. UMD alignment / page-size request

- `.local/reference/wdk26100/d3dukmdt.h:419`–`:466` defines
  D3DDDI_ALLOCATIONINFO2. Its fields cover the allocation handle, optional
  preallocated system memory/section, private data, VidPn source, flags,
  returned GPU VA and priority. There is **no Alignment or PageSize field**.
  Reserved bits are not an extension mechanism.
- `.local/reference/wdk26100/d3dkmddi.h:3917`–`:3922` places Alignment and
  MinimumPageSize/RecommendedPageSize in a union in the **KMD** allocation
  info. The latter are explicitly inputs. Writing them as KMD output hints
  is not a documented UMD request for physical system-page adjacency.
- `drivers/apple-agx/render-admission/src/allocation_windows.c:469` already
  supplies 64 KiB alignment. `.local/reference/wdk26100/d3dkmddi.h:2129`
  defines SysMem64KBPageSupported; `drivers/apple-agx/shared/include/apple_agx_gpuva_g3_caps.h:89`
  already enables it, and `render-admission/src/lifecycle.c:808` reports it.
- `investigation/analysis/M2-vidmm-64k-decision.md:92` records the directly
  relevant contrary observation: even after opting in to 64 KiB system
  pages, EXP1240–1244 contained scattered 4 KiB backing. The current leaf
  updater supports 64 KiB PTEs by expanding them to sixteen logical pages
  (`gpuva_g3_paging_windows.c:520`), but support is not a guarantee that
  VidMm supplies this format for every mapping.

**Verdict:** no documented per-allocation request in this UMD structure.
A different preallocated-memory ownership model or global WDDM 3.2 page-size
contract requires its own design/admission evidence; neither follows from
changing an alignment field. This analysis does not rule out all possible
future Windows allocation architectures.

## Ownership and retained contract

VidMm owns the system backing, residency, CPU lock mappings and paging schedule.
The KMD owns the pool's backing, page-table translation and publication checks.
The UMD keeps SystemDirect pointing at the canonical allocation and waits for
residency before locking (`umd_gpuva_windows.c:1268`–`:1304`, under the
render-admission UMD source directory). m1n1 owns the broker and stage-2 alias
window; Mu owns the reserved-memory/ACPI handoff. No initialization, runtime,
interrupt, DMA ownership, power or recovery responsibilities change here.
The protected SystemDirect, trim, headroom and capability lines are untouched.

## Executable regression and verification plan

`tests/test_g4_aperture_native_leaf.py` compiles the actual
AdmissionG3UpdateLeaf body with the actual shared address-translation source.
`tests/g4_aperture_native_leaf.c` supplies only the OS/memory-view/graph boundary
fixture. It exercises a contiguous writable group, a scattered replacement,
one-PTE repair, mixed permissions, partial unmap and full unmap. Assertions
check the publication address, logical PFNs/provenance and mapping references.
The graph/broker boundary always acknowledges operations in this fixture; this
is not coverage of broker refusal, TLB completion, VidMm or GPU execution.

Sensitivity check: removing the physical-adjacency condition from a temporary
extracted function makes the scattered-replacement assertion fail (exit -6).
The unmodified function passes under ASan/UBSan. This is mutation RED followed
by invariant GREEN, **not** RED-to-GREEN implementation of native scattered
backing. The requested production fix remains incomplete.

Run the new replay, allocation placement, direct-buffer placement, virtual
submit, pool granule and paging resubmission tests, followed by
`cd tests && python3 run_against_baseline.py`. Exact results and commands are
recorded in the task REPORT.md. No known-failure list or experiment/change
ledger is changed; the integration owner owns those ledgers per the handoff.

Actual result: eight targeted tests passed. The full baseline reported:

```text
ran 393 tests in 170 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

The separate real-broker 64 KiB paging test and pool/broker-capacity test could
not run because `hv_agx_retained_platform.c` and `hv_agx_gpuva_v5.h` are absent
with the uninitialized m1n1 submodule. The existing real-broker builder test
explicitly skips in that environment. These limitations are not new baseline
entries or passing coverage. Independent read-only review found no blocking
issue and reran the new ASan/UBSan replay successfully.

## WHAT IS STILL UNKNOWN / smallest next checkpoint

The missing contract is how CPU and GPU retain one authoritative backing when
VidMm chooses scattered system pages. A future implementation needs either a
documented backing-ownership mechanism with native contiguous pages, or a
complete coherent shadow path. Selecting and proving that architecture is the
next causal target; another placement-flag experiment does not distinguish it.

**No hardware run is justified by this branch:** it changes no driver behavior.
After a real ownership/bridge implementation exists, the smallest checkpoint
is one writable CPU-visible General BO forced through system residency:
CPU write -> GPU read/write -> CPU readback, then trim/evict/remap and repeat,
including a partial page and two aliases. Capture logical and native backing
provenance and verify byte equality and fences before a short GravityMark
D3D12 run past the prior 22–25 second boundary. Keep SystemDirect and the
hardware-validated UMD; use the existing exact-package GPU-visible rollback.
Claude owns that run and any necessary KMD reboot. No new artifact is supplied
for installation or launch by this branch.
