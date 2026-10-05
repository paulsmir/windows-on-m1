# Standard primary allocation implementation plan

**Goal:** unblock EXP496 CDD shared-primary creation using the existing allocation owner.
**Architecture:** FULL GRAPHICS WDDM3.0, existing 48-byte allocation ABI and local
segment2. Shared primary uses the existing linear texture backing description;
Windows owns primary/source tagging and VidMm residency. No new platform path.
**Spec:** EXP496 current caller and Microsoft standard-allocation DDI contract below.
**Execution:** inline; standing user authorization covers implementation/build/hardware.

## Evidence and inspected contracts

- EXP496 stacks-raw.csv at42565031: CreateCddAllocations+4b8 ->
  DXGDEVICE::CreateStandardAllocation+284 -> size-query C000000D.
- Current dxgkrnl fd417add... source RVA f2d94/f2d98 sets type1;
  f2d64..da0 fills2560x1600/A8R8G8B8/source0 structure. 2af6c8..dc
  forwards that type/data with NULL private buffers. 2afa28..50 tags primary
  and source in Windows allocation, not a legacy KMD allocation flag.
- allocation_windows.c accepts only type4; also writes GDI Pitch during sizing.
- Microsoft DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA: sizes only when private
  buffers NULL, creation union immutable during sizing, resource size may0.
  https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_getstandardallocationdriverdata
- Microsoft D3DKMDT_SHAREDPRIMARYSURFACEDATA and DXGKARG_CREATEALLOCATION:
  source identity supplied by Windows, KMD resource handle optional. With zero
  resource private bytes there is no additional KMD resource object to own.
- Pinned26100 shared/d3dkmddi.h allocation flags: WDDM2 flags have no legacy
  primary bit. Microsoft RosKmdAdapter.cpp GetStandardAllocationDriverData and
  CreateAllocation read fully: shared/primary local allocations are not marked
  CPU-visible or cached. RENDER_ONLY reference used only for common allocation
  DDI, not admission/scheduler. No reference code copied.
- Existing render_allocation.c computes16-byte pitch and64KiB residency size;
  scanout_windows.c requires native2560x1600/BGRA/10240-byte stride and uses
  existing local-segment address translation. memory_windows.c preserves the
  existing segment contract. No Asahi/m1n1/Mu behavior changes; accepted
  retained-root477/platform406 remains immutable, not requalified.

## Ownership and translation

Windows: standard request, primary/source tagging, resource grouping, residency.
KMD: description validation, one allocation object/open count/lifetime, existing
local memory/paging owner. No separate resource allocation needed for one object.
m1n1/Mu: unchanged broker/DCP/AGX initialization, IRQ and recovery owners.
Map type1 native request to the existing texture description, CpuVisible0.
Do not add hardware capabilities, new ABI, shadow/staging implementations,
scheduler changes or resource sharing beyond the existing one-allocation owner.

## Task — complete the standard-primary description contract

Files: allocation_windows.c; tests/test_apple_agx_render_standard_allocation.py.

- [ ] Execute actual production callback in a minimal WDK ABI shim linked to
  real render_allocation.c. RED: type1 size/data fail today; GDI sizing changes
  Pitch. Assert query preserves input, primary output pitch10240/bytes16384000,
  format21, texture type, CpuVisible0; reject wrong source/dimensions/format,
  short buffer, null creation data in materialization, unknown type/adapter.
- [ ] Size phase: validate adapter/index/supported type/no resource buffer;
  report48/0 and return before touching union if allocation buffer NULL.
- [ ] Data phase: validate capacity before any writes; type1 requires native
  width/height, source0, BGRA format. Call AdmissionAllocationDescribe with
  texture/4B/CpuVisible0. Type4 preserves existing validation and sets Pitch
  only after success. Copy complete validated description once.
- [ ] CreateAllocation accepts Resource grouping with hResource NULL and zero
  group-private bytes, retaining the existing single allocation owner. Reject
  non-NULL resource handles as before; no fake resource object/lifetime.
- [ ] Run relevant render/feature tests; review exact diff, commit only intended
  files and append CHANGES row with full commit (implemented, not HW validated).
- [ ] Freeze exact496 plus committed changed files; pinned FRYZZING KMD/UMD,
  analysis, Universal, Inf2Cat, signing and hashes. Record EXP497 before launch.
- [ ] Verify clean ordinary G2 and exact hashes, stage497, natural full-owner
  boot once. Collect receipts/ETL/errors. Expected CDD allocation advances past
  standard-private size/data into actual allocation/next producer boundary.
  No inference of GPU execution from this result. Cleanup exact package then
  restore ordinary; inspect only next evidenced failure and continue.

Self-review: one deterministic standard-allocation contract, no ABI/platform/
caps changes. Same linear backing works with existing allocation/paging/scanout
consumers. Hardware unknown is Windows acceptance/next actual callback, not
whether a constant or callback can be guessed. Recovery unchanged377/392,
emergency385 only if the exact package makes ordinary guest unrecoverable.
