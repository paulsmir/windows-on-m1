# T7h: explicit GPU-only General allocations

Offline candidate on `codex/t7h-gpu-only-local`. No hardware, guest, package,
builder or other-worktree operation. The supplied EXP1918U5 observation, not a
new measurement, associates the failed 129 MiB range with no CPU-access event.

## WINDOWS CONTRACT

Pinned `.local/reference/wdk26100/d3dkmddi.h:3804` defines WDDM2 CpuVisible;
`:3912` defines DXGK_ALLOCATIONINFO and its preferred/read/write/eviction sets.
The Microsoft [allocation flags contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_allocationinfoflags)
requires CpuVisible for LockCb: without it the callback returns an error.
The [allocation information contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_allocationinfo)
uses the write segment set for WDDM2 placement; keep read/write identical and
preferences inside that set. EvictionSegmentSet=0 requests transfer directly to
page-locked system backing, without an aperture mapping for eviction.

Thus GPU residency in a memory segment is distinct from system storage while
evicted. A sole supported memory segment does not permit resident aperture
fallback. Under pressure VidMm must recover legal placement or refuse residency;
it may evict other allocations. This is an inference from the segment/residency
contract, not proof of Windows accepting this candidate or an OOM guarantee.
The [segment flags contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_segmentflags)
distinguishes a memory segment from an aperture. PopulatedFromSystemMemory describes where the KMD obtains a memory segment's
pages, not permission to replace the allocation's supported set with arbitrary
system pages. `render-admission/src/memory_windows.c:21` emits these flags;
`pool_windows.c` checks native contiguous granules before publishing them.

Existing CreateAllocation reserves pool backing before publishing a handle and
can return STATUS_NO_MEMORY there. MakeResident can also fail; neither boundary
is a new promise of successful allocation under pressure. EXP1027
(`investigation/EXPERIMENTS.md:53197`) rejected **CpuVisible=1** memory-only
allocations. The new request must carry CpuVisible=0, not repeat that shape.

## AGX/ASAHI CONTRACT

Paths below are under `drivers/apple-agx/` unless `.local/reference` is explicit.
Asahi UAT uses 16 KiB pages (`.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi/pgtable.rs:39`).
Mesa's size/alignment rounding cannot make scattered Windows PFNs contiguous.
At base commit `68a97a70ba696c19e0a39a1926412ada33cb73cf`, the winsys grants CPU read/write to every General BO
(`mesa/winsys/agx_win32_asahi_bo.c:356`). C45 only avoided eager mapping
(`mesa/winsys/agx_d3d12_windows.cpp:432`); it did not change this backing contract.

CPU-map inventory (reachable means internal access, not a legal API MapHeap):

| Path | GPU-only heap / DEFAULT texture reachability and treatment |
| --- | --- |
| agx_bo_map / native_map | Reachable final pointer provider. Refuse explicit GPU-only BOs before locking or returning a pointer, emit measure-gpu-only-map. Cache keys include flags. |
| AgxWin32AsahiBufferWriteMap | Eager owner map is unreachable for NOT_AVAILABLE (C45). Direct unexpected calls return NULL through the same BO guard. |
| buffer transfer_map | Reachable: ExecuteIndirect, predication/argument reads and buffer sides of texture copies. Use a CPU-visible staging buffer, GPU resource_copy_region and writer synchronization. |
| texture transfer_map / agx_map_texture_cpu | Reachable: readback, upload, software copy fallback. Eligible color and single-plane Z16/Z32 depth textures use the existing staging-blit path for both directions. BC, unsupported staging formats, combined depth/stencil and MSAA textures remain CPU-visible; see the T7h2 audit below. |
| agx_shadow CPU memcpy | Reachable through transfer preparation normally. Prohibit shadowing GPU-only BOs; staged transfers already bypass CPU synchronization/shadowing. |
| clears and GenerateMips | GPU clears/blits do not map target storage; transfer-based CPU fallback uses the same staging path. The generated D3D11 frontend never opts in. |
| tile zero/scratch, sparse tables | Unreachable as opted-in resources: separate BO constructors, deliberately CPU-visible for initialization. A reserved virtual resource is not itself opted in. Physical NOT_AVAILABLE tile heaps can be. |
| shader/linker/disk-cache, sampler/query heaps, batch pools, encoders, scratch/debug/printf | Unreachable as opted-in application resources: independent internal BO creation with existing flags; all remain CPU-visible. |
| D3D11 staging and presentation/shared imports | Unreachable opt-in; preserve current constructors and borrowed/shared backing. |
| UMD screen staging/SystemDirect/Borrowed | A GPU-only slot has one canonical allocation, no CPU flags and no private/borrowed staging. The existing SystemDirect bookkeeping bypasses staging escapes and permits idle trim; it does not grant CPU access. ScreenMapBuffer refuses before preparatory callbacks. Other slots retain their exact policy. |

Primary Mesa evidence: `.local/reference/mesa/src/gallium/drivers/asahi/agx_pipe.c:567`
(BO flags), `:697` (shadow memcpy), `:830` (staging allocation), `:912`
(transfer map), `:1032` (writeback); `agx_blit.c:618` (GPU buffer copy, byte-tail
handling, then texture CPU fallback); `agx_state.h:960` (CPU texture pointer).
Owner CPU readers: `mesa/winsys/agx_d3d12_windows.cpp:658`, `:2122`, `:2174`,
`:2246`. BufferWriteMap: `agx_win32_asahi_scene.c:233`.
No external source is copied into the repository: the existing projection
patches the notice-bearing pinned Mesa file in generated output.

## TRANSLATION

Use Gallium's existing PIPE_RESOURCE_FLAG_UNMAPPABLE as the per-resource intent;
project it into a private AGX_WIN32_BO_GPU_ONLY flag. Emit a new
AppleAgxWin32BufferGpuOnly access request, with GPU access and **no CPU bits**.
Validate it only for General, LOCAL-version, CpuVisible=0 descriptions; reject
contradictory and borrowed requests. Advertise the bit in the General class's
private device-info access mask; old KMDs refuse it instead of silently falling
back. This requires matched UMD/KMD, not UMD hot-swap.

NOT_AVAILABLE CreateHeap already passes cpu_visible=false to CreateBuffer.
Committed DEFAULT 2D textures opt in explicitly from the DDI; default 1D/3D
constructors require NOT_AVAILABLE already. Keep BC (including volumes), formats without RT+sampler support (for example RGB9E5),
combined depth/stencil, MSAA and presentation imports CPU-visible. T7h2 adds Z16/Z32 via their color staging formats (see below). Texture eligibility follows API CPU intent and the staging format capability,
not a trace-derived size/content/bind combination. Current 1D/1D-array constructors
only admit R32_UINT and R8G8B8A8_UNORM, both supported staging formats; the 1D-array staging
target is converted to 2D_ARRAY and sized by the transfer box depth.
CPU-visible General, shader and encoder allocations remain unchanged.

KMD returns CpuVisible=0, read/write mask 4, preference 3 with a pool; without
a pool use mask 2, preference 2. EvictionSegmentSet=0 and GPUVA access stay.
Existing pool reservation/refusal and T8 headroom are unchanged. The KMD owns
backing/placement eligibility; VidMm owns placement/eviction; UMD owns CPU access,
GPU copies and fences. m1n1 owns stage-2/broker and Mu the firmware reserve.
No initialization, DMA protocol, interrupt, power or recovery contract changes.
The worktree's uninitialized m1n1/Mu sources and live launch contracts are not
revalidated; no platform change is justified by this offline task.

## Implementation plan

Execute inline, tests before production changes, following the task's approved
scope. No additional approval is needed for this offline work.

1. Add executable request-validation and KMD body replays: valid GPU-only,
   malformed CPU/share/class/version combinations, pool/no-pool, reservation
   refusal, and unchanged CPU-visible General. Observe RED, implement private
   request plumbing and explicit placement, observe GREEN.
2. Add production-body tests for owner intent, winsys refusal/cache separation,
   and projected staging transfers (read/write bytes, synchronization, cleanup
   on failure, no recursive GPU-only staging). Implement the owner and Mesa
   projection plus receipts; preserve the existing CPU-visible path.
3. Run focused tests, full required baseline to BASELINE_OK, inspect protected
   invariants and diff. Make small implementation commits, then write REPORT.md
   with exact results, evidence, unknowns and Claude's checkpoint. Integration
   owns EXPERIMENTS.md and CHANGES.csv under the handoff-specific rule.

Review focus: unexpected CPU access; BC/stencil fallback; old/new KMD mismatch;
CPU-visible cache reuse; failed staging allocation/map and resource cleanup.

## WHAT IS STILL UNKNOWN

Windows acceptance, actual VidMm pressure/eviction behavior, MakeResident OOM
propagation, Windows compilation, runtime staging coherency/performance, and
the exact VA/lifetime identity of the refused GravityMark resource remain
hardware/build questions. T7h2 covers the Z32F layout implicated by the supplied
review. Host replay cannot establish hardware success. CPU-visible
resources can still fall back to scattered aperture backing; this candidate
does not claim to repair that separate class.

## Checkpoint and recovery (Claude only)

WHY THIS HYPOTHESIS: EXP1918U5 reports no CPU-access event for the refused
range; the winsys nevertheless gives it CPU flags; EXP1027/EXP1917 rejected
removing aperture with CpuVisible=1, leaving CpuVisible=0 as the discriminant.

Build a matched KMD/UMD package and reboot through the existing full-owner
contract. First verify NOT_AVAILABLE allocation admission and an internal
staged readback with unchanged upload/readback maps. Then run GravityMark
D3D12 2k and 10k with the short profile, beyond the previous ~22 s boundary.
Expected: no G4 Unmapped, no removal, and native-memory provenance for opted-in
BOs. Fail on AllocateCb E_INVALIDARG, map-refusal receipt on a required path,
G4 Unmapped/removal, content mismatch, or desktop loss. Record MakeResident
E_OUTOFMEMORY and earlier allocation OOM separately from invalid-shape refusal.
Collect receipts first, then exact-package cleanup in the ordinary GPU-visible
Code28 recovery; immutable GPU-hidden recovery is emergency-only. Preserve and
hash known-good artifacts before installation. No recovery operation runs here.

## Source-review corrections and verification scope

Read-only review found two newly reachable fallback hazards before commit:
BC3D/RGB9E5 staging eligibility, and the missing 1D-array staging case. Dedicated
replays failed on those paths before their fixes, then passed. The final rule
checks render-target plus sampler support before opting in 2D/3D color formats;
this is not a new format capability advertisement. Host byte tests exercise
actual transfer dispatch/copy-helper offsets against a controlled GPU-copy
callback; they do not prove hardware format conversion or cache coherence.

The original request parser rejected the GPU-only wire request; the original
winsys still returned CPU flags and allowed a direct BO map. New executable
regressions observed those failures. CPU-visible placement, locks, SystemDirect
limits and old map/shadow behavior are retained as controls. Final test totals,
known baseline failures, commit IDs and source hashes live in the handoff report.

`measure-gpu-only-map` has HRESULT E_INVALIDARG and three hexadecimal words:
slot token low/high, then requested CPU access for the authoritative screen
refusal; the native BO refusal uses its (32-bit) token, zero high word, and
native BO flags (GPU-only bit 29). No pointer/content is recorded. Normal
staging transfers should emit no such refusal. `ddi-slot-create` carries the
new wire flags (normally 0x2c), and existing KMD allocation/G4 receipts provide
CpuVisible and segment provenance. UMD slot SystemDirect remains set because
there is one canonical allocation, independently of CPU permission.

Prior references used as contracts, not another archaeology pass:
T7c/T7f/T7g, C45, R136 (CpuVisible=0 is a distinct control and forbids Lock),
R148 (canonical GPU-local allocation versus CPU staging), EXP936 (direct
borrowed primary identity), and EXP853 (4 KiB scatter cannot form 16 KiB leaves).
No primary/shared import is reclassified. The supplied EXP1917 result and
EXP1918U5 trace remain the current hardware evidence; no old result is rewritten.


### CPU-map callsite index

Pinned Mesa paths are relative to `.local/reference/mesa/src/`. These are the
raw map callsites behind the reachability inventory, including internal-only
BOs that must retain CPU access:

- `asahi/lib/agx_device.h:206`: cached agx_bo_map_placed pointer provider;
  `asahi/lib/agx_bo.c:396`: upstream eager mapping (replaced Windows BO owner).
- `gallium/drivers/asahi/agx_pipe.c:697`, `:983`, `:1026` and
  `agx_state.h:960`: resource shadow, staging map, linear map, texture map.
- `gallium/auxiliary/util/u_draw.c:145`, `:165`, `:214`, `:233`: indirect
  count/argument reads; D3D12's CPU expansion also calls OwnerReadBuffer at
  `drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp:6400` and `:6414`.
- `gallium/drivers/asahi/agx_state.c:1487`, `:2788`: shader upload and sampler
  heap; `agx_disk_cache.c:106`, `:111`: shader binary upload/read;
  `asahi/lib/agx_linker.c:125`: linked shader code.
- `gallium/drivers/asahi/agx_query.c:106`, `:170`: query BOs;
  `agx_batch.c:84`, `:169`: internal batch storage/timestamps;
  `agx_pipe.c:1521`, `:2510`: command stream offsets and texel-fetch sampler.
- `asahi/lib/pool.c:75`, `agx_scratch.c:116`, `agx_device.c:354`, `:703`,
  `:717`: internal pools, scratch and debug/printf BOs. None receives the
  application resource's UNMAPPABLE flag.
- `drivers/apple-agx/mesa/winsys/agx_win32_asahi_bo.c` sparse tile-zero/scratch
  and page-table maps remain separate constructors. Resource raw CPU copies
  in `agx_win32_asahi_scene.c:246` have only D3D10/11 callers in the projection
  (`build-native-asahi-state.py:5158`, `:5258`), which do not opt in.
- `drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c:797`, `:1027`
  contain temporary staging locks; SystemDirect bypass at `:1191` excludes
  GPU-only canonical slots from those copy escapes. Authoritative map refusal
  is in `umd_win32_screen.c:1020`; the protected SystemDirect expression is
  unchanged at `:642`.


## Final offline verification

The committed implementation passed 82 focused tests (20 modules). The required
baseline on that implementation, including all 12 new regressions, returned:

```text
ran 413 tests in 175 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

The six failures are unchanged known-baseline entries; none was added or hidden.
The real-broker replay separately reports its existing uninitialized-m1n1 skip.
Three extracted-code mutations (map guard, LOCAL plumbing, staging write offset)
were caught. All four protected invariants and `git diff --check` pass. The final
review found no remaining source blocker after the format/array fixes. Full
Windows compilation, GPU content/coherency and VidMm acceptance remain unknown.

## T7h2 follow-up: committed depth (supersedes the blanket depth exclusion)

The supplied review identifies the only 128 MiB committed texture as depth,
not color. The first candidate's depth exclusion therefore misses the strongest
candidate for the refused BO. This follow-up changes only UMD eligibility; the
explicit GPU-only request, KMD placement and CPU-visible controls stay intact.

### Source evidence and design

Pinned `mesa/src/` paths below are under `.local/reference`.

- `asahi/layout/layout.c:269` adds eight metadata bytes per 16x16 tile per
  layer. Running the real host `ail_make_miptree` for Z32F 4096x4096, **two
  array layers, one mip** gives data `0x8000000`, metadata `0x100000`, total
  `0x8100000`. One layer/one mip gives `0x4080000`; one layer/two mips gives
  `0x5602b80`; two layers/two mips gives `0xac05700`. Thus the size agrees
  exactly with two layers, not with interpreting `l2` as two mip levels.
  This corroborates the supplied receipt; it is not an allocation admission rule
  or proof of resource identity without a VA/lifetime join.
- `gallium/drivers/asahi/agx_pipe.c:803` maps physical Z32F to R32F and Z16
  to R16_UNORM for staging. `:830`, `:871`, `:891` construct linear color
  staging and symmetric GPU blits using the **physical layout format**.
  `:933`, `:978`, `:983`, `:1039` read by blitting/synchronizing staging,
  map only its BO, and write back by blitting and flushing staging readers.
- Unmodified Mesa CPU-detiles uncompressed depth at `:986`/`:1000` and
  CPU-retiles at `:1044`/`:1050`. Small base levels, small mip tails,
  `AGX_DBG_NOCOMPRESS` and incompatible binds can disable compression
  (`agx_pipe.c:401`, `asahi/layout/layout.h:353`, `:444`). Depth bind
  forbids linear (`agx_pipe.c:342`). T7h's existing UNMAPPABLE projection
  forces staging independently of compression and blocks shadow memcpy
  (`build-native-asahi-state.py:379`, `:412`); therefore the opt-in must
  cover the uncompressed tails too, without relying on a compression query.
- `agx_blit.c:40`, `:522` handle effective depth/color formats and legalize
  compression before compute/util_blitter GPU work. Texture copy's CPU fallback
  (`:674`) calls the normal transfer interface, so it reaches staging too.
  `agx_pipe.c:1170` copies the resource template on decompression, retaining
  UNMAPPABLE. ClearDepthStencil in the owner uses GPU clears, not CPU storage
  (`agx_d3d12_windows.cpp:2026`). Readback and upload use texture_map or C70's
  separate cached CPU-visible staging (`:2116`, `:2129`, `:2271`). Direct
  agx_bo_map/BufferWriteMap remain forbidden by the existing winsys guard.
- `gallium/frontends/d3d10umd/Format.cpp:96`, `:114`, `:188`, `:192`
  translate D32/D16 and R32/R16_TYPELESS with depth intent to Z32F/Z16.
  The owner passes depth intent before eligibility (`agx_d3d12_windows.cpp:963`).
- Combined Z24S8 and Z32S8X24 are **not interleaved in their GPU BO**:
  `u_transfer_helper.c:199` lowers them to Z32F plus a separate S8 resource;
  Asahi enables SEPARATE_Z32S8, SEPARATE_STENCIL and Z24_IN_Z32F
  (`agx_pipe.c:2494`). The helper CPU packs/unpacks between mapped staging
  planes and its own allocation (`u_transfer_helper.c:421`, `:528`). The
  second-plane map at `:437` is not checked for NULL before pack routines.
  These formats, Z24X8 and stencil-only remain CPU-visible in this candidate:
  enabling them requires multi-plane conversion/failure-cleanup coverage,
  beyond the single-plane regression that covers the observed Z32F resource.
  This is a conservative scope decision, not a claim that staging Z/S is
  impossible. Their current compressed transfers already use staging.

Reachability for committed single-plane DEFAULT depth: API CPU map is disallowed;
internal texture copies/readback/upload can reach transfer_map, but stage through
R16/R32. Clears/draws use GPU addresses. Indirect reads are buffer-only. Internal
shader/query/tile/scratch BOs and D3D11 staging are separate allocations. No raw
CPU-map caller in Asahi accesses an application depth BO outside transfer/shadow
(the callsite index above applies). Shared/presentation, BC and MSAA retain
existing paths. Combined stencil conversion is deliberately outside the opt-in.

Windows CpuVisible=0/LockCb and native-only placement contracts above are
unchanged; no new Windows interface, firmware, m1n1, DMA or interrupt behavior
is introduced. This is offline source work; no live state or other worktree was
accessed. The worktree-local `.local/tandem/REVIEW.md` is absent; the task's
T7h2 review is accepted and is the evidence driving this correction.

### Implementation plan (inline, authorized by TASK.md)

The supplied task defines the approved scope:

1. Extend `tests/test_d3d12_gpu_only.py` with failing Z16/Z32 eligibility cases,
   negative combined/stencil/MSAA/BC/CPU-access/placed cases, and capability
   refusal for each staging format. The existing DDI and owner bodies run.
2. In `AgxD3d12OwnerCreateTexture2D`, select the proven color staging format
   for Z16/Z32 before the existing render-target/sampler capability check.
   Keep every other depth format excluded. No size or bind-combination shortcut.
3. In `tests/test_umd_gpu_only_transfer.py`, execute projected transfer
   map/unmap, actual staging constructor/format and both blit helpers, with
   controlled allocation/scheduling callbacks and a bounded GPU-copy oracle. Verify bytes,
   subresource boxes, read synchronization, write flush, no direct BO map,
   compressed/uncompressed paths, and allocation/map failure cleanup.
4. Run targeted tests and the complete baseline to BASELINE_OK, check protected
   invariants/diff, commit this independent correction, then write REPORT.md.
   Integration owns experiment/change ledgers under the handoff rule.

### Unknowns and hardware checkpoint

Host execution cannot prove GPU depth/color bit preservation, cache coherence,
Windows acceptance or pressure behavior. Run a matched KMD/UMD package with a
reboot (not hot-swap), first a Z32F two-layer DEFAULT upload/clear/readback and
Z16 control including a small uncompressed mip, then GravityMark D3D12 2k/10k
short profiles beyond ~22 s. Require native-only placement, no required-path
measure-gpu-only-map, no G4 Unmapped/removal, and correct contents. Distinguish
AllocateCb E_INVALIDARG from allocation OOM/MakeResident E_OUTOFMEMORY. Preserve
and hash known-good recovery artifacts, collect receipts before exact-package
cleanup, and return to the normal GPU-visible Code28 recovery; immutable
GPU-hidden recovery remains emergency-only. No hardware work is performed here.


### T7h2 verification and review

The eligibility regression failed before the fix on the first Z16 positive
case, then passed with the format translation. The eight focused modules ran
59 tests successfully. The full required baseline on the final code/tests was:

```text
ran 414 tests in 175 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

The new transfer fixture uses actual Mesa types/format helpers, AIL layouts,
and projected staging/map/unmap bodies. Controlled callbacks replace allocation,
scheduling and GPU execution; they are not a hardware format-conversion test.
All three extracted-source mutations were caught: dropping forced staging,
using R16 for Z32 staging, and losing the writeback layer. No production source
was modified during mutation checks. Protected invariants and diff checks pass.

Independent read-only review found no blocking issue. Its optional separate
READ-only/WRITE-only depth success cases remain future coverage: this fixture
covers READ|WRITE, and the existing transfer test already covers the separate
usage branches for buffers. Combined Z/S stays excluded. The real broker replay
skips because m1n1_windows is uninitialized. Known baseline entries are unchanged.

The shared tandem review was rechecked before the implementation commit; its
SHA-256 remains 753bdfe638d6171954875a5fc9a58c09761999ffcab89b9d216f566c7e1300c5.
Per-OPEN-item dispositions remain as recorded in the handoff's
`T7h2-review-dispositions.md`; no historical hardware hypothesis is reopened.
