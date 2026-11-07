# D3D12 C80: presentation-surface readback without the Direct-shadow round trip

The pipeline session asked for this on 2026-10-23, as task (c) after
EXP1899N.

## Observation (EXP1899N user-umd.log, pid 2960)

- `measure-d3d12-presentation 00000057 018f01ff 40000a00`: a B8G8R8A8 511x399
  presentation surface. Its KernelAllocation is 0x40000a00 and its Direct
  slot is token 0x12. The size is 0xc7800 = pitch 2048 x 399.
- Each ExecuteCommandLists replays a `CopyTextureRegion` from that surface
  into a buffer: `measure-d3d12-texture-copy 00000001 018f01ff 00000800`.
  This is ToBuffer, the full 511x399 at pitch 0x800, 902 copies in 902
  executes.
- Every other copy is preceded by
  `measure-xfer 00000001 00000012 000c7800 ... 0000000c 40000a00`, the
  GPU->CPU download of the whole surface.
- The next submission emits
  `measure-xfer 00000000 ... 0000000e 40000a00`, the shadow upload. The CPU
  only read the surface, but the upload still hashes it.
- 1319 transfers in total. The ticks between upload and download are
  58k-77k QPC per pair (hypothesis: 24 MHz QPC, so 2.4-3.2 ms per frame pair).

## Cause (current source)

The source is a layering problem:

1. `AgxD3d12OwnerCopyTexture` performs every texture<->buffer copy by
   `pipe->texture_map` of the texture plus `buffer_map`.
2. The presentation surface is `AgxWin32AsahiImportLinearColor32`: linear,
   `PIPE_BIND_SHARED`, a borrowed GPU-local allocation (Direct slot, no
   CPU-visible allocation, R158/EXP871).
3. A CPU map of it reaches `AdmissionUmdScreenMapBuffer` ->
   `AdmissionUmdGpuvaPrepareDirectMap` (EXP1059). That function downloads the
   whole surface into the private shadow through 13 copy escapes of 64 KiB.
4. Mesa keeps the BO mapped. At the next submission, `transfer_held` uploads
   the shadow and drops the map:
   - `staging_unchanged` returns 0 for a borrowed mapped slot, so the shadow
     is hashed;
   - the chunk hashes match, so no bytes move;
   - the following map downloads again.

## WINDOWS CONTRACT

`CopyTextureRegion` from a texture into a placed footprint of a buffer is a
GPU copy in queue order. Nothing requires the CPU to touch the source.

## AGX/ASAHI CONTRACT

- `agx_resource_copy_region` copies texture->texture of the same target and
  format with `agx_blit` on the GPU (`try_copy_via_blit`). Only unsupported
  cases fall back to `util_resource_copy_region` (CPU maps).
- `PIPE_USAGE_STAGING` single-level 2D textures are linear
  (`agx_select_best_modifier`) with writeback BOs.
- `texture_map(PIPE_MAP_READ)` syncs the writer batch.

## TRANSLATION

When `ToBuffer`, `Depth == 1`, the source is a 2D texture and the source has
`PIPE_BIND_SHARED` (only the imported presentation surface has this bind in
the owner), the copy runs as follows:

1. Mesa copies the region with `resource_copy_region` into the owner's cached
   linear `DownloadStaging` (RT|SV, at least 256x256, recreated when the
   format changes or the region does not fit). This is a GPU blit.
2. The CPU maps the staging for READ. It is a small owner buffer, so a
   SystemDirect CPU-visible allocation: no copy escape.
3. The CPU copies the rows into the buffer map, as the map path did.
4. The owner releases the staging before the context is destroyed.

Other textures keep the map path. The surface is no longer CPU-mapped in this
path, so the UMD Direct shadow downloads and uploads disappear for it.

## Tests (RED -> GREEN)

- `tests/d3d12_volume_copy_fixture.cpp` with `test_d3d12_volume.VolumeCopy`
  executes the real `AgxD3d12OwnerCopyTexture` and `staged_download`. The
  case is a 5x3 readback of an 8x4 shared BGRA surface at (2, 1), run twice
  (the staging is reused), then the full surface (the staging is recreated).
  - The test requires zero CPU maps of the shared surface, one GPU copy per
    readback, exact rows, and untouched poison around them.
  - RED before the fix: `shared_maps=1 staging_copies=0`.
- `test_d3d12_native_owner`: DownloadStaging is released before
  `AgxWin32AsahiContextDestroy`.

## WHAT IS STILL UNKNOWN

- Whether `util_blitter_is_blit_supported` accepts the BGRA8 shared -> BGRA8
  staging blit on the device. If it does not, Asahi falls back to the CPU
  copy, which is correct but as slow as before.
- Checkpoint: rerun the EXP1899N workload. Expect:
  - zero `measure-xfer ... 40000a00` (token of the surface) during the
    readback loop;
  - one `measure-d3d12-staged-download hr=0` per copy;
  - an unchanged window image (the screenshot compare of the EXP1899 run);
  - frame pacing no worse.
