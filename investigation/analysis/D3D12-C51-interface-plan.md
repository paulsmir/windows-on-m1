# D3D12 C51+: interface plan for SM 6.2 to 6.5 (from 0033)

Offline header analysis of WDK 26100 `d3d12umddi.h` (cited as H). Items marked
[HYP] are hypotheses that a hardware run must confirm.

## Shader model and interface each needs

| SM | release constant | value |
|---|---|---|
| 6.2 | `_0042` | 0x60025 |
| 6.3 | `_0054` | 0x60035 |
| 6.4 | `_0062` | 0x60045 |
| 6.5 | `_0071` | 0x60055 |

Tables:

| target | DEVICE_CORE | COMMAND_LIST_3D |
|---|---|---|
| 0042 | CORE_0040 (96) | 3D_0040 (58) |
| 0071 | CORE_0070 (116) | 3D_0062 (67) |

- The queue table stays CORE_0001.
- CORE_0040 and CORE_0043 have the same size, and so do CORE_0062 and CORE_0063. Select the table by the negotiated version, as C50 does.
- ADAPTERFUNCS is unchanged up to `_0109`.

## Step A: 0042 (SM 6.2), about 11 entries

These are the only behavioural changes in the range:

**CORE_0040 positions 30-33 become the command pool** (H 6633-6641).
- Create is `(HDEVICE, {PoolFlags}*, HCOMMANDPOOL_0040)`.
- Destroy and Reset are both `(HDEVICE, HCOMMANDPOOL_0040)`.
- Type and QueueFlags are gone. The driver never reads the allocator type, and its Reset is a no-op.

**CORE positions 37-38 are command-list creation `_0040`** (H 6615-6625).
- The arguments drop hDrvCommandList, hRTCommandList and hDrvCommandAllocator.
- hDrvCommandList and hRTCommandList become parameters.
- Project onto `_0001` and call CreateCommandList.

**CORE positions 93-96 are the command recorder `_0040`** (H 6649-6658).
- The driver records into per-list heap buffers, so all four are no-ops.

**3D_0040 position 2 is ResetCommandList_0040** (H 6538-6545).
- Its argument is {hDrvCommandRecorder, ID, CommandListFlags}.
- ResetCommandList ignores its argument.

Plumbing and shader models:
- Add SM 6.2 to the shader-model list.
- fp32 denorm mode is core in 6.2. AGX flushes fp32 denormals (Honeykrisp: `shaderDenormPreserveFloat32 = false`), so a `preserve` shader runs as FTZ. [HYP: dxil-spirv's float-controls execution modes reach spirv_to_nir harmlessly; needs an offline fixture.]
- Native 16-bit ops (SHADER_CAPS_0042 Native16BitOps) stay optional: FALSE.

## Step B: 0071 (SM 6.3, 6.4 and 6.5 together), about 35 more entries

Real adapters:
- Queue_0050: refuse a non-null SchedulingGroup.
- Present_0051: contexts output. [HYP: element 0 replaces hSrc/hDst.]

CORE stubs:
- 3 scheduling group entries.
- 6 meta-command entries. Enumerate returns 0.
- 9 state-object and raytracing entries. CheckDriverMatchingIdentifier returns UNSUPPORTED_TYPE.
- SetBackgroundProcessingMode_0063 writes FALSE.
- AddToStateObject is rejected.
- 5 of these take a non-HDEVICE first argument, so they need plain stubs rather than AGX_REJECT.

List stubs:
- 2 meta-command entries and 5 raytracing entries are rejected.
- RSSetShadingRate and RSSetShadingRateImage accept only their defaults.

Caps:
- Cap 1067 (hardware scheduling) answers 0.
- Every new OPTIONS field stays 0: RenderPassTier, RaytracingTier, VRS, BackgroundProcessing. MeshShaderTier and SamplerFeedbackTier only appear at 0073.
- Render passes are an extended-feature table. Nothing is listed in GetOptionalDDITables, so they are not offered. [HYP: the runtime emulates RenderPassTier 0.]

Core intrinsics that need offline fixtures before step B:
- SM 6.4: dot2add, dot4add_i8packed and dot4add_u8packed. dxil-spirv lowers these without options.
- SM 6.5: WaveMatch, WaveMultiPrefix{Sum,Product,BitCount}. dxil-spirv falls back to ballot/broadcast helpers when NV partitioned subgroups are off.

## Open hardware questions

- Order of the runtime's pool and recorder calls.
- Whether cap 1067 is queried, and whether EnumerateMetaCommands runs during device creation.
- Whether RSSetShadingRate is called on list reset.
