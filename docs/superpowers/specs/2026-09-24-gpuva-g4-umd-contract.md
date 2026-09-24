# G4: Mesa winsys on WDDM GPU virtual addresses

Status: offline UMD contract. Hardware execution belongs to the integration thread.

## Sources inspected

- `/Users/pavel/public_windows/.local/research/GPUVA_PLAN.md` §1 and G4;
  `docs/superpowers/specs/2026-09-24-gpuva-g3-contract.md`;
  `investigation/GPU_CURRENT_STATE.md` (EXP782 boundary).
- Mesa reference `src/asahi/lib/agx_bo.c`, `agx_device.c` and
  `src/gallium/drivers/asahi/agx_batch.c`: a BO has a stable VM address,
  VM_BIND maps it, and submit passes native compute/render command structures.
  These are behavioral references; no external code is copied.
- Current `mesa/winsys/agx_win32_asahi_bo.c`, `agx_win32_asahi_batch.c`,
  `agx_win32_screen.c`, `render-admission/umd/src/{umd_runtime_device,
  umd_draw_composer,umd_win32_screen}.c`, and KMD
  `render-admission/src/gpuva_g3_windows.c`.
- Pinned WDK 26100 `d3dukmdt.h` at `.local/tandem/wdk26100-d3dukmdt.h`;
  Microsoft Learn [GpuMmu model](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpummu-model),
  [reserve](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ns-d3dukmdt-d3dddi_reservegpuvirtualaddress),
  [map](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_mapgpuvirtualaddresscb),
  [residency](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_makeresidentcb),
  [submit](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/ns-d3dumddi-_d3dddicb_submitcommand),
  [GPUVA update](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_updategpuvirtualaddresscb).

## WINDOWS CONTRACT

VidMm owns per-process page tables, physical allocation and residency. The UMD
assigns GPUVA. `AllocateCb` returns an allocation handle, not an address.
`ReserveGpuVirtualAddressCb` reserves a 64-KiB-aligned and sized interval;
its `hPagingQueue` alias is obsolete in WDK 26100. `MapGpuVirtualAddressCb`
maps an allocation in 4-KiB logical page units and returns a GPUVA and a
possibly pending paging fence. An explicit reservation followed by an exact
base map keeps the returned address within the selected 39-bit AGX range.
The UMD waits for every nonzero map fence before GPU use.

The UMD calls `MakeResidentCb` on the command BO and every BO referenced by
the native batch, waits for `PagingFenceValue` on the paging queue's monitored
fence before submit, and balances successful residency with `EvictCb` after
completion. `SubmitCommandCb` names GPUVA command bytes, command length,
context and private KMD data; it carries no general allocation list.
Displayable written allocations must go in `WrittenPrimaries` when applicable.
Command completion uses a monitored fence associated with the rendering
context. `UpdateGpuVirtualAddressCb` is for tile-resource remapping with a
rendering monitored fence interlock; ordinary BO creation uses reserve/map.

## AGX-ASAHI CONTRACT

`agx_bo.va->addr` is the mapped hardware GPUVA, never a construction address.
Native encoder, shader and resource address fields are emitted once with that
VA. Submit preserves the original Asahi render/compute command structures and
the encoded BO bytes. No capture graph, relocation, materializer or patch list
runs in the VA profile. The physical profile remains a separate build choice.

## TRANSLATION AND OWNERSHIP

| Operation | UMD/winsys | VidMm/runtime | KMD/EL2 |
|---|---|---|---|
| BO create | Allocate, reserve, map, validate AGX range/alignment | Own allocation and GPUVA/page tables | Validate AGX 16-KiB leaf mapping; EL2 publishes UAT |
| Submit | Collect referenced allocation handles, make resident, wait paging fence, submit native VA command, wait completion, evict | Schedule paging and command, report monitored fences | Validate KMD ABI, process root and ranges; execute TA/3D and signal only after completion |
| Release | Wait outstanding uses, free GPUVA before allocation destruction | Retire mapping and allocation | Drain jobs/TLB and release process slot |
| Failure | Fail closed and retain uncertain mappings/resources until reset | Report HRESULT and fence state | Poison uncertain process; recovery remains integration-thread owned |

Mu exposes APPL0002 and resources. m1n1 owns inherited AGX state, UAT slot
publication, TLB and recovery. This G4 branch changes neither layer nor KMD G3.
DMA backing and interrupts remain owned by the integration stack. The last
hardware-validated recovery is ordinary GPU-visible Code28, as recorded in
`GPU_CURRENT_STATE.md`; this branch performs no hardware run.

## WHAT IS STILL UNKNOWN / KMD CONTRACT REQUEST

1. Current `AdmissionDdiSubmitCommandVirtual` insists on physical Render's
   `ADMISSION_GDI_DMA_PRIVATE_SIZE` shadow and recorded render packet. The main
   thread must define a versioned VA private command ABI for native Asahi
   compute/render metadata, command BO GPUVA/length, context/process identity,
   primary writes and completion fence. It must reject the old format in VA
   mode. The UMD cannot safely send raw Asahi metadata until this is defined.
   The proposed UMD v1 private header in `agx_win32_gpuva_batch.c` is 24 bytes:
   little-endian `Magic=0x34584741` at offset 0, `Version=1` at 4,
   `HeaderBytes=24` at 6, `CommandBytes` at 8, reserved zero at 12 and
   `CommandVa` at 16. Exactly `CommandBytes` unmodified Asahi bytes follow:
   fragment attachment header/records, then render header/payload. Those
   bytes are also copied verbatim into the mapped command BO at `CommandVa`.
   The KMD must bind `CommandVa` to the `SubmitCommandVirtual` DMA GPUVA,
   validate the length and each native VA/range in the process page table,
   and reject malformed or unsupported native command types before firmware
   access. Private data is a CPU validation copy, not a relocation table.
2. KMD must confirm a 39-bit range, low USC-address limit, exact executable
   protection, permitted BO classes, and 16-KiB AGX leaf grouping under the
   selected 64-KiB VidMm segment page profile. Never round an invalid mapping.
3. `CreateContextVirtual` output, rendering monitored fence handle/address,
   private data size, and `SubmitCommandCb` to `SubmitCommandVirtual` translation
   need a pinned WDK 26100 compile test and a host callback replay.
4. Eviction/free ordering with in-flight references, map fence `E_PENDING`,
   and failure after a pending mapping need an exact lifecycle contract.
5. First hardware checkpoint, owned by the integration thread only: one
   minimal draw has one stable BO VA, completed map/residency fences, native
   command bytes unchanged, `SubmitCommandVirtual` admission and physical AGX
   completion. On any failure, use the existing exact-package Code28 recovery.

## Offline implementation checkpoint

`APPLE_AGX_GPUVA_WINSYS=1` selects a separate BO and batch path in the
projected native runtime. BOs receive 64-KiB reservations and map the exact
VidMm allocation in 4-KiB DDI pages; `agx_bo.va->addr` holds the returned VA.
Native render command bytes are written directly into a mapped command BO.
The active reference set is made resident and its paging fence awaited before
`SubmitCommandCb`; a rendering monitored fence gates eviction and VA free.
No capture graph or relocation pass is invoked in this profile. The existing
physical profile is still the default. Direct CDM/compute-only submission is
currently fail closed until the KMD accepts a native compute command contract.

The host core and emulated WDK callback draw pass, the x64 native projection
and archive build, and the ARM64 native archive and UMD DLL link complete in
`C:\Users\pauls\AD04-g4-mesa-va\mesa-build-g4`. These are offline gates;
there is no G4 Air execution. The repository-wide 1042-test run returned
17 failures and 108 errors; examples include missing `m1n1_windows` source
files and local toolchain prerequisites. That submodule was pre-existing dirty
and is outside the authorized G4 scope. Focused GPUVA and physical UMD tests
pass. The ARM64 DLL SHA-256 is
`59422363304f4728fa89f21a70128920f91a3a27411016d7686e8de5eea422da`.
