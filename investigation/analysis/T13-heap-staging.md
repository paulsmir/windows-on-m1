# T13 — native canonical D3D12 heaps with write-watched CPU staging

Offline design and implementation plan; no hardware, guest or package activity.

## WINDOWS CONTRACT

Pinned SDK `winsdk26100/um/d3d12umddi.h:294-328` supplies CPUPageProperty
NOT_AVAILABLE, WRITE_COMBINE, WRITE_BACK and heap byte size, not the API heap
type. UPLOAD/READBACK/CUSTOM CPU access therefore follows the page property.
The existing `CreateHeap`/`MapHeap` path preserves this distinction and returns
one persistent pointer. Placed buffers use offsets in the same heap; there is
no independent placed-buffer CPU allocation.

Microsoft contracts (offline references; online revisions not revalidated):
- https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12resource-map
  permits persistent maps; CPU stores must finish before consuming GPU work,
  and CPU readback must wait for the GPU. Map itself does not wait for GPU work.
- https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_heap_type
  UPLOAD is GENERIC_READ, READBACK is COPY_DEST, CUSTOM describes CPU access
  through its page property. Readback is not an upload-source heap.
- https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-getwritewatch
  returns addresses of written pages, with an in/out address count and output
  granularity. WRITE_WATCH_FLAG_RESET retrieves and resets atomically.
- https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-resetwritewatch
  warns about stores between a separate query and reset.
Pinned `memoryapi.h:367-383` confirms those signatures and zero-success return.
MEM_WRITE_WATCH requires reserved process virtual memory; page addresses are
not GPU addresses. Never use write-watch granularity as a native UAT page size.

## AGX/ASAHI CONTRACT

Asahi Linux `drivers/gpu/drm/asahi/pgtable.rs:39` fixes native pages at 16 KiB.
The supplied EXP1923/T12 observation is an omitted native leaf in a 128 MiB
CPU-visible upload allocation. Windows system backing can be scattered 4 KiB.
T12 rejects any process with an omitted leaf, including an unrelated temporary
copy buffer. Mesa alignment cannot constrain those PFNs.

`agx_win32_asahi_bo.c:355-363` gives internal General BOs CPU access as well;
Shader/Encoder classes are CPU-written too. They currently use the same
SystemDirect allocator, with aperture fallback. The shared construction arena
is not the placement of every shader/query/tile/scratch BO in this GPUVA owner.
They must participate in the staging solution, rather than become a new T12
refusal after application heaps are repaired.

## TRANSLATION

Ruling: reuse the existing CPU-only private-staging / checked copy-escape
transport, rather than allocate ordinary CPU-visible GPU copy resources.
Those resources recreate the aperture problem. `umd_win32_screen.c` already
creates a LOCAL/CpuVisible=0 canonical allocation plus VirtualAlloc staging;
`allocation_windows.c:517-540` places that canonical allocation in segment 3
alone. This is the same native placement as T7h, without claiming that the
CPU-facing BO itself is UNMAPPABLE (Mesa still needs its staging pointer).
No KMD flag or ABI is added. The canonical allocation has no CPU mapping.

One default-on D3D12-owner switch, APPLE_AGX_D3D12_HEAP_STAGING (0 opts out),
selects private staging for unshared CPU-accessible class BOs. This includes
UPLOAD/READBACK/CUSTOM heaps of every size and the owner's required internal
CPU BOs. NOT_AVAILABLE/GPU-only resources, imported presentation/shared BOs,
and other owners retain their existing paths. The C75c SystemDirect expression
remains exact, with the D3D12 selection applied separately.

Uploads run in the existing referenced-BO submission path before SubmitCommandCb,
which covers direct bindings, descriptor heaps, CopyBufferRegion,
CopyTextureRegion and indirect arguments without a second DDI reference walker.
Unreferenced heaps are not copied. Private staging persists through UnmapHeap.
Write-watch ranges replace whole-allocation hash scans in this path; unchanged
staging performs no transfer. Initialization/replaced canonical backing requires
one full upload. Failures must never turn an incomplete transfer into clean
state. Existing copied-generation/residency/hold checks remain authoritative.

The existing written-BO list, finish_submission and complete_render own
readback: wait for internal GPU completion, download CPU-visible written BOs,
then publish the completion fence. Outgoing queue waits therefore cannot expose
a pre-download completion. The write list is allocation-granular, so copy-out
is conservatively the whole written BO, not a fabricated exact shader range.
Copy-out preserves CPU changes to disjoint data against a canonical byte
baseline. Only a byte changed by the GPU is examined, and it is stored only if
the CPU still holds the baseline value. This avoids touching GPU-unchanged
bytes, including another placed resource or disjoint bytes in the same CPU
page. A same-byte CPU/GPU data race remains an application synchronization
violation. Driver stores may dirty write-watch pages: the next upload compares
only reported pages against the baseline to suppress this feedback. Never
reset the watch after copying. GPU-only/unmapped buffers do not download until
an actual CPU consumer (the existing deferred-map path).

Ownership: UMD owns staging/coherence and API pointers; Mesa owns reference and
write sets; VidMm owns residency; KMD owns placement/native publication and the
checked host copy; m1n1 owns broker/stage-2; Mu owns firmware reserve/ACPI.
No initialization, interrupt, DMA, power or recovery behavior changes. Local
m1n1/Mu submodules are unavailable; their current state is not inferred.

## Implementation plan (inline under TASK.md authorization)

- [x] Add actual-source host regressions for selected allocation policy,
  dirty-page copy-in, clean no-op, persistent mapping, failure retention,
  disjoint CPU writes during readback and fence ordering. Observe RED.
- [x] Select the existing canonical/staging path only on the D3D12 owner;
  implement bounded write-watch range copies and conservative readback merging.
- [x] Run targeted modules, protected-invariant checks and the complete baseline
  until BASELINE_OK. Review the final change; commit small independent units.
- Write the task REPORT with exact results, commits, receipts and limits after
  the final documentation commit (handoff artifact, outside this Git tree).

## Cost bound

For H bytes of CPU-accessible heaps, persistent process staging costs H bytes
besides canonical storage. Upload bytes per submission are the union of dirty
CPU pages (clipped to H); clean pages require no hashing. Initialization or a
replacement copies H once. Three 128 MiB heaps bound upload at 384 MiB per
submission, with a 64 KiB escape payload ceiling; sparse changes cost their
rounded dirty pages instead. Readback is bounded by the sum of written,
CPU-visible BO sizes per submission, including internal BOs. A BO that has ever been GPU-written also retains an H-byte canonical baseline
until destruction. Thus additional persistent memory is H + W, where W is the
sum of staged BO sizes that have needed readback (0 <= W <= H); worst-case
canonical + staging + baseline is 3H. Three read-only 128 MiB UPLOAD heaps need
384 MiB staging and no baseline. Three such GPU-written heaps would need
768 MiB extra process storage. READBACK copies scan the whole written BO and
merge differing bytes; unchanged GPU bytes are not read/stored in the CPU map.

A dirty upload allocates an address array of ceil(H / system-page-size) pointers
and a 65536-byte-payload request. At 4 KiB pages on 64-bit Windows the array is
256 KiB for one 128 MiB heap, allocated one BO at a time. A clean persistent
heap uses a one-address GetWriteWatch probe without an allocation, copy escape,
content scan or fence wait. Dirty addresses are sorted O(P log P), adjacent
pages coalesced into <=64 KiB transfers; a fully dirty 384 MiB set needs 6144
UPLOAD calls. The worst sparse pattern has one call per dirty page (up to
98304 pages across 384 MiB, with the full-dirty contiguous case coalescing).
Copy QUERY currently validates the whole canonical allocation before a dirty
transfer; KMD PTE-check work is therefore O(H/4096) even for sparse uploads.
The receipt measures transfer bytes, not this query cost. No claim that the
new path matches C75c's zero-copy speed is made. These are software bounds,
not measured throughput; checked CPU copy escapes are not GPU blits.

## WHAT IS STILL UNKNOWN

Native Windows compilation, VirtualAlloc/GetWriteWatch behavior on Windows
ARM64 and x64, GPU/cache coherence, VidMm pressure/eviction, actual dirty bytes,
and performance require integration validation. Shared cross-process coherent
heaps retain their separate contract; this does not claim to repair them.

## Smallest hardware checkpoint — integration owner only

Preserve/hash the known-good recovery artifact. Run GravityMark D3D12 2k/10k
short profiles with Wom1GpuFaultInfoRead=1. Expect no T12 refusal, no unmapped
fault, progress past execute #47 and no GPU-accessible private heap allocation
in OMITTED_LEAVES. Watch measure-d3d12-heap-staging dirty/upload/download bytes,
plus existing BO identity, omission and fault receipts. First exercise persistent
UPLOAD -> copy -> READBACK and CUSTOM disjoint CPU writes. Collect evidence
before exact-package ordinary GPU-visible recovery; hidden recovery is emergency
only. No hardware is run by this task.


## Final source evidence and failure rules

Paths are relative to `drivers/apple-agx/` except explicit reference paths.
Line numbers refer to this candidate and will be indexed in REPORT.md.

- `mesa/winsys/agx_d3d12_windows.cpp` initializes the per-owner switch before
  screen creation (including internal BO allocation), and brackets Execute
  metrics. `agx_win32_asahi_bo.c:355-363` supplies actual class/access intent.
- `render-admission/umd/src/umd_win32_screen.c` retains the protected direct
  expression; the separate owner-specific override selects LOCAL/CpuVisible=0
  plus process staging. CPU access flags must remain on this CPU-facing BO:
  `render-admission/src/gpuva_g3_windows.c:1421-1425` requires CpuWrite/CpuRead
  for checked copy escapes. Setting T7h's GpuOnly flag would forbid those
  copies. The canonical placement contract, not that incompatible flag, is
  reused; no direct canonical CPU map or aperture is involved.
- `render-admission/src/allocation_windows.c:517-540` sets segment 3 alone
  for LOCAL/CpuVisible=0 class BOs. `gpuva_g3_windows.c:1392-1495` checks
  allocation identity/access, active job and broker lease absence, paging
  quiescence, process generation and each copied page's owner/offset/native
  segment before host memory copy with barriers. Nonzero byte offsets and
  partial pages are explicitly implemented. No arbitrary physical pointer
  is accepted from the UMD.
- `render-admission/umd/src/umd_gpuva_windows.c` branches selected slots to
  `transfer_heap_slot` before legacy hash logic. This retains per-slot holds
  and waits for a prior GPU reader before upload. GetWriteWatch(RESET) precedes
  copying; partial failure invalidates Sync and a later attempt copies all
  bytes. Write-watch allocation failure has the existing unwatched conservative
  full-copy fallback; watch-query failure refuses rather than declaring clean.
  Downloads may not use the legacy failed-query replacement path: fresh backing
  cannot reconstruct GPU output. A failed copy-out never publishes completion.
- `d3d12/agx_d3d12_ddi.cpp:2403-2419` preserves placed-buffer base pointers
  and heap-relative GPU offsets. MapHeap/UnmapHeap retain the native persistent
  pointer. CopyBufferRegion and texture footprints replay with HeapBuffer;
  indirect expansion uses OwnerReadBuffer, which synchronizes/maps through the
  existing Mesa transfer path. No second reference walker is introduced.
- Upstream `.local/reference/mesa/src/gallium/drivers/asahi/agx_pipe.c:912-1055`
  owns transfer synchronization, resource validity and CPU tiling; the existing
  GPU-only projection and C70/C80 cached staging remain unchanged. All of those
  private CPU-facing BOs now select native canonical storage in this owner.

The current DDI still has pre-existing limits for mapped CUSTOM textures
(the buffer-heap path is the supported persistent CPU pointer path; arbitrary
mapped twiddled CUSTOM textures are not newly implemented). Shared/primary and
imported allocations retain their existing separate policy. These exceptions
can still produce T12 refusals under pressure; the candidate does not claim
universal admission for every legal D3D12 resource. No cap is broadened.

## Receipt

`measure-d3d12-heap-staging`, seven hexadecimal u32 words per Execute:
version=1, dirty-bytes low/high, uploaded-bytes low/high, downloaded-bytes
low/high. Counts sum referenced BO transfers across intermediate submissions;
BOs can be counted more than once in one Execute. Dirty bytes are the page-rounded
write-watch observations, including driver copy-out pages, while uploaded bytes
exclude equal-baseline pages. Initialization and unwatched fallback uploads can
exceed dirty bytes. Download bytes include whole written BO copies. The record
is byte accounting, not a success verdict; pair it with measure-d3d12-execute hr.
Writes from Map operations outside Execute are excluded by the entry reset.

## Verification record

The missing selected path and dirty-range test failed before implementation.
Host replays execute the actual policy and parser, upload/merge helpers,
completion publication and Map/Unmap bodies (the latter in test_umd_cpu_access).
Controls include unsorted pages, sparse/adjacent/tail ranges, first initialization,
no dirty pages, pointer stability, disjoint CPU writes, watch/allocation/copy
failures, partial copy followed by full retry, failed readback, held-reader waits,
GPU-only/shared/other-owner selection and cleanup after failed deallocation.

Independent review found the double-read copy-out race. A deterministic
interleaving accessor reproduced it; testing the immutable GPU delta first
fixes it. Review also requested the explicit W memory bound and partial-copy
failure case above; both are included. Exact test totals and BASELINE_OK are
recorded in the handoff REPORT, along with native-build/hardware limitations.


Final required baseline:

```text
ran 428 tests in 180 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

The focused suite passes 78 tests. The explicit environment check passes five
texture1D tests and skips the real-broker replay because `m1n1_windows` is
uninitialized; the previously reported texture1D environment failure does not
occur here. No known-failure list or hardware/change ledger was modified.
All four protected invariants pass. These results validate host behavior only.
