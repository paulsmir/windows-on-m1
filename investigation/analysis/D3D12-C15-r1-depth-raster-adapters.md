# C15 R1 typed depth/stencil and rasterizer CPU adapters

These are two independent typed state projections; no CORE_0010 table or
shader-model capability is published by this checkpoint.

WINDOWS CONTRACT: pinned WDK26100 `d3d12umddi.h` SHA256
`d338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28`
defines `D3D12DDI_DEPTH_STENCIL_DESC_0010` and
`D3D12DDI_RASTERIZER_DESC_0010` as their older semantic fields followed by
`D3D12DDI_LIBRARY_REFERENCE_0010`. Calc and create have distinct typed PFNs;
the latter return VOID and must use the existing per-device error callback for
invalid input. The official [depth/stencil description](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/ns-d3d12umddi-d3d12ddi_depth_stencil_desc_0010)
and [0010 callback](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_createdepthstencilstate_0010)
confirm the typed form. A live library handle is validated by the linked
zero-cache registry; its PipelineIndex is runtime metadata, not a pointer.

AGX/ASAHI CONTRACT: current 0003 state objects store the older depth/stencil
and rasterizer records. PSO creation copies their semantic values, and the
existing native owner maps them to Gallium depth/stencil/rasterizer state.
Mesa Asahi assigns `create_depth_stencil_alpha_state`,
`bind_depth_stencil_alpha_state`, `create_rasterizer_state`, and
`bind_rasterizer_state` in `agx_state.c`. These new typed adapters operate only
on CPU metadata; m1n1, Mu, UAT, GPU queues, power and recovery are unaffected.

TRANSLATION: add separate unregistered 0010 size/create wrappers for each
state. Validate the readable record and live same-device LibraryReference;
project every old semantic field into a zero-initialized old descriptor, never
copying LibraryReference or source padding. Then call the existing 0003 state
create function with the current private size. Invalid VOID create reports a
real error and leaves destination bytes untouched. Depth/stencil must preserve
both face-operation groups and masks. Rasterizer must preserve all modes,
booleans, forced sample count and exact finite/infinite float bits already
accepted by current state behavior; no new normalization is introduced here.

WHAT IS STILL UNKNOWN: the complete R1 state/shader/PSO table, descriptor
selection/lifetime, DXIL allocator OOM and GPU execution remain separate.
There is no hardware discriminator for these unregistered CPU wrappers.
Use independent actual-source host tests (different valid fields, NULL/foreign
references, canaries) and pinned Windows ARM64 PFN signature/syntax checks.
Full source-bound UMD build may combine both independent adapters because each
has its own positive CPU receipt; it must not publish CORE_0010.

The actual DDI now contains fieldwise depth/stencil and rasterizer projections
with unregistered typed calc/create wrappers and four pinned-WDK PFN assertions.
The source renderer initially rejected their absence. Apple clang21 with
ASan/UBSan then returned `R1_DEPTH_RASTER_PASS checks=15 failures=0` on the
actual function bodies, including distinct face operations, raster float
values, valid/foreign library references, dead device, NULL record/destination
and destination canaries. This one fixture reports a combined result; the
individual field groups remain independently inspectable in the source.
EXP1636 staged exact DDI/header/script hashes and pinned WDK26100 ARM64
native-owner/NONE/winsys syntax returned rc0 for all three. Raw builder log
SHA256 `ddac7d6b56ae5ad6b0982f2191bba1037db051b79879fc66b6a51330c42730e1`.
Full production link and CORE_0010 publication remain separate.
