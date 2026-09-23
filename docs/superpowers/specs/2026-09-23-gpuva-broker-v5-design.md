# G2 broker v5 contract design — offline, no m1n1 implementation

Status: design plus host executable specification only. m1n1, Mu, ACPI, KMD
caps and hardware are unchanged. User permission is required to implement this
ABI in m1n1 or run it on Air.

## WINDOWS CONTRACT:

VidMm owns every process GPUVA reservation, page-table allocation and lifetime,
residency decision, mapping update and paging fence. KMD receives a generation
tagged process/root and `BuildPagingBuffer` operations. `SetRootPageTable`
records VidMm's current root; it is not necessarily a context-switch event.
`UpdatePageTable` can have `hAllocation=NULL` for implicit tables, a 4-KiB
logical index/count, noncontiguous physical pages across an allocation, and a
special paging-process `CPU_VIRTUAL`/null-DMA-buffer bootstrap requiring
immediate update. A paging fence may complete only after PTE publication and
required TLB invalidation. `SubmitCommandVirtual` must use the matching process
root and only ready, resident mappings. No private broker allocator may replace
VidMm's table ownership.

Primary Windows sources: pinned WDK 26100 `d3dkmddi.h` SHA256
`c13cecb0ce73e7bbdb6bec8586d05eea31932a8c532bec49b3dae4a03054770e`
(GPU-MMU caps, UpdatePageTable, SetRootPageTable, BuildPagingBuffer), Microsoft
[GpuMmu model](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpummu-model),
[UpdatePageTable](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_buildpagingbuffer_updatepagetable),
and [page-table descriptor](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_page_table_level_desc).

## AGX/ASAHI CONTRACT:

Asahi `pgtable.rs` has 16-KiB pages, three native levels and 39-bit lower VA.
Asahi `mmu.rs` has 64 UAT contexts, user slots 1..63, a process VM root,
refcounted slot bindings, release-store TTB publication, ASID invalidation and
sync on slot reuse. With G13 `map_kernel_to_user=false`, a user slot's TTBR1 is
zero. Context 0 retains firmware/kernel mappings. Current m1n1
`hv_agx_retained_root.c` validates retained TTBR1 identity/private prefix and
owns a context-0 oriented table inventory; `uat.py::bind_context` currently
copies its TTBR1 into a user context. That helper cannot be reused unchanged as
an isolation guarantee. Mu exposes reviewed resources/ACPI only; it does not
own process UAT tables.

Asahi source: `drivers/gpu/drm/asahi/mmu.rs` slot constants 93-98, VM binding
1470-1524, teardown 1360-1405; `pgtable.rs` geometry 39-63. m1n1 source:
`m1n1_windows/src/hv_agx_retained_root.c` 138-282 and
`proxyclient/m1n1/hw/uat.py` 245-270, 560-569.

## TRANSLATION:

Introduce versioned broker ABI v5 in a future authorized change. Requests
carry operation, ABI/epoch, process identity + generation, root backing token +
generation, table backing token/level/index, GPUVA range, VidMm allocation
identity + generation, expected mapping generation and authorization grant.
No guest-supplied PA becomes a host PA without translation, ownership/range
checks, alignment and local-segment proof. No PA or root value is returned to
UMD. Handle `hAllocation=NULL` through a separately registered VidMm implicit
table grant, never as unowned memory.

Create process registers its VidMm-owned root and a system paging process
distinct from apps. Root relocation increments generation and retires the old
root after all leases/jobs have drained. Lease an available slot 1..63, publish
its TTBR0 with release ordering and **TTBR1=0**, then invalidate that ASID and
sync before acknowledgement. Every TA/3D firmware work item retains its
process/root/slot generation through exact completion. Busy slots cannot be
reassigned; 63 hardware slots must not limit lifetime count of inactive
Windows processes.

For each PTE update: validate all entries and destination table ownership;
stage the prior bytes; apply the whole native-leaf group; synchronize writes;
perform required TLB invalidation; acknowledge and advance mapping generation
only after success. A prepublication failure restores prior bytes and grants,
with no successful paging fence. A postpublication failure cannot report a
clean rollback without proof of restored visibility and TLB state: mark the
process/slot tainted, quiesce jobs and use existing fatal recovery. Bootstrap
is bounded synchronous CPU initialization before any paging job depends on the
system root. Protect firmware context 0 and retained TTBR1 prefix by immutable
identity checks on every path, including cleanup and failure.

Ownership: VidMm controls allocation/table placement, residency and logical
fences; KMD converts supported WDDM operations and keeps generation state;
broker owns validated host writes, slot lease/TLB and failure containment;
firmware executes selected context and reports completion; UMD holds opaque VA
bindings only. Recovery is fail-closed at the violated owner, then exact
package rollback to the GPU-visible Code28 profile if hardware becomes stuck.

Host design tests: `python3 -m unittest tests.test_gpuva_broker_v5_contract -q`
executes an independent finite broker reference, not m1n1. Its cases check
context0 preservation, process isolation at identical VA, slot reuse blocked
until job retirement and TLB acknowledgement, stale lease rejection, owned
backing validation, failed update rollback without advancing mapping generation,
and root relocation invalidating the prior lease. These tests specify behavior
for a later m1n1 implementation; they do not prove firmware ordering or Windows
admission.

## WHAT IS STILL UNKNOWN:

1. Exact VidMm `PAGETABLELEVELDESC` index/coverage semantics for native 16-KiB
   leaves under 4-KiB logical entries; 16-KiB slab acceptance on 26100 is
   separately held at G1b.
2. Whether all required firmware TA/3D commands consistently carry the selected
   nonzero context and whether user TTBR1=0 preserves required firmware access
   under retained m1n1 state.
3. The actual host mapping of VidMm local segment/table backing through EL2,
   including protected ranges, cache visibility and guest-to-host translation.
4. Exact TLB and handoff acknowledgement ordering under m1n1, failure during
   publication, slot recycling, root relocation and reset.
5. System-memory/eviction paths that can deliver legal 4-KiB scatter or mixed
   protection inside a native leaf; the G1 generator does not prove their
   absence.

Smallest later hardware checkpoint, only after permission: one app process and
the paging process map the same VA to distinct owned backing, bind one slot at
a time, complete exact firmware work and TLB retirement while context0 TTBR1
identity remains byte-for-byte unchanged. Failure stops before UMD graphics
features and recovers through the known GPU-visible image.
