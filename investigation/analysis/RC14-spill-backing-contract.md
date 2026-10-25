# RC14: spill backing contract — implementation not delivered

Offline review at `7cf71b70e8716834d735163844091749f7c82d4b`, branch
`codex/rc14`, 2026-10-25. This is an incomplete implementation task: no
production fix, new regression, or RED-to-GREEN claim. The existing spill switch
still permits backing the GPU cannot address. Do not run a new package on the
strength of this note. README permits an explicit explanation when no correct
change has been established; that exception is used here, not a claim that a
complete shadow implementation is impossible.

Paths below: D = drivers/apple-agx/render-admission; W = .local/reference/wdk26100;
A = .local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi. All evidence was read
locally. No hardware, network, guest, builder, package, or other worktree access.

## Evidence and decision

The supplied EXP1952 log in the shared `.local/experiments/EXP1952-sysmem-spill/`
`evidence/EXP1952/gm1952a-umd.log:3121` records allocation 0x40003040,
VA 0x115eac0000, size 0x4010000, write=1, reason=5, failed VA 0x115eaf0000.
The first-fault follows at :3124; :3142 joins the same allocation to the CPU
access summary with no map activity. These are the actual line numbers in the
available file (the task cites :3134). The CPU-access line alone is a summary,
not a general proof of mediation of every possible CPU pointer.

Reason 5 means TailShort, not specifically scattered pages:
shared/include/apple_agx_g3_copy_query_receipt.h:22 and
shared/src/apple_agx_gpuva_g3_graph.c:681. The query loses ComponentReason at
D/src/gpuva_g3_windows.c:1439. A missing table, absent leaf, or unpublished/
non-writable leaf after a valid prefix can all produce TailShort. Thus the log
proves incomplete native coverage; scattered spill backing is the task's
supported hypothesis, not a PFN measurement recovered from this log.

The exact known architectural mismatch is independently present in source:
D/src/gpuva_g3_paging_windows.c:724 requires four contiguous, aligned,
equal-permission physical 4-KiB pages to publish a native leaf. :786 retains
logical mappings for omitted groups. RC13 permits GPU-only General allocations
to spill (:566 in D/src/allocation_windows.c), but supplies no alternative native
backing. The native guard properly refuses incomplete coverage before submission.
Do not weaken that guard or admit by the observed size, offset, or contents.

The next implementation target remains an ordered backing bridge for the
restricted GPU-only class. Repeating flag trials or a MapAperture-only bounce
patch would not implement it. No third historical comparison pass is needed.

## Option 1: mandatory allocation-level 64-KiB backing not established

Pinned WDK coordinates:

- W/d3dkmddi.h:3799–3840: all DXGK_ALLOCATIONINFOFLAGS_WDDM2_0 fields. No
  mandatory physical 64-KiB allocation request is declared. CpuVisible,
  PermanentSysMem, ExistingSysMem, AccessedPhysically and MapApertureCpuVisible
  describe different contracts. Clearing DisableLargePageMapping permits a
  mode; it does not promise the shape of all system PFNs.
- :3917–3922: Alignment shares storage with MinimumPageSize and
  RecommendedPageSize, explicitly marked input. Writing a page-size enum into
  Alignment is not an established output contract for this WDDM 3.0 path.
- :2113–2160, specifically :2129: SysMem64KBPageSupported. The production query
  already sets it at D/src/lifecycle.c:808 and supplies leaf-table storage at
  :817. The INF already opts into Supports64KBPages. No missing opt-in found.
- :2558–2603 and :2684–2701: descriptor3 uses DXGK_SEGMENTFLAGS;
  Use64KBPages is a segment page-mode flag. PopulatedFromSystemMemory denotes
  driver-populated memory; ReservedSysMem is reserved. These declarations do
  not give a KMD the right to replace VidMm's aperture MDL or demand that every
  eligible allocation avoid 4-KiB fallback. Production uses descriptor4/5 in
  D/src/memory_windows.c, so editing only descriptor3 would miss this path.
- :4951–4960: MapApertureSegment supplies pMdl, page offsets and count.
  :5041–5052: MapApertureSegment2 supplies a DXGK_ADL, AdlOffset and
  CpuVisibleAddress, not a replacement-MDL output. Neither declaration makes
  four consecutive MDL entries physically consecutive or 16-KiB aligned.
- :4727–4752: TransferVirtual's 64-KiB flags describe interpretation of transfer
  mappings, not allocation requests. Paging-queue ordering is not a PFN
  allocation policy. No type named DXGK_PAGINGQUEUE occurs in this header;
  do not invent a page-size flag on it.

CPU coherence for a genuinely contiguous direct mapping is the existing
single-backing contract: CPU and GPU see the same pages, with the existing
cache attributes, fences and barriers. Actual PFNs still need validation at
publication, even when VA and allocation size are 64-KiB aligned. Advertising
unsupported large-page modes cannot provide that validation.

## Option 2: shadow is feasible in principle, but needs the complete owner path

RC13 narrows eligibility to explicit GPU-only General, CpuVisible=0, with a
native-pool promise. That removes ordinary persistent CPU-visible uploads from
this experiment. It does NOT remove paging reads/writes or their ordering.
The earlier T7f CPU-visible lease design should not be copied wholesale into
this narrower path; prove which remaining CPU access paths require mediation.

Concrete integration blockers for a small bounce patch:

1. **No mandatory aperture callback.** D/src/allocation_windows.c:592 sets
   AccessedPhysically only for class 0. General GPUVA backing is decoded from
   segment-0 PTEs directly at gpuva_g3_paging_windows.c:639–676. The existing
   MapAperture entry only records supplied PFNs (memory_runtime_windows.c:988–1038).
   Attaching a shadow solely to that entry misses direct system PTEs.
2. **Build is not execute.** gpuva_g3_paging_windows.c:1099–1135 snapshots source
   and destination IPAs while building records; :1176 writes the records.
   paging_windows.c:340–357 executes them later and then completes the fence.
   A copy-in during UpdatePageTable can precede an earlier queued fill/transfer.
   Waiting for native jobs alone does not establish that pending CPU copies ran.
3. **Two backing identities need explicit representation.** LogicalPtes and
   ResidentPtes currently contain one IPA/provenance per page. The native query
   at gpuva_g3_windows.c:1443–1453 authenticates allocation/offset and requires
   native translation to equal that IPA. A native-only shadow fails this check.
   Replacing the logical IPA too changes what the CPU paging resolver reads
   (gpuva_g3_paging_windows.c:966–996), potentially losing the original copy source.
   Neither deleting the equality check nor overwriting PFNs is a bridge.
4. **Eviction needs authoritative bytes.** The virtual executor copies the IPAs
   stored in records at gpuva_g3_paging_windows.c:1250–1305; physical paging and
   copy escapes also exist. A GPU-written shadow must remain authoritative
   until ordered copy-out finishes. Invalid PTEs alone are not a discard-content
   authorization. Aliases must share one owner/generation; partial updates
   cannot overwrite a live shadow with stale original bytes.
5. **Storage must be acquired before a non-fallible paging boundary.**
   pool_windows.c:262–303 demonstrates checked contiguous-chunk allocation.
   Reusing its allocator is possible; borrowing an unoccupied advertised
   segment-3 address is not: VidMm owns those placements. A separate native
   allocation needs bounded physical capacity and broker authorization, or a
   reviewed disjoint window. Native-pool quantity reservations are not private
   shadow addresses. UpdatePageTable cannot negotiate OOM/retry:
   gpuva_g3_paging_windows.c:1380–1420 turns forbidden statuses into poisoning
   plus contract completion, not a recoverable placement retry.
6. **Teardown and cache behavior remain part of correctness.** Source pages,
   private pages and grants must survive pending records and all aliases until
   revoke/TLB acknowledgement and copy-out completion. Uncertain revoke must
   retain backing. System paging currently maps MmCached (:1220), whereas
   private pool mappings use their existing RAM attributes. A new duplicate
   mapping must not introduce inconsistent cache types; a memory barrier is
   not evidence that arbitrary alias attributes are coherent on J313.

Required implementation sequence (not implemented here): reserve bounded
private native backing at an explicitly fallible admission boundary; introduce
allocation/range/generation authority with separate original and GPU identities;
encode ordered activation/copy-in and retirement/copy-out records; route every
paging/copy/validation consumer through that authority; release only after the
last alias, pending copy and acknowledged GPU revoke. Add actual production
replays for queued initialization, partial map/unmap, 4K/64K changes, two aliases,
GPU writes followed by eviction/readback, allocation failure and uncertain revoke.
A test-only byte-copy model does not prove these integrations.

## Option 3: no 4-KiB gather within a native leaf

A/pgtable.rs:39–43 defines UAT_PGBIT=14 and UAT_PGSZ=1<<14;
A/Kconfig:18 requires PAGE_SIZE_16KB. A/mmu.rs:1200–1229 maps DMA SG ranges
with native alignment. The primary file is pgtable.rs in this checkout, not
uat.rs. A native leaf encodes one base plus the low 14 address bits, so four
scattered physical 4-KiB pages cannot be represented by one such entry.
A CPU virtual alias does not change their physical adjacency. A stage-2/DART
redesign would be a separate contract requiring current implementation and
explicit G2 authorization, not a KMD flag fix. No external code was copied.

## Ownership and validation limits

VidMm owns placement and system backing. KMD owns paging execution, native
publication, private backing lifetime and cache mapping. UMD owns submission
holds and ordinary CPU maps. m1n1 owns broker/stage-2 authorization, Mu owns boot
reserves/ACPI. IRQ, power and recovery owners are unchanged. This worktree's
m1n1 and Mu gitlinks are uninitialized (21c3648d and 361a2134); neither live
state nor current implementation/ACPI was inspected. The task forbids hardware
and other-worktree access. This note cannot certify a new launch contract.

No new deterministic invariant test is needed merely to restate the existing
aperture, native-allocation guard, caps and spill-placement tests. Existing test
results, exact baseline output and remaining unknowns are in the handoff REPORT.
There is no RED/GREEN production-fix result to report.

## Conditional integration checkpoint

No hardware run is justified by this documentation-only branch. The requested
future candidate is base 7cf71b70 plus a completed backing fix (none supplied).
After native WDK compilation and offline authority/lifetime replays, first use
one GPU-only allocation forced onto measured scattered PFNs: initialize, GPU
read/write, transfer/evict, read back, remap, and verify bytes through aliases.
Capture original/native PFNs, copy fences, held backing and revoke completion.
Only after that passes run GravityMark D3D12 2k, then 10k, with
Wom1GpuFaultInfoRead=1, Wom1PoolHalfSize=1, Wom1SysmemSpill=1 and DxgKrnl ETW.
Preserve launch/signer/caps settings, record package/manifest hashes and known-good
recovery, and collect evidence before exact-package GPU-visible cleanup.

WHY THIS HYPOTHESIS: EXP1952 proves incomplete native coverage after spill was
allowed; the current native publisher cannot represent scattered groups; an
ordered authority bridge addresses that mismatch while flag changes do not.
Require actual native coverage, correct contents, rendered frames/completion and
DWM survival. MakeResident success alone is insufficient. No experiment was run
or preregistered by this offline task.
