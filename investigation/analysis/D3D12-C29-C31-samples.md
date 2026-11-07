# D3D12 C29-C31: what the EXP1825 Microsoft samples need

EXP1825 ran 11 DirectX-Graphics-Samples (68907e31) on package 30.0.1824.0.
Six passed on ARM64 and x64. The other five hit three independent gaps.

## C29: BC1-BC7 2D textures (Bundles, DynamicIndexing, Multithreading)

WINDOWS CONTRACT: the D3D12 runtime asks CheckResourceAllocationInfo for a
committed BC texture. GetCopyableFootprints gives a footprint per subresource:
Width and Height in texels, aligned up to the 4x4 block, so a 2x2 mip has a
4x4 footprint. RowPitch counts one row of blocks. CopyTextureRegion
coordinates and boxes are in texels and are block-aligned.

AGX/ASAHI CONTRACT: ail lays BC1-BC7 (S3TC, RGTC, BPTC) out twiddled and
never linear (agx_pipe.c rejects compressed linear). The AGX samples them
natively (layout/formats.c). The transfer map of a twiddled, uncompressed
level detiles whole blocks: stride = blocks across the box x block bytes
(util_format_get_stride, ail_detile in tiling.cc). The native runtime
patches Format.cpp, so FormatTranslate maps BC6H and BC7 to BPTC.

TRANSLATION:
- The owner creates sampler-only BC textures (render-target and depth binds
  are refused).
- CopyTexture copies ceil(H/4) block rows of ceil(W/4) x bytes. It clamps
  the map box to the level extent, so the 4x4 footprint of a small mip maps
  only the mip's own texels.
- The DDI sizes allocations in blocks and computes buffer offsets as
  (y/4) x pitch + (x/4) x bytes.
- Texture-to-texture copies of BC formats stay refused, because the owner
  region copy still requires 1x1 blocks.

WHAT IS STILL UNKNOWN: nothing for upload and sampling; hardware verifies
the decode (probe C29A).

## C30: bundles (HelloBundles, Bundles)

WINDOWS CONTRACT: a bundle inherits the caller's render targets, viewports,
scissors, blend factor, stencil reference and root arguments. Whatever the
bundle sets (PSO, topology, root signature, root arguments, IA buffers)
stays set in the caller after ExecuteBundle. Only the bundle-legal list
DDIs are called on a bundle. The runtime uses one command-list DDI table
for every list type.

AGX/ASAHI CONTRACT: none. A bundle is a recording construct, and execution
stays the owner's ordinary draw replay.

TRANSLATION: on a bundle, each bundle-legal DDI appends {replay function,
arguments by value} to a per-list log; the arrays behind pointer arguments
are copied too. ExecuteBundle replays the log on the calling list's handle
through the same DDIs, which makes inheritance and state leak-back exact.
The state a bundle inherits (RTV, viewport, scissor, ClearRootArguments) is
never logged.

WHAT IS STILL UNKNOWN: whether the runtime sends default-state calls to a
new bundle (for example a NULL root signature) that would reset inherited
root arguments when replayed. Probe C30A inherits root constants and a CBV
from the caller.

## C31: root-argument ExecuteIndirect (ExecuteIndirect sample)

WINDOWS CONTRACT: a command signature is an ordered list of arguments,
tightly packed. Root constants take 4 bytes per DWORD; CBV, SRV and UAV
views take 8 bytes (GPU VA); vertex and index buffer views take 16 bytes;
these come before exactly one final DRAW (16), DRAW_INDEXED (20) or
DISPATCH (12). The sample uses {CBV, DRAW} with a stride of 0x18. The
argument and count buffers may be written by earlier GPU work in the same
list (the sample's culling compute shader).

AGX/ASAHI CONTRACT: a Gallium indirect draw reads only the draw arguments,
so no root binding can change per record. A buffer_map with PIPE_MAP_READ
flushes and waits for the batches writing the resource
(agx_prepare_for_map).

TRANSLATION:
- A single-argument signature keeps the Gallium GPU-indirect path (C14).
- For a multi-argument signature, ExecuteIndirect snapshots the list state.
- At execution, the owner reads the count and the records through
  synchronised maps. For each record, the arguments are applied to a
  scratch copy of the state through the ordinary setters. One draw or
  dispatch is then recorded on the scratch list and replayed immediately.
- The scratch list reports no runtime list errors. A refused record is
  skipped and counted in the receipt `measure-d3d12-indirect-expand`.

WHAT IS STILL UNKNOWN: cost per record on hardware (the sample issues 1024
draws per frame).

Checkpoint (EXP1826): probe steps C29A, C30A and C31A PASS on ARM64 and
x64; Bundles, HelloBundles, DynamicIndexing, Multithreading and
ExecuteIndirect reach frames; the six EXP1825 passes and the D3D11 smokes
are unchanged.
