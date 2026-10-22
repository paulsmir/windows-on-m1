# D3D12 feature levels 11_1 and 12_0

Status: design, 2026-10-22. Origin: EXP1881G. GravityMark 1.89 refuses the
adapter on D3D12 and on D3D11 ("can't create 0 adapter"). The D3D12 runtime
closed the adapter right after `D3D12DDICAPS_TYPE_0081_3DPIPELINESUPPORT1`
reported `kProfileLevel` = 11_0. Night Raid and 3DMark accept 11_0;
GravityMark, and newer games, need more.

## WINDOWS CONTRACT

Source: Microsoft, "Hardware Feature Levels" (Direct3D 12).

| Requirement | 11_0 | 11_1 | 12_0 | This driver now |
|---|---|---|---|---|
| Shader model | 5.1/6.0 | 5.1/6.0 | 6.0 | SM 6.0-6.5 (C44-C52) |
| Resource binding tier | 1 | 1 | 2 | Tier 2 (C47) |
| Tiled resources | optional | optional | Tier 2 | not supported |
| Min/max filters | no | optional | yes (with Tier 2) | no |
| Typed UAV loads | 3 formats | 3 formats | 18 formats | the set (C49) |
| Logic ops | optional | yes | yes | C57 (pending EXP1882) |
| Target-independent rasterization | no | yes | yes | no |
| MRT with ForcedSampleCount 1 | optional | yes | yes | not checked |
| UAV-only rendering, max forced sample count | 8 | 16 | 16 | ignored (see gap) |

How the level is chosen:
- The runtime reads `MaximumDriverSupportedFeatureLevel` from the 3D pipeline
  caps.
- It refuses `D3D12CreateDevice` above that level.
- The D3D11 runtime makes the same decision before it opens the UMD.

## AGX/ASAHI CONTRACT

Sources: Mesa Asahi (`gallium/drivers/asahi`, `asahi/vulkan` Honeykrisp).

- **Logic ops.** `pipe_blend_state.logicop_enable` and `logicop_func` reach
  the AGX blend key (`agx_state.c`). They are lowered in the fragment
  epilogue.
- **Sample counts.** 1, 2 and 4 only (`hk_physical_device.c`
  `sample_counts`), including `framebufferNoAttachmentsSampleCounts`. There is
  no 8x or 16x rasterization.
- **Min/max filtering.** No hardware reduction. Honeykrisp reports
  `samplerFilterMinmax` only through the `fake_minmax` driconf workaround.
- **Sparse.** Honeykrisp has `sparseBinding`, `sparseResidencyBuffer`,
  `sparseResidencyImage2D`, `sparseResidencyAliased`,
  `sparseResidencyStandard2DBlockShape` and `sparseResidencyNonResidentStrict`.
  It has no 3D or multisampled sparse. The implementation is in `hk_image.c`
  (sparse layout), `hk_queue.c` (binds) and `hk_device_memory.c`. The page
  size is 16 KiB; a D3D tile is 64 KiB, that is 4 pages.
- **Native sparse textures** (Mesa `docs/drivers/asahi.rst`, "Sparse page tables"):
  - A texture or PBE descriptor in sparse mode points at a sparse page table,
    not at the image.
  - The table is an array of folios. Each folio is 256 4-byte "Sparse Block"
    entries mapping image pages to GPU VAs, plus 256 counters: 2 KiB per
    4 MiB. There is one folio set per layer.
  - It maps pages to pages and inherits the image tiling.
  - `ail_layout.sparse_table_size_B` and `sparse_folios_per_layer` give the
    sizes; `ail_page_to_sparse_index_el` gives the index.
  - Texture tile mappings are therefore plain memory writes into a per-resource
    table, from the CPU or a compute kernel. They need no UAT or GPUVA change
    per tile.
  - Reserved *buffers* still need GPUVA remapping: Honeykrisp uses VM binds.
    Here that means the WDDM GPUVA callbacks the UMD already uses
    (`umd_gpuva_windows.c`: Reserve/Map), plus a zero page for unmapped
    ranges.

## TRANSLATION / plan (one checkpoint per step)

1. **C57 logic ops (done).**
   - The PSO's logic op becomes Gallium's single logic op. A PSO whose render
     targets disagree is refused.
   - Probe C57A.
2. **Forced sample count.** This is already a gap at 11_0: UAV-only rendering
   must honour ForcedSampleCount up to 8, and 11_1 needs 16. AGX rasterizes
   at most 4 samples.
   - Exact emulation: rasterize the UAV-only draw at 1 sample, conservatively
     covered. The pixel shader evaluates the triangle's edge functions at the
     D3D standard 8/16 sample positions, which gives `SV_Coverage` and the
     covered-sample count.
   - Needs: a lowering that passes the three clip-space vertices, through a
     geometry stage or per-primitive data.
   - Design first, probe against D3D's standard sample positions.
3. **Target-independent rasterization (11_1).** ForcedSampleCount 1 with
   1-sample render targets is the identity case. Higher counts with RTVs use
   the same coverage emulation as step 2; there is no depth/stencil in TIR.
4. **Tiled resources Tier 1 then Tier 2 (12_0).**
   - Reserved resources: GPUVA without backing, `UpdateTileMappings` and
     `CopyTileMappings`, all through the WDDM GPUVA update callbacks the KMD
     already serves for committed and placed resources.
   - Textures: a reserved texture is a sparse-mode descriptor over a
     per-resource sparse page table (ail sizes). UpdateTileMappings and
     CopyTileMappings write Sparse Block entries for the 4 pages of each
     64 KiB tile: CPU writes for queue-time updates, ordered on the queue.
   - Texture tiles use the standard 2D block shape (Honeykrisp
     `sparseResidencyStandard2DBlockShape`).
   - Buffers: GPUVA ranges remapped through the WDDM GPUVA callbacks;
     unmapped ranges point at a zero page.
   - Tier 2: reads of unmapped tiles return 0, and
     `CheckAccessFullyMapped` / residency feedback through `sparse_residency`
     NIR.
   - Min/max filters emulated in the shader: gather the bilinear footprint,
     take the min/max per mip level, then trilinear blend.
5. **Raise `kProfileLevel`**: to 11_1 after steps 1-3 are validated, and to
   12_0 after step 4. Each with a GravityMark run as the end-to-end check.

## WHAT IS STILL UNKNOWN

- The exact level GravityMark requests: 11_1 or 12_0. A shim probe that
  calls `D3D12CreateDevice` at each level with the GravityMark adapter
  enumeration settles it without guessing.
- Whether the D3D11 path, our D3D10/11 UMD, needs its own 11_1 work.
  D3D11.1 logic ops are refused today: `reject-wddm2-logicop` in
  `agx_wddm2_ddi.cpp`.
- How much of Honeykrisp's sparse image layout maps onto the WDDM GPUVA tile
  callbacks without UAT changes in the KMD.
