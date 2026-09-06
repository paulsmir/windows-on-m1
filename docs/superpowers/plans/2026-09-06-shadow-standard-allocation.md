# CDD shadow standard-allocation implementation plan

**Goal:** close the post-EXP500 CDD size-query `STATUS_INVALID_PARAMETER` by
supporting the exact type-2 shadow allocation that current Windows requests.
**Architecture:** FULL GRAPHICS WDDM 3.0. Reuse the existing 48-byte allocation
description and single allocation object; change no capability, resource owner,
memory topology, paging path, AGX protocol, firmware, or platform state.
**Execution:** inline as EXP501 under the existing standing source-through-hardware
authorization.

## Evidence and inspected sources

- EXP500 `stacks.csv` lines 23052-23120 records
  `ADAPTER_RENDER::DdiGetStandardAllocationDriverData ->
  DXGDEVICE::CreateStandardAllocation -> ADAPTER_DISPLAY::CreateCddAllocations`
  and the CDD `Failed to find size of PrivateDriverData buffers` C000000D event.
- Current `dxgkrnl.sys` SHA256
  `fd417addb93f4d31d0f0fa9216d9bf41bace190b2891b632dd3e5fcb031c0f66`
  with current public symbols: `CreateCddAllocations` starts at RVA `0x0f2968`.
  The EXP500 return address is RVA `0x0f31b8`, exactly `+0x850`. At
  `0x0f3154..0x0f31b4`, the caller stores value 2 in the standard-allocation
  type field, stores 2 as the creation-data size, points at the two dimensions
  plus format structure, and calls `CreateStandardAllocation`; the return at
  `+0x850` is tested for failure. Type 2 is therefore current
  `D3DKMDT_STANDARDALLOCATION_SHADOWSURFACE`, not the earlier type-1 primary.
- Pinned WDK 10.0.26100.0 `shared/d3dkmdt.h` defines type 2 and the complete
  `D3DKMDT_SHADOWSURFACEDATA` contract: Width, Height, Format are input and
  Pitch is output because the allocation is lockable. Pinned `d3dkmddi.h`
  maps type 2 to `pCreateShadowSurfaceData` in
  `DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA`.
- Microsoft `DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA` documentation requires
  the sizing call to report private-buffer sizes without modifying the standard
  creation-data union; at least one private size must be nonzero. Microsoft
  `D3DKMDT_SHADOWSURFACEDATA` documents a lockable surface matching the primary
  in format and resolution and requires the driver to return Pitch.
- Microsoft render-only sample `RosKmdAdapter::GetStandardAllocationDriverData`
  was read completely for the common allocation ABI only. Its type-2 branch
  derives a four-byte BGRA linear surface and returns width-times-four Pitch.
  The sample also treats its shadow as shared and leaves its per-allocation
  CpuVisible bit clear, but unlike this driver the sample publishes its local
  segment itself as CPU-visible. That placement assumption is not transferred;
  no external code is copied and its scheduler/admission assumptions are not
  used.
- Microsoft `DXGK_ALLOCATIONINFOFLAGS_WDDM2_0::CpuVisible` documentation is
  explicit that KMD must set the bit for `pfnLockCb` to succeed. Pinned WDK
  26100 confirms it is bit 0 and independent from the already-set bit-15
  `AccessedPhysically`. Therefore merely recording `CpuVisible=1` in KMD-private
  data while returning FlagsWddm2 value `0x8000` would knowingly violate the
  lockable-shadow consumer contract.
- Current `allocation_windows.c` reports 48/0 before reading creation data, but
  its supported-type guard admits only types 1 and 4. That exact guard causes
  C000000D for the observed type-2 sizing call. `render_allocation.c` already
  provides checked 16-byte pitch/size derivation and the existing description
  validation/object lifetime. The existing type-4 CPU-visible staging
  classification is the closest internal representation for a lockable shadow.

## Contracts and ownership

### WINDOWS CONTRACT:

Windows owns the CDD request, display-mode dimensions and format, standard
allocation lifetime orchestration, VidMm residency, and later lock/copy calls.
For sizing, KMD returns allocation-private size 48 and resource-private size 0
without changing Width, Height, Format, or Pitch. For materialization, KMD
validates non-null type-2 data and BGRA format, derives a checked linear
description, and returns Pitch only after all validation succeeds.

### AGX/ASAHI CONTRACT:

No AGX operation occurs in this DDI. The accepted retained-root AGX owner,
Asahi-derived UAT/queue semantics, m1n1 broker state, Mu publication, interrupts,
and recovery contracts remain unchanged and are not requalified by EXP501.
This callback only creates Windows-facing allocation metadata.

### TRANSLATION:

Map a type-2 shadow request to the existing 48-byte description with the caller's
Width/Height, four bytes per pixel, BGRA format, the existing CPU-visible staging
classification, and `CpuVisible=1`. Use `AdmissionAllocationDescribe` for checked
16-byte Pitch and total size. Return that Pitch through the WDK output field;
the existing `CreateAllocation` remains the only allocation object owner and
copies the private `CpuVisible` classification into FlagsWddm2 bit 0. Primary
descriptions remain `CpuVisible=0`, so this does not mark the shared primary
CPU-visible. No `PermanentSysMem`, `Cached`, MapAperture2, segment, or placement
claim is added: Microsoft requires none of those as a companion to CpuVisible,
and current transfer/paging behavior remains unchanged.

### ATOMIC CONTRACT:

The type-2 sizing/materialization branch, returned shadow Pitch, and
FlagsWddm2.CpuVisible propagation are one indivisible documented lockable-shadow
contract. Without the branch Windows fails sizing; without Pitch the structure
violates `D3DKMDT_SHADOWSURFACEDATA`; without the allocation flag Microsoft's
documented lock callback fails. The regression executes both production DDIs
and proves the type-1 primary remains `0x8000` while a CPU-visible shadow
description becomes `0x8001`. No other flag or segment property changes.

### WHAT IS STILL UNKNOWN:

Whether current dxgkrnl accepts the materialized type-2 description and what
next callback or lifecycle boundary follows. The existing placement and paging
implementation is not changed or declared proven; a later lock, residency,
transfer, submit, completion, fence, or present boundary may expose a separate
defect. One EXP501 natural bind is the discriminator.

## Minimal implementation and verification

- [x] RED: execute the production callback with the complete type-2 WDK shape;
  the current supported-type guard returns C000000D on the size query.
- [x] GREEN: admit type 2, leave sizing behavior unchanged, validate/materialize
  the shadow through the existing description helper, set Pitch only after
  success, and propagate the description's CpuVisible bit in CreateAllocation.
  Preserve primary flags and all allocation ownership/paging/platform behavior.
- [x] Run the focused allocation test and the established relevant suite; review
  the exact diff and commit only source, test, and this plan.
- [ ] Append one valid 19-column CHANGES row with the full source commit, freeze
  exact EXP500 source plus this commit's changed files, and run pinned WDK/SDK
  26100 KMD, UMD, analysis, Universal, Inf2Cat, signing, version, and hash gates.
- [ ] Preregister and perform one exact EXP501 package bind from verified clean
  ordinary G2. Collect ETL, receipts, events/dump, host log, then remove the exact
  package and restore/verify ordinary 377/392.

Recovery remains ordinary 377/392; emergency 377/385 is permitted only if the
exact candidate makes the GPU-visible guest unrecoverable. A pass proves only
the observed standard-allocation boundary, not paging, AGX execution, completion,
fence, presentation, acceleration, or CS1.6.
