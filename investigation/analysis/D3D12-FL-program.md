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
   - Honeykrisp's split (hk_image.c, hk_queue.c), the model to follow:
     - A sparse image reserves a 64 KiB-aligned VA range bound to scratch
       pages, so writes (PBE, image stores, lowered atomics) to unmapped tiles
       are discarded.
     - Reads go through the texture unit's sparse map, a separate zeroed BO of
       `sparse_table_size_B`. Unmapped tiles therefore read 0 and report
       non-resident (`Image Mode` Sparse; `Sparse block` {Address shr 14,
       Enabled bit 31}, genxml cmdbuf.xml).
     - A bind updates both: VA -> memory, and the sparse-map entries for its
       pages.
     - For D3D12: UpdateTileMappings maps the tile's 4 pages through the
       GPUVA map callback (unmapped tiles: one shared scratch allocation
       mapped at each page) and writes the 4 Sparse Block entries.
   - Tier 2: reads of unmapped tiles return 0, and
     `CheckAccessFullyMapped` / residency feedback through `sparse_residency`
     NIR.
   - Min/max filters emulated in the shader: gather the bilinear footprint,
     take the min/max per mip level, then trilinear blend.
5. **Raise `kProfileLevel`**: to 11_1 after steps 1-3 are validated, and to
   12_0 after step 4. Each with a GravityMark run as the end-to-end check.

## WHAT IS STILL UNKNOWN

- ~~The exact level GravityMark requests~~ RESOLVED 2026-10-22 (static
  disassembly of GravityMark 1.89 `bin/Tellusim_arm64.dll`, llvm-objdump):
  - D3D12: `D3D12CreateDevice(adapter, 0xc100 = D3D_FEATURE_LEVEL_12_1, ...)`
    at 0x180ee863c and 0x180eec2d4 (the "can't create %u adapter" path).
  - D3D11: `D3D11CreateDevice` with one level, 0xb100 = 11_1 (0x180f2b724).
  - So GravityMark D3D12 needs 12_1 = 12_0 + ROVs + conservative
    rasterization Tier 1; GravityMark D3D11 needs only 11_1.
  - AGX: ROVs map onto the hardware pixel ordering (Asahi compiler lowers
    `nir_intrinsic_begin_invocation_interlock` to `wait_pix`); no hardware
    conservative rasterization (Honeykrisp exposes none), so CR Tier 1 needs
    the same geometry expansion + exact in-shader coverage as TIR 8x/16x.
- D3D11 11_1 gaps (agx_d3d11_ddi.cpp / agx_wddm2_ddi.cpp): logic ops
  (`reject-wddm2-logicop`), 64 UAV slots and UAVs in every stage
  (`AGX_D3D11_UAV_SLOTS` = `DXBC_NIR_MAX_UAVS` = 8; Gallium has 32 shader
  buffers, 64 images), ForcedSampleCount > 1 (`reject-wddm2-rasterizer`).
- Whether the D3D11 path, our D3D10/11 UMD, needs its own 11_1 work.
  D3D11.1 logic ops are refused today: `reject-wddm2-logicop` in
  `agx_wddm2_ddi.cpp`.
- How much of Honeykrisp's sparse image layout maps onto the WDDM GPUVA tile
  callbacks without UAT changes in the KMD.

## RESOLVED 2026-10-22: the levels GravityMark requests

Source: static disassembly of GravityMark 1.89 `bin/Tellusim_arm64.dll`
(guest `C:\Program Files\GravityMark\bin`, llvm-objdump; no execution).
- D3D12: both calls to `D3D12CreateDevice` (0x180ee863c, 0x180eec2d4) pass
  `mov w1, #0xc100`, that is `D3D_FEATURE_LEVEL_12_1`. A failure logs
  "D3D12::getAdapter(): can't create %u adapter", the message of EXP1881G.
  `ID3D12Device6` is also required, which the runtime provides.
- D3D11: `D3D11CreateDevice` (0x180f2b760) receives a one-element level array
  {0xb100}, that is `D3D_FEATURE_LEVEL_11_1` only.

12_1 is 12_0 plus rasterizer-ordered views and conservative rasterization
Tier 1. The AGX facts for those two (sources: Asahi compiler, Honeykrisp):
- **ROV.** `agx_compile.c` lowers `nir_intrinsic_begin_invocation_interlock`
  to `wait_pix 0xC`. This is the hardware pixel ordering that blending
  already uses. ROV loads and stores become an interlocked critical section;
  no hardware gap.
- **Conservative rasterization.** No AGX hardware support (Honeykrisp
  `extendedDynamicState3ConservativeRasterizationMode = false`, no
  `VK_EXT_conservative_rasterization`). The emulation is shared with step 2
  (ForcedSampleCount/TIR):
  - Expand each triangle in clip space. Each new vertex is an affine
    combination of the original three, applied to position and to every
    varying, so the perspective-correct attribute planes are unchanged.
  - Rasterize the expanded triangle at 1x.
  - In the pixel shader, evaluate the original edge functions, passed as
    flat data, at the D3D standard sample positions. This gives exact 8x/16x
    coverage, `SV_Coverage`, `SV_InnerCoverage` and the discard.
- The D3D standard 16x positions are 1/16-pixel units in [-8, 7]. These are
  the units of Honeykrisp's `sampleLocationSubPixelBits = 4` and range
  [0, 0.9375]: the same grid.

Order (shortest route to a GravityMark run first):
1. **D3D11 FL 11_1** (GravityMark D3D11), in the D3D10/11 UMD:
   - logic ops (`reject-wddm2-logicop`), same mapping as C57;
   - 64 UAV slots and UAVs at every stage: today 8 (`DXBC_NIR_MAX_UAVS`,
     `AGX_D3D11_UAV_SLOTS`). Raw views as shader buffers plus counters
     exceed `PIPE_MAX_SHADER_BUFFERS` 32, so 64 slots need raw UAVs as
     R32 buffer images (`PIPE_MAX_SHADER_IMAGES` 64) or the C47 heap path;
   - ForcedSampleCount up to 16 and TIR (`reject-wddm2-rasterizer`), with
     the coverage emulation above;
   - then the caps: `3DPIPELINELEVEL_11_1` and
     `D3D11_OPTIONS.OutputMergerLogicOp`.
2. **D3D12 12_0**: C53 tiled resources Tier 1, then Tier 2.
3. **D3D12 12_1**: ROV through interlock, then conservative rasterization
   Tier 1 with the shared emulation.
