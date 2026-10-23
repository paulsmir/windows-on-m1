# D3D12 C76: open an existing shared linear texture

Offline implementation, 2026-10-23. No hardware verdict. This implements a
bounded committed-texture open path; explicit shared heaps remain fail-closed.

## WINDOWS CONTRACT

The pinned source is `.local/reference/wdk26100` (WDK 26100), with SDK types in
`.local/reference/winsdk26100`. Two premises in the original task need correction:

- `d3d12umddi.h:307` defines `D3D12DDI_HEAP_FLAGS`: there is no
  `D3D12DDI_HEAP_FLAG_SHARED`. `COHERENT_SYSTEMWIDE` is a separate DDI flag;
  this implementation does not equate it with API `D3D12_HEAP_FLAG_SHARED`.
- `d3dumddi.h:4499` defines `D3DDDI_DEVICECALLBACKS`: there is no
  `pfnQueryResourceCb` or `pfnOpenResourceCb`. The similarly named kernel
  entry points are `D3DKMTQueryResourceInfo[FromNtHandle]` and
  `D3DKMTOpenResource[FromNtHandle]` (`d3dkmthk.h:5886`, `:5899`). The open DDI
  receives handles already opened by the runtime; it must not open them again.

`D3D12DDIARG_OPENHEAP_0003` (`d3d12umddi.h:484`) supplies:

| Input | Meaning and treatment |
|---|---|
| `NumAllocations`, `pOpenAllocationInfo` | One allocation for the supported texture. Each `D3DDDI_OPENALLOCATIONINFO` supplies the opener's allocation handle plus its private description (`d3dukmdt.h:468`). |
| `hKMResource` | Opener's kernel resource handle; retained by the admission lifetime record. |
| `pPrivateDriverData`, `PrivateDriverDataSize` | Creating UMD's resource metadata, validated before allocating owner/import state. |
| `InitialResourceState` | Runtime's initial state; no creator process address or GPUVA is reused. Existing D3D12 replay handles resource transitions. |

The 0003 size/open signatures are at `d3d12umddi.h:1828`. The 0043 signatures
at `:6864` add a protected-session handle, **before the resource handle** in
Open. Separate typed wrappers reject non-null protected sessions. `static_assert`
checks cover both ABIs; CORE_0003/0010 use 0003, CORE_0070 uses 0043, and newer
tables inherit the appropriate entries.

Creation's `D3DDDICB_ALLOCATE` (`d3dumddi.h:3433`) associates per-resource private
data and an array of per-allocation private data with `hResource`, the runtime
resource. It returns `hKMResource` and allocation handles. The existing
`AdmissionUmdCreateResource` producer already records both descriptions, so C76
reuses that producer instead of inventing a second allocation protocol.

## AGX/ASAHI CONTRACT

Ordinary owner textures use `resource_create` and Asahi's chosen layout
(`mesa/winsys/agx_d3d12_windows.cpp:838`). They may be twiddled/compressed and
cannot be reconstructed from only dimensions and format. Mesa's
`src/gallium/drivers/asahi/agx_pipe.c:162` imports a handle with its modifier and
stride, and `:453` chooses linear storage for shared/scanout resources where
possible. These are source observations, not a new hardware measurement.

The existing shareable/presentation producer is
`AgxD3d12OwnerCreatePresentationTexture` (`mesa/winsys/agx_d3d12_windows.cpp:1237`):

1. `AdmissionUmdDescribePrimary` (`render-admission/umd/src/umd.c:137`) describes
   one linear 2D allocation with 16-byte row alignment and 128-byte size padding.
2. `AdmissionUmdCreateResourceWithMetadata` (`umd.c:741`) sends
   `ADMISSION_PRESENT_RESOURCE_DATA` and `ADMISSION_ALLOCATION_DESCRIPTION`
   through `pfnAllocateCb` (`umd.c:803`). Format is DXGI at resource level and
   A8R8G8B8/A8B8G8R8 at allocation level; dimensions, pitch, size and CPU
   visibility belong to the allocation description.
3. `AdmissionUmdScreenAdoptAllocation` (`umd_win32_screen.c:787`) establishes
   a borrowed screen slot. For GPU-local allocations its direct branch (`:811`)
   uses the supplied kernel allocation. CPU-visible imports take a staging plus
   separate canonical-BO path; C76 explicitly refuses them.
4. `AgxWin32AsahiImportLinearColor32` (`mesa/winsys/agx_win32_asahi_scene.c:94`)
   rebuilds an AIL linear layout, checks its pitch/extent, and imports the BO.
   It does not allocate replacement texture storage.

The D3D11 precedent is `AdmissionUmdOpenResource` (`umd.c:848`),
`AgxD3d10WindowsPresentationOpen` (`mesa/winsys/agx_d3d10_windows.cpp:960`), and
`attach_presentation_render_resource` (`:356`). The frontend routes its open
DDI to that path (`mesa/scripts/build-native-asahi-state.py:5072`).

Ownership remains: runtime opens resource/allocation handles; admission UMD
retains the runtime-resource retirement record; winsys owns the opening device's
GPUVA mapping and borrowed BO; KMD owns backing storage and allocation reference
counts. Existing firmware, power, interrupt, DMA execution and recovery ownership
are unchanged. m1n1/Mu initialization is outside this UMD-only boundary; no live
ADT/register assumptions or firmware changes are needed. The current state file
records the existing native-owner/presentation milestones; C76 does not claim a
new assisted or standalone launch contract.

## TRANSLATION

`agx_d3d12_shared_open.h:15` decodes exact-size wire records using aligned local
copies. Supported storage is GPU-local, linear, uncompressed Texture2D, one mip,
one layer and one sample, BGRA8 UNORM/sRGB or RGBA8 UNORM, within the existing
8192-dimension/16-MiB admission envelope. It validates format/storage agreement,
size, pitch, magic, version and reserved fields. It never accepts a process
pointer or producer GPUVA as resource identity.

`agx_d3d12_ddi.cpp:2530` validates one nonzero allocation and kernel-resource
handle. Sizing returns both runtime-private object sizes. Open initializes the
resource/heap, calls `AgxD3d12OwnerOpenSharedTexture` (`agx_d3d12_windows.cpp:1343`),
and records the returned BO plus the metadata dimensions/format. Textures have
no exposed buffer GPUVA (`agx_win32_asahi_scene.c:365` returns zero for them);
the actual per-device GPU mapping lives in the imported native BO. The open
path reconstructs the creator's one-level resource description and calls the
same `TextureBytes` accounting function: `CheckExistingResourceAllocationInfo`
must report the same size before and after open. The native import still uses
the exact serialized linear extent, not that larger DDI allocation estimate. A
missing heap handle uses the resource's existing `own_heap` storage. The kernel
allocation recorded by the opened heap is exactly the runtime-supplied handle.
The owner translates the input to the existing D3D10 admission open structure,
adopts the allocation, and imports the same linear layout. Destruction uses
`AgxD3d12OwnerDestroyPresentationTexture` (`:1416`) and the existing
runtime-resource retirement path (`umd.c:957`).

The existing creator already records the supported render-target texture's
creation data. No flags/caps are advertised to turn ordinary twiddled heaps
into shareable resources. Explicit heaps, buffers, mip chains, arrays, MSAA,
unsupported formats, malformed/foreign metadata and protected sessions retain
the typed `AGX_REJECT` receipt. Cross-adapter resource creation is now refused
before owner allocation (`agx_d3d12_ddi.cpp:2374`).

A necessary metadata distinction: MSAA and format-converted presentation
resources have a **different display allocation** from their render texture.
Version 3 did not identify that distinction. Version 4 keeps the same struct
size and adds `ADMISSION_PRESENT_RESOURCE_CONVERTED` (bit 2) to mark auxiliary
storage. The owner supplies it only when `record->Converted` exists. D3D12 open
requires v4 and rejects that bit; otherwise an old MSAA/R10G10B10A2 resource
could appear to open successfully as its single-sampled RGBA8 display surface.
The generic KMD/D3D11 validator continues accepting unconverted v3 data. D3D11
can consume a v4 converted record as its described display surface, as before.
A matching rebuilt KMD and UMD are required for the new v4 producer; no package
was built or installed during this offline task.

`tests/test_d3d12_shared_open.py` executes the production creator's description
and serialization block, copies both wire buffers to deliberately unaligned
storage, and invokes the actual decoder. It covers varied widths/formats,
converted storage, legacy metadata, short buffers, invalid formats, storage
mismatch, undersized storage, CPU-visible allocations and reserved/version
corruption. A second compiled regression executes `TextureBytes` and
`OpenHeapAndResource` for 37x29 and 4096x17 across all accepted formats. It
checks creator/open allocation accounting, allocation identity, own-heap
fallback and failed-import state, while preserving the smaller native extent.
The C validator is compiled as C and linked into the C++ fixture;
this also catches C/C++ linkage mistakes that syntax-only checks miss.

## WHAT IS STILL UNKNOWN

- The runtime's actual 0003/0043 open call sequence and preservation of the two
  private-data payloads on this driver have not been observed on hardware.
- Cross-device GPUVA mapping, coherence and retirement need C76A. Passing host
  tests or syntax checks is not evidence of cross-device data visibility.
- C76A uses two distinct devices in one process. Cross-process and D3D11/D3D12
  interop are subsequent checkpoints, not proven by C76A.
- Explicit shared heaps, broader texture layouts and shared buffers need a new
  layout-bearing protocol; arbitrary BO layouts cannot be inferred from traces.
- Legacy v3 shared textures are deliberately refused by D3D12 because their
  metadata cannot exclude converted display storage; D3D11 compatibility remains.
- The probe requires a runtime exposing `ID3D12DeviceFactory`. A factory failure
  is reported separately from resource creation/open failure.

## Checkpoint

Claude owns preregistration, exact package hashes, installation, evidence and
rollback. This task performs no hardware run, guest access or package build.

Run `AGX_D3D12_C76A` / `only C76A` through the probe's existing `run_isolated`
path. It uses the first device's adapter LUID and a separate device factory with
`DISALLOW_STORING_NEW_DEVICE_AS_SINGLETON`, omitting
`ALLOW_RETURNING_EXISTING_DEVICE` (SDK `d3d12.h:29727`). This avoids the process
singleton of repeated `D3D12CreateDevice` calls. It creates a shared committed
RGBA8 render-target texture and NT handle, opens on the second device, verifies
its description, then closes the NT handle. Two distinct upload patterns are
written on device A, transitioned to COMMON and CPU-fence-completed before
device B copies to readback. The creator resource is released before B's second
read. All 37x29 pixels must match both times; the non-power-of-two dimensions
exercise row padding without changing admission rules.

Expected: two different device pointers, create/share/open S_OK, two zero-mismatch
reads, `AGX_D3D12_C76A: PASS`, and clean resource/device teardown. Collect
`measure-d3d12-shared-import`, allocation/map/retirement receipts and any
`measure-d3d12-rejected` table/offset. A mismatch, refused open, unproven fence
completion or teardown failure rejects the checkpoint. On unproven completion
the probe exits instead of releasing possibly live GPU objects. Use the
existing preregistered recovery contract; no new recovery mechanism is proposed.
