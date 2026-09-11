# CPU-visible allocation residency implementation plan

> **For agentic workers:** Execute this bounded plan inline. Do not delegate,
> create EXP503, or change any platform, AGX, scheduler, capability, display, or
> UMD behavior.

**Goal:** Make the existing WDDM 3.0 CPU-visible shadow allocation descriptor
truthfully name every implemented residency location needed for locking while
leaving the non-CPU-visible primary contract unchanged.

**Architecture:** `AdmissionDdiCreateAllocation` remains the single descriptor
producer. Segment 2 remains the first preference because the allocation can be
physically accessed there and the paging path implements system-memory/local
transfers. CPU-visible allocations additionally support aperture segment 1,
whose existing map/unmap callbacks map VidMm-owned system pages; non-CPU-visible
allocations remain local-only. No memory owner or paging implementation changes.

**Tech stack:** C11 KMD callback, pinned WDK/SDK 10.0.26100.0, Python production-
callback harness, clang ASan/UBSan, ARM64 MSVC 14.44.35207 package build.

**Spec:** `.local/experiments/EXP502-cpu-visible-residency/agent-brief.md`

## Source-first contract

- Inspected `allocation_windows.c`, `memory_windows.c`, `render_memory.c`,
  `memory_runtime_windows.c`, `paging_windows.c`, the shared physical topology,
  pinned WDK 26100 `d3dkmddi.h`/`d3dukmdt.h`, and Microsoft documentation for
  allocation usage tracking, GPU segments, `DXGK_ALLOCATIONINFO`, allocation
  flags, and lockable shadow surfaces.
- Windows owns placement, system backing, lock virtual addresses, and paging
  requests. The KMD owns allocation/open lifetimes, the two segment descriptors,
  aperture map/unmap validation, physical local transfers, and fail-closed AGX
  submission translation. m1n1 retains root/physical translation; Mu publishes
  the unchanged device and memory resources.
- WDDM 2+ uses `SupportedWriteSegmentSet` as the unified supported placement set;
  every preference must name a supported segment. A CPU-visible allocation in a
  non-CPU-visible local segment must also support an aperture segment so VidMm
  can place it in system memory for locking. The current code violates only that
  final requirement by returning set 2 for all allocations.
- The existing aperture segment 1 is a 4 KiB linear aperture with implemented
  `MAP_APERTURE_SEGMENT`/`UNMAP_APERTURE_SEGMENT` handling. The existing local
  segment 2 has implemented physical system/local transfer, fill, discard, and
  local-view translation. Therefore CPU-visible `{1,2}` with preference 2 is
  coherent; aperture-only would unnecessarily forbid the implemented local
  source/destination path needed by shadow/primary bitblts.

## Atomic contract

For a valid CPU-visible private description, return preferred segment 2,
supported read/write sets `0x3`, `CpuVisible=1`, and `AccessedPhysically=1` as
one descriptor invariant. For a valid non-CPU-visible description, preserve
preferred segment 2, supported read/write sets `0x2`, `CpuVisible=0`, and
`AccessedPhysically=1`. Keep `MapApertureCpuVisible=0`; the current map callback
uses the MDL and does not require a CPU virtual address. Invalid private metadata
must fail before publishing an allocation handle.

## Implementation and verification

- [ ] Extend the production-callback test with literal complete descriptors for
  primary and CPU-visible shadow/staging, plus corrupt-metadata fail-closed.
- [ ] Run the focused test and retain the expected RED: CPU-visible supported set
  is `0x2`, not `0x3`.
- [ ] Add only the aperture and CPU-visible set constants and conditional set
  selection in `AdmissionDdiCreateAllocation`.
- [ ] Run the focused test GREEN, all relevant render tests, and diff checks.
- [ ] Commit only this plan, the production source, and its test; append one valid
  19-column `CHANGES.csv` row with the final 40-character commit.
- [ ] Freeze exact EXP501 source plus only those committed files, build/sign
  version 30.0.502.0 on the pinned builder, and verify package hashes.
- [ ] Preregister EXP502 with the exact source/platform/package identities and
  one variable: CPU-visible supported placement changes from set `0x2` to `0x3`.
- [ ] From a verified clean ordinary baseline, stage only the exact package,
  gracefully restart, and launch full-owner 477/406 once with durable redirected
  stdout/stderr. The smallest falsifiable checkpoint is disappearance of the
  exact aperture-set validator, followed only by the next natural Windows
  allocation/paging primitive.
- [ ] Collect ETL, Event494/stacks, driver/package/health/physical receipts and any
  dump; state only the boundary positively evidenced.
- [ ] Hash-check and remove only the bound package/service/devnode, rescan to one
  inert APPL0002, gracefully restart, and restore ordinary 377/392 with its own
  durable host log. Emergency 377/385 is reserved only for an unrecoverable
  installed-candidate boot.
- [ ] Update `EXPERIMENTS.md`, `CHANGES.csv`, `GPU_CURRENT_STATE.md`, and the
  EXP502 report with exact implementation/offline/hardware distinctions.
