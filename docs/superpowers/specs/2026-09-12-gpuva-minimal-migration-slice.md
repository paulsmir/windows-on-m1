# Minimal GpuMmu migration slice — gated design, not implementation

## Entry gate

The finite semantic model passed its positive and fail-closed scenarios, but
unrestricted4KiB mapping cannot be represented by one16KiB UAT leaf. No
production work is authorized by this document until the admissible VidMm input
domain/table-segment design excludes or exactly supports those counterexamples.
If that proof fails fundamentally, stop migration and use relocation option.

Do not use the model's fixed backing registry or explicit resident flag as
production memory-management implementations.

## Smallest coherent vertical slice, once the gate is closed

One existing physical adapter/node, system paging process and multiple isolated
user process objects. Validate at least P/Q with equal numeric VA. One16KiB
test allocation per process is a workload bound, not an advertised process or
allocation limit. No desktop/present/OpenGL expansion in this slice.

1. Versioned process/device/allocation/root generations and borrowed BO binding.
   Binding carries no PA and is separate from residency/paging dependencies.
2. VidMm-owned page-table allocation registration, including implicit tables;
   immediate CPU paging-process bootstrap and the chosen normal update mode.
3. Exact normalized PTE translation, owner/protection/range validation and
   transactional broker publication. No firmware private table mutation.
4. Root selection/slot leases tied to all in-flight work, actual invalidation
   acknowledgement before reuse, and safe root movement/destruction.
5. Map/Free GPUVA and independent MakeResident/Evict/synchronization in UMD.
   Preserve reservation across eviction and refresh mapping readiness on remap.
6. Minimal virtual-submit bridge into existing execution/completion mechanisms,
   carrying the exact process/root lease through completion. No new shader,
   firmware command generation, scanout or capability speculation.
7. Fault/timeout/removal and teardown must not release uncertain in-flight
   ownership. Use each DDI's actual status/IRQL contract, not generic fake success.

Required public wiring is an atomic compatibility group: process DDIs, root
selection, page-table descriptions and paging operations, virtual contexts,
SubmitCommandVirtual, UMD GPUVA/residency and matching truthful caps. Implementing
a helper or registering one stub does not authorize enabling that group.
The currently proven physical profile remains unchanged throughout preparation.

## Offline exit conditions before any separately authorized hardware

- Real shared translation agrees with pinned WDK entry normalization for every
  admitted mapping/protection/partial-update case, including system-memory paths.
- Injected allocation/publication failures do not yield a successful mapping
  fence, lost ownership or a visible partial root.
- P/Q same-VA separation, root replacement, process destruction and allocation
  recreation are checked in the actual composition, not copied test-only logic.
- Driver uses authoritative residency grants and actual completion dependencies;
  neither is inferred from BO existence or a retained CPU mapping.
- Static/DDI analysis, build/package consistency and unchanged physical profile
  controls pass. Hardware remains a separate explicit authorization gate.

No part of this production slice is implemented by the offline model task.
