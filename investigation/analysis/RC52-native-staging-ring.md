# RC52: native staging ownership design and tested allocator prerequisite

**The production staging-ring optimization is not implemented.** The committed
code is a portable ring ownership allocator and its executable tests. Nothing
routes runtime uploads, readbacks, queries or textures through it. There is no
KMD user mapping, changed package, new runtime receipt or performance improvement.
The missing integration is a material part of rc52, not a qualification detail.

Work is offline in `codex/rc52`, base `2a7acf14`. The task forbids hardware,
guest, package and other-worktree operations and assigns ledger updates to the
integration owner. Existing production behavior and pool admission policy remain
unchanged. No external implementation was copied.

## Observed contract and architecture decision

RC50's saved EXP1974 join demonstrates that persistent Lock2 plus UMD residency
is not an immutable native placement: the mapped 128 MiB allocation was later
republished in segment 0 with all native leaves omitted. RC51's analysis and
cached dxgmms2 validation explain why ordinary CpuVisible allocations cannot
simply remove aperture support. See `RC50-canonical-map-native-refusal.md` and
`RC51-placement-and-idle-reclaim.md`; this task did not repeat their historical
comparison or make a new machine-state claim.

Current source paths below are relative to `drivers/apple-agx/`:

| Source | Consequence |
| --- | --- |
| `render-admission/umd/src/umd_gpuva_windows.c:1291` | Each dirty private heap transfer queries canonical backing and then transfers bounded ranges. The escape cost represents real synchronization and data movement. |
| Same file `:1241`, `:1267`, `:1399` | Heap and ordinary staging copies use the bounded copy escape. Removing it without a new storage/copy path loses data. |
| `mesa/winsys/agx_win32_gpuva.c:262`, `:294`; UMD submit `:2049` | MakeResident/paging precede the transport callback, which then uploads held slots. Calling Gallium copy/flush at that point recursively enters submission with an active held set. |
| `mesa/winsys/agx_win32_asahi_scene.c:247` | The similarly named BufferCopy helper is a CPU memcpy; it is not the GPU-copy replacement. |
| `.local/reference/mesa/src/gallium/drivers/asahi/agx_blit.c:617` | Real buffer copies acquire a compute batch, declare source reads/destination writes, then emit vector or byte copy kernels with barriers. |
| `.local/reference/mesa/src/gallium/drivers/asahi/agx_pipe.c:980`, `:1031` | Texture staging already orders a GPU writer before CPU reads and copies CPU-written staging back to the texture. |
| `render-admission/src/gpuva_g3_windows.c:2428` | The existing KMD reservation is one 32 MiB leaf span for private render objects; it is not a free staging reservation. |
| `render-admission/src/gpuva_g3_paging_windows.c:216` | Root parking validates both roots before detaching and rolls back failed transactions. A second reserved span must participate in the whole transaction. |
| `shared/src/apple_agx_gpuva_g3_graph.c:775` | Current root-detach validation accepts only the exact existing private branch. It cannot silently ignore a new ring leaf. |
| `render-admission/src/gpuva_g3_windows.c:3831` | User command validation excludes the private arena. A staging range needs a distinct authenticated access contract, not blanket access to driver-private memory. |
| Same file `:3844` | CPU-parsed envelopes require ordinary logical-PTE provenance. Keep those allocations on the current path initially. |

A feasible implementation direction is a separate driver-owned native arena,
with an explicit storage kind for CPU-facing staging BOs. Existing Mesa GPU
copies then consume the staging bytes before their destination is used. This
also avoids a second copy for eligible internal encoder/query storage. It is
not sufficient to mark the slots SystemDirect or give them synthetic KMT
handles. Runtime handle translation, reference holds, readiness, destruction,
and CPU maps all need explicit ring branches.

The allocator delivered here describes transient copy slices, including a group
of slices committed to one accepted batch. It is **not** a general persistent-BO
allocator: persistent BO ownership across repeated submissions still requires
an appropriate lifetime owner and updated last-use tracking. Do not assume the
new header alone implements the ring-as-BO alternative.

## Native backing and Windows mapping/security contract

The current slab allocator has a useful but limited contract:

- `render-admission/src/render_pool_slab.c:22` retains the ordinary native
  promise plus private lease count. `:163` and `:193` acquire exclusive raw
  chunks, with bounded cap/rollback. Private acquisition is disabled until
  PrivateCap is configured; pool startup does not currently configure it.
- `render-admission/src/pool_windows.c:278` allocates original WC MDLs and
  verifies 64 KiB-aligned contiguous chunks. `render_pool_slab.h:66` explicitly
  does not promise whole-range contiguity or a single original MDL.
- Mapping a whole 2 MiB parent slab for one leased slot would expose pages of
  other owners. `winsdk26100/km/wdm.h:33617` supplies IoBuildPartialMdl for exact
  source intervals, but separate partial views do not form a flat 32 MiB CPU
  pointer. No inspected primary source establishes an arbitrary-PFN composite
  MDL assembled from unrelated slab MDLs as the supported solution.

The simpler flat-map direction is one dedicated, bounded original
MmAllocatePagesForMdlEx MDL with native 64 KiB chunks. That needs explicit pool
accounting and headroom admission without reserving the same physical bytes
again in ordinary slabs. A per-process limit alone is insufficient: adapter-wide
ring capacity must bound aggregate pinned memory. Do not change the protected
ordinary admission policy or steal its existing backing promise.

Pinned Windows header evidence:

- `.local/reference/winsdk26100/km/wdm.h:29624` requires at most APC_LEVEL for
  a UserMode MmMapLockedPagesSpecifyCache call and declares its SEH exception
  behavior; `:29633` gives the mapping interface and cache/protection inputs.
- Same header `:29696` declares MmUnmapLockedPages. The implementation must use
  the process that owns the view and never accept a kernel pointer from a user.
- `.local/reference/winsdk26100/km/ntddk.h:11227` declares process notifications;
  `:11443` declares PsGetProcessExitStatus. Those declarations alone do not prove
  address-space teardown ordering.
- `.local/reference/wdk26100/d3dkmddi.h:3804` separates CpuVisible/PermanentSysMem
  and other allocation flags; `:3849` provides additional flags. None is used
  here to bypass the observed placement requirement.

Before exposure, all newly leased bytes must be zero, every CPU alias must keep
WC cache attributes, and the mapping must contain only that ring's pages.
Validate exact referenced process identity, live KMD process/device/context,
opaque ring generation, buffer length/version/reserved fields and WOW64 user
address representability. The buffered reply may contain user VA, GPUVA and
size; never MDL, EPROCESS, slab or kernel addresses. A successful pointer is not
proof of native residency or permission.

**Unresolved security evidence:** the available cached primary sources do not
establish the process-exit predicate that proves an MDL user view has been
removed. Neither a numeric PID nor PsGetProcessExitStatus suffices. An explicit
close in the owning process followed by GPU revoke is straightforward; abrupt
exit and stop/remove also need a supported owner and cleanup ordering. A design
that keeps the EPROCESS/MDL until the process object is signaled avoids one
cross-context unmap race, but the exact mapping-teardown guarantee and adapter/
worker lifetime still need to be established. This is an unverified contract,
not proof that the architecture is unsupported or that new firmware is needed.
A local documentation/source-path question was sent during the task.

CPU-view removal and GPU revoke are distinct conditions. Free/reassign physical
pages only after both are established. Unknown accepted work, failed TDR stop,
failed unmap/revoke and process-exit races must retain ownership. Quarantining
forever is containment, not a complete production cleanup implementation.

## Platform comparison and ownership

Asahi primary `asahi-linux-asahi/drivers/gpu/drm/asahi/gem.rs:160,195` aligns GEM
objects to native pages and distinguishes CPU cache policy; the Windows runtime
can provide 4 KiB scattered backing instead. Asahi's GEM lifetime is not a
Windows user-MDL mapping teardown contract.

Saved exact m1n1 `075e3980` source in shared handoff `rc46/source-evidence/`:
`src/hv_agx_retained_platform.c:50` checks eligible native-aligned RAM and each
4 KiB quarter; `src/hv_agx_gpuva_v5.c:459` accepts exclusive REGISTER_BACKING
with owner/generation checks, table exclusion and finite raw-RAM capacity.
`shared/src/apple_agx_gpuva_g3_graph.c:530` already selects that protocol for
PrivateBacking. Thus a separately owned native ring does not require a firmware
change. Its grants still require acknowledged revoke before page return.

Saved Mu `361a2134` in shared handoff `rc30/source-evidence/`,
`MemoryInitPeiLib.c:252,440,510`, publishes/reserves firmware, low-window backing
and the AGX reserve. Changing boot RAM publication does not implement Windows
mapping lifetime. Live ADT/register/IRQ/ACPI state and launch contracts were not
remeasured; the supplied saved experiments remain the reference.

KMD owns physical chunks, mappings, authorization and grant retirement; UMD owns
slice allocation, CPU access and submission holds; Mesa owns GPU copy/hazard
ordering; VidMm owns ordinary canonical placement. m1n1 retains grant/UAT/TLBI
ownership and Mu retains boot RAM/ACPI ownership. Initialization, interrupts,
power and recovery behavior remain unchanged.

## Implemented deterministic invariant

`render-admission/umd/include/umd_staging_ring.h` has no OS dependencies or
exported symbols. It is caller-serialized, bounded to 128 MiB and 256 live
metadata entries, and must be initialized once for a new backing owner.

Reservations account for alignment and wrap padding. Several pending slices
can belong to one accepted submission. Before acceptance they cancel in reverse
order. Commit associates them with a strictly increasing completion fence; reclaim walks only the
FIFO prefix whose GPU work and optional CPU consumers are finished. Readback
release before completion and stale tickets fail. Ticket/fence exhaustion
refuses new work instead of wrapping. Unknown submission completion poisons the
ring and prevents reuse. Reinitialization is not a recovery API.

The caller must provide an actual completion proof on the same timeline, issue
CPU/GPU memory barriers, and own the mapping lifetime. The allocator cannot
validate user-supplied fences or replace KMD authentication. It has no runtime
call sites; performance and mapping security are not tested by its unit tests.

## Remaining implementation and smallest hardware checkpoint

Still required: native backing/accounting owner; versioned mapping ABI and its
validation/cleanup regression; two-span graph root transactions; UMD storage and
routing; GPU copy or ring-backed staging consumers for queries/textures; fallback
receipts; integration and native WDK ARM64/x64/x86 compile/link checks. Those are
not included in this branch. No current hardware run is justified by metadata
code alone.

After those gates, integration's smallest correctness experiment is exact bytes
through CPU upload -> GPU copy -> CPU readback, wrap with two in-flight batches,
CPU-held readback retention, foreign-process rejection, zero-before-exposure,
abrupt process exit with queued work, TDR/device removal, failed cleanup and
aggregate budget exhaustion. Require no stale CPU mapping, no early physical
reuse, exact contents and normal reclamation. A GPU fence alone is insufficient.

Only after correctness, run the requested HD 50k and FHD 100k arms with identical
firmware/caps/signing/RAM/pool/workload settings and compare against supplied
EXP1975a **1632 / 9.8 FPS** and EXP1973a **1275 / 7.6 FPS**. Use the supplied
macOS FHD 100k score **7700** only for that matching workload; no matching HD
50k macOS reference was established here. The task's observed copy components
(39.8 + 6.4 ms HD, 48.3 + 9.8 ms FHD) are opportunity bounds, not a promised
additive frame saving. Preserve producer validity/censoring flags when analyzing
wait receipts. The saved EXP1975 log was inspected read-only.

Proposed receipts (not implemented): ring capacity/live/high-water bytes,
served upload/readback bytes, pending/retired spans, wrap/full waits, fallback
reason/bytes (capacity, layout, shared/imported, envelope, mapping unavailable),
map/refusal status and cleanup state. Near-zero copy_escape depends on measured
coverage, particularly large persistent heaps; bounded capacity alone cannot
promise it. This branch saves **zero** runtime copy escapes.

Hash-pin the eventual matched package and preserve known-good recovery before
that future experiment. Collect evidence before ordinary exact-package
GPU-visible cleanup; GPU-hidden recovery remains emergency-only. RC52 performed
no build, install, launch, guest action or recovery.
