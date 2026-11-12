# PITCH-1: a 64-byte-aligned row pitch for every scan-out-capable allocation

Status: implemented offline on arch/pitch1 (cut from exp/np1 4e231cb19). No hardware run yet.
Classification: DISPLAY_ONLY for the scan-out rule; FULL GRAPHICS for the render-target layout that must agree with it.

Problem: the overlay check (Check3/Set3 rule table) and the scanout broker require a stride that is a multiple of
64 bytes; the allocator gave a displayable buffer `Width * 4` rounded up to 16. A 640-wide window (2560 B) fits by
accident; a 1682-wide window (6736 B, 6736 % 64 = 16) is refused with `AdmissionMpoReasonStride` and DWM
composites it on the GPU instead of giving it its own plane.

## WHAT REAL BUG OR INVARIANT WILL THE TESTS CATCH?

1. A scan-out-capable allocation whose pitch the overlay check or the broker refuses (the 1682 symptom above).
2. A pitch or size disagreement between the description that is created, the copy every opener validates, the
   Asahi linear import and the broker record (on hardware: a sheared or skewed picture).
3. Any change to the pitch or size of a non-displayable allocation (Win32 class buffers must stay
   `Width == Pitch == Size`; CPU-visible GDI surfaces report their pitch to GDI).

## WINDOWS CONTRACT

- `winsdk26100/shared/d3dkmdt.h:1380-1393` `D3DKMDT_GDISURFACE_TEXTURE = 1`, `STAGING_CPUVISIBLE = 2`,
  `STAGING = 3`, `EXISTINGSYSMEM = 5`.
- `winsdk26100/shared/d3dkmdt.h:1395-1403` `D3DKMDT_GDISURFACEDATA.Pitch` "out: The driver must return pitch if
  allocation is CPU visible". The driver chooses the pitch; GDI uses the returned value. Under this rule a CPU-visible
  GDI surface keeps exactly the old pitch (the rule applies only to `CpuVisible == 0`).
- `wdk26100/d3dkmddi.h:4113-4126` `DXGKARG_DESCRIBEALLOCATION` has no pitch field: DescribeAllocation reports Width,
  Height, Format, multisample, refresh, rotation only. Nothing to change there.
- `wdk26100/d3d10umddi.h:526-531` `D3D10DDI_MAPPED_SUBRESOURCE { pData; RowPitch; DepthPitch; }`: the driver
  returns the row pitch of a map; the application must use it (D3D never promises tight rows for a mapped texture).
  The presentation resources concerned are created `D3D10_DDI_USAGE_DEFAULT` with `MapFlags == 0`
  (`umd.c:176` refuses anything else) and the driver does not report MapOnDefaultTextures (no match in
  `mesa/`, `d3d12/`, `render-admission/`), so an application cannot map them at all; a CPU readback goes through
  a copy into a staging resource, whose own layout supplies RowPitch (Asahi `agx_pipe.c:1013`
  `transfer->base.stride = ail_get_linear_stride_B(...)`).
- D3D12: the buffer side of every texture copy uses an application-visible footprint with
  `RowPitch` a multiple of `D3D12_TEXTURE_DATA_PITCH_ALIGNMENT = 256` (`winsdk26100/um/d3d12.idl:415, 2030`); the
  texture's internal row pitch is never exposed. Swap-chain buffers are default-heap resources (not CPU mappable).
- GPU-only allocations have no application-visible pitch at all.

## AGX-ASAHI CONTRACT

Scan-out side (the exact rule):

- m1n1 broker, layer record (ABI V3): `.local/m1n1-wt-s2-broker-v3/src/hv_agx_scanout_broker.c:137-138`
  `if ((uint64_t)raw->stride < (uint64_t)raw->width * 4 || (raw->stride & 63)) return ..._LAYER_STRIDE;`
  and `:139-140` `raw->surface_size != (uint64_t)raw->stride * raw->height` -> `_LAYER_GEOMETRY`;
  `:141` surface offset aligned to `HV_AGX_SCANOUT_ALIGNMENT` = 64 KiB (`hv_agx_scanout_broker.h:49`).
  Same rule for the scaled single-surface path, `:68-70`.
- m1n1 DCP swap builders: `src/dcp_iomfb_present.c:125-127` (one slot) and `:188-190` (two slots)
  `(stride & 63)` refused; `buffer_size = height * stride`.
- Asahi DCP driver: `asahi-linux-dcp/drivers/gpu/drm/apple/plane.c:172-180` "Pitches have to be 64-byte aligned
  for linear images" (`if (fb->pitches[i] & 63) return -EINVAL;`); its dumb buffers are created that way,
  `apple_drv.c:61-62` `args->pitch = ALIGN(DIV_ROUND_UP(args->width * args->bpp, 8), 64)`.

Render side:

- Asahi layout: `mesa/src/asahi/layout/layout.c:14-21` an explicit `linear_stride_B` must be a multiple of 16
  (Asahi's own default is `ALIGN_POT(stride, AIL_CACHELINE)` = 128, `layout.h:18`); `:24-28` layer size =
  `align64(stride * height, 128)`. Asahi Mesa imports a linear buffer with the exporter's stride
  (`gallium/drivers/asahi/agx_pipe.c:183, 213`); our import does the same with the description's pitch
  (`mesa/winsys/agx_win32_asahi_scene.c:122-160` `AgxWin32AsahiImportLinearColor32`: `pitch % 16 == 0`,
  `bytes >= layout.size_B`, `ail_get_linear_stride_B == pitch`).
- A 64-byte multiple satisfies both (64 is a multiple of 16), and `pitch * height` padded to 128 is what the UMD
  already allocates (`umd.c:220` and `windows/icd/agx_wgl_icd.cpp:849`).

## Every site that computes or assumes the pitch (before this change)

| Site | Role | Result |
|---|---|---|
| `render-admission/src/render_allocation.c:35-69` `AdmissionAllocationDescribe` | THE pitch computation (`(rowBytes + 15) & ~15`) | changed: uses the new helper |
| `render_allocation.c:71-85` `AdmissionAllocationDescriptionValid` | every opener / KMD validator; `Pitch & 15` | changed: class alignment |
| `umd/src/umd.c:140-226` `AdmissionUmdDescribePrimary` (D3D11 create, and D3D12 swap-chain create via `AgxD3d12OwnerCreatePresentationTexture`, `mesa/winsys/agx_d3d12_windows.cpp:3084-3175`) | create; Size padded to 128 | inherits the helper |
| `umd/src/umd.c:1033-1120` `AdmissionUmdOpenResource` | open: copies the creator's description, validates | inherits (Valid) |
| `d3d12/agx_d3d12_shared_open.h:17-36` `AgxD3d12DecodeShared`; `agx_d3d12_ddi.cpp:2770-2810`; `agx_d3d12_windows.cpp:3225-3245` | D3D12 open of a shared surface | inherits (Valid, decoded pitch) |
| `windows/icd/agx_wgl_icd.cpp:844-875` (GL swap surface create), `:935-985` (redirection surface open) | OpenGL ICD | inherits |
| `mesa/winsys/agx_d3d10_windows.cpp:376-405`, `agx_d3d12_windows.cpp:3166, 3242`, `agx_wgl_icd.cpp:873, 976` -> `agx_win32_asahi_scene.c:122-160` | Asahi linear render target, explicit stride = description pitch | unchanged code, agrees by construction |
| `render-admission/src/allocation_windows.c:276-323` GetStandardAllocationDriverData (shared primary, shadow, GDI surface; `surface->Pitch` out) | KMD standard allocations | inherits; GPU-only GDI textures now 64-aligned |
| `allocation_windows.c:475-484` `ScanoutCapable` | which allocations may be planes | now `AdmissionAllocationDisplayClass(...) &&` format |
| `allocation_windows.c:822-842` DescribeAllocation | no pitch in the DDI | none |
| `render-admission/src/render_win32_transport.c:62-65, 96-111` | KMD create validation | inherits (Valid); class buffers unchanged |
| `render-admission/src/mpo_windows.c:85-100` | plane input: `SurfaceStride = Pitch`, `SurfaceBytes = Pitch * Height` | unchanged; C_ASSERT ties the constants |
| `render-admission/include/mpo_contract.h:47`, `src/mpo_contract.c:79-81, 97-99, 104-106` | Check3/Set3 stride, size, row rules | unchanged (64 = allocator constant) |
| `render-admission/src/mpo_layers.c:23-35` | broker record Stride / SurfaceSize | unchanged |
| `render-admission/src/render_present.c:150-185`, `gpuva_g3_windows.c:6141-6145`, `shared/src/apple_agx_present_job.c:39-41, 180-181` | KMD present BLT / GPU present job | use each description's pitch; unchanged |
| `umd/src/direct_flip_contract.c:19-24`, `shared/include/apple_agx_fixed_panel_modes.h:43-52`, `shared/src/apple_agx_fixed_panel.c:120` | full-screen mode geometry `Pitch == Width * 4` | unchanged: every offered mode width is a multiple of 16 px, so `Width * 4` is already the 64-aligned pitch |
| `mesa/winsys/agx_win32_asahi_batch.c:147-152` | draw receipt pitch from the AIL layout | unchanged |
| Not displayable, unchanged: `mesa/winsys/agx_win32_pipe_screen.c:103-147` (legacy CPU-visible BGRA pipe resources), `umd/src/umd_win32_screen.c:956, 1246` (byte buffers, bpp 1), `shared/src/apple_agx_gdi.c:132-160` (no production caller), `render_backend_image.c:397`, `render_dynamic_output.c:191` (diagnostic tiled/legacy views) | | |

## TRANSLATION (the rule)

- Display class = `Type == D3DKMDT_GDISURFACE_TEXTURE && BytesPerPixel == 4 && CpuVisible == 0`
  (`AdmissionAllocationDisplayClass`). It is the context-free part of the KMD `ScanoutCapable` predicate, which now
  calls it, so every allocation that can become a plane is in the class. It covers every presentation buffer:
  D3D11/D3D12 swap-chain and shared BGRA/RGBA surfaces (`AdmissionUmdDescribePrimary`), the GL ICD surfaces, the
  KMD shared primary and GPU-only GDI textures (DWM redirection surfaces).
- Display class pitch = `Width * 4` rounded up to 64 (`ADMISSION_ALLOCATION_DISPLAY_PITCH_ALIGNMENT`); everything
  else keeps 16 (`ADMISSION_ALLOCATION_PITCH_ALIGNMENT`). One helper, `AdmissionAllocationPitch`, used by
  `AdmissionAllocationDescribe`; `AdmissionAllocationDescriptionValid` requires the class alignment, so a
  display-class description with a 16-only pitch can no longer reach the overlay check.
- Why not every linear render target: Mesa's own linear resources are laid out by AIL (128-aligned by default),
  not by this allocator; the remaining allocator users are byte buffers (`Width == Pitch == Size` is a transport
  invariant) and CPU-visible surfaces whose pitch GDI already consumes. None of them can be a plane
  (`ScanoutCapable` is 0), so changing them buys nothing and risks their contracts.
- Why the application cannot observe it: the class is GPU-only (`CpuVisible == 0`), created DEFAULT/MapFlags 0 and not
  mappable (no MapOnDefaultTextures); D3D12 never exposes a texture's internal pitch; every CPU readback goes through
  a staging copy whose RowPitch is its own layout's; `D3D10DDI_MAPPED_SUBRESOURCE.RowPitch` is driver-defined
  anyway. The cost is at most 60 bytes per row (memory), no visible change.
- Agreement: create writes the description once; every opener validates the creator's copy; the Asahi import
  takes the stride from it; the KMD plane input and the broker record take the stride from it. No site recomputes.
  Offered full-screen modes keep `Pitch == Width * 4` because each width is a multiple of 16 px.
- Size: unchanged formula (`Pitch * Height` padded to 128 by the UMD); the broker record carries
  `Pitch * Height` (mpo_windows.c), which the padded size always covers.

## Sites changed

- `drivers/apple-agx/render-admission/include/render_allocation.h`: constants, `AdmissionAllocationDisplayClass`,
  `AdmissionAllocationPitchAlignment`, `AdmissionAllocationPitch`.
- `drivers/apple-agx/render-admission/src/render_allocation.c`: the helper; Describe and DescriptionValid use it.
- `drivers/apple-agx/render-admission/src/allocation_windows.c`: `ScanoutCapable` uses the class predicate;
  `C_ASSERT(D3DKMDT_GDISURFACE_TEXTURE == ADMISSION_ALLOCATION_TYPE_GDI_TEXTURE)`.
- `drivers/apple-agx/render-admission/src/mpo_windows.c`:
  `C_ASSERT(ADMISSION_MPO_STRIDE_ALIGNMENT == ADMISSION_ALLOCATION_DISPLAY_PITCH_ALIGNMENT)`.
- Tests: new `tests/test_render_display_pitch.py` + `render-admission/tests/display_pitch_test.c`;
  `tests/test_d3d12_shared_open.py` (expected pitch now the helper's); `tests/test_umd_wp1_window_primary.py`
  (window primaries get the 64-aligned pitch); `tests/test_apple_agx_render_standard_allocation.py` (a GPU-only
  17x2 GDI texture now has pitch 128); test shims that defined `D3DKMDT_GDISURFACE_TEXTURE` as 2 now use the WDK
  value 1 (`test_umd_displayable_resource.py`, `test_umd_primary_backbuffer.py`, `test_umd_wp1_window_primary.py`,
  `test_d3d12_shared_open.py`, `test_apple_agx_standard_rgba_backbuffer.py`, `test_g4_segment_placement_replay.py`);
  the shared KMD shim in `test_g4_segment_placement_replay.py` gains `AdmissionAllocationDisplayClass`;
  `render-admission/tests/render_present_test.c` describes its SameAllocation 4x2 BLT fixture with one
  (CPU-visible) description on both sides instead of a GPU-only destination that now has a 64-byte pitch.
- Verification: RED with the pre-change allocator, `display_pitch_test.c` reports e.g.
  `width 1682 height 840 pitch 6736: reason 14` (Check3 Stride); GREEN after. Full gate unchanged
  (1240 tests, only the known NEW `test_render_irq_completion_step1...aslpp_define`). The suites outside the gate
  that compile the allocator fail exactly as on HEAD (pre-existing rot: output_expectation_lifetime,
  render_present_blt exp505, render_standard_allocation, standard_rgba_backbuffer, g4_live_*, g4_r105_open,
  r145, rc106).

## WHAT IS STILL UNKNOWN

- The Windows binaries were not built (no builder access): MSVC /W4 /WX of the two new C_ASSERTs
  (enum == unsigned constant, same form as the existing `allocation_windows.c` STAGING_CPUVISIBLE assert) and of
  the new `render_allocation.c` code is unverified; the host build uses -Wconversion/-Wsign-conversion as a proxy.

- Hardware: that DCP shows a plane whose stride is larger than `width * 4` without skew (the broker and Asahi say it
  must; never exercised on J313 because every surface so far had a tight stride). First checkpoint below.
- Whether 128 (AIL's own "optimal" linear stride, `layout.c:18`) renders measurably faster than 64 for linear render
  targets. Not measured; 64 is the minimum every consumer requires.
- (Resolved by PITCH-2 below: the former 16 MiB open cap.)

## First hardware checkpoint (proposal)

A windowed flip-model application 1682x840 (EXP2356/2357 geometry; pitch 6736 -> 6784). Expected: the MPO receipt
shows Check3 supported (no `AdmissionMpoReasonStride`, reason 14), Set3 sends two records with
`Stride = 6784`, `SurfaceSize = 6784 * 840`, and the broker applied receipt has no refusal. Look at the picture:
a skewed / sheared image means the render side and the scan-out side disagree on the stride; black or stale means
the record was refused. Recovery: the previous package (pitch 16) is behaviourally identical except for refusing
the plane.

## PITCH-2: one size limit for create and every open

Before: create admitted 1..8192 per axis (`umd.c:164-165`) with no size cap; the D3D11 open
(`umd.c:1097-1100`) and the D3D12 decode (`agx_d3d12_shared_open.h:29`) refused Size > 16 MiB; the GL ICD open had
no limit. With the 64-byte pitch a surface like 513x8000 (2112 x 8000 B) was created but refused at open.

Sources of the true limit:
- Kernel: CreateAllocation / OpenAllocation accept a plain `ADMISSION_ALLOCATION_DESCRIPTION` after
  `AdmissionAllocationDescriptionValid` and `AdmissionAllocationAlign64K` only
  (`render_win32_transport.c:62-65`, `allocation_windows.c:430-436, 930-960`); no size cap.
- Render side: a presentation allocation is usable only once adopted, and adoption refuses
  `Bytes > General MaximumBytes` (`umd_win32_screen.c:1331`). The KMD reports that limit
  (`lifecycle.c:741` = `AdmissionGeneralMaximumBytes`, `allocation_windows.c:81-92`: the G3 pool size, or
  `ADMISSION_WIN32_ALLOCATION_LEGACY_MAX_BYTES` = 16 MiB in the legacy build, `render_win32_transport.h:95` -- the
  origin of the old 0x1000000 literal). The UMD has it from QueryAdapterInfo (`umd_runtime_device.c:400-406`).
- Allocator: the largest create is 8192 x 8192 at the display pitch 32768, padded to 128 = 0x10000000 bytes.

Rule (`render_allocation.c AdmissionPresentationSizeCheck`): extent 1..8192 per axis, else reason EXTENT;
Size <= min(0x10000000, General MaximumBytes), else reason BYTES. Called with the adapter's MaximumBytes
(`umd_win32_screen.c AdmissionUmdPresentationClassMaximumBytes`, 0 when unknown -> every size refused) by:
D3D11/D3D12 create (`umd.c AdmissionUmdCreateResourceWithMetadata`), D3D11 open (`umd.c AdmissionUmdOpenResource`,
also the D3D12 shared open, which calls it), GL ICD create and redirection-surface open (`agx_wgl_icd.cpp`
`wgl_kmt_create`, `wgl_redirect_open`). The pure D3D12 decoder (`AgxD3d12DecodeShared`, used before a device is at
hand) applies the allocator bound; the class bound follows in `AdmissionUmdOpenResource`.
Test: `display_pitch_test.c test_create_and_open_share_the_size_limit` (every width 1..8192 against 18 edge heights,
both orientations, three class limits: open reason == create reason; above the extent both EXTENT; above the class
limit both BYTES); `test_render_display_pitch.py` checks every create/open body calls the check and carries no
separate cap.
