# D3D12 C50: interface 0033 (R3 build 3) and shader model 6.1

## Sources inspected

- WDK 26100 `d3d12umddi.h`: `D3D12DDI_SUPPORTED_0033` (6254),
  `D3D12DDI_SHADER_MODEL_6_1_RELEASE_0033` (3484), `D3D12DDI_DEVICE_FUNCS_CORE_0033`
  (6401-6521), `D3D12DDI_COMMAND_LIST_FUNCS_3D_0033` (6303-6368), and every
  retyped argument (`CREATE_SHADER_0026`, `DEPTH_STENCIL_DESC_0025`,
  `CREATE_PIPELINE_STATE_0033`, `CREATE_ROOT_SIGNATURE_0013`, `CREATECOMMANDQUEUE_0023`,
  `CREATEHEAPANDRESOURCE_0030`, `RESOURCE_ALLOCATION_INFO_0022`,
  `RESOURCE_BARRIER_0022`, `PRESENT_0028`, `DEBUG_KMT_ALLOCATION_INFO_0014`).
- DirectX-Specs, View Instancing: tier 0 drivers that support SM 6.1 must still run
  SV_ViewID shaders in PSOs declaring 0 or 1 view instance, and SV_ViewID can
  then be replaced by 0.
- dxil-spirv `opcodes/converter_impl.hpp:872` ("If not [multiview], ViewID will just
  return constant 0").
- Asahi Gallium: no `load_view_index` handler (only Honeykrisp lowers it).
- Receipts from EXP1866 `tool-d3d12-on-umd.log`: `d3d12-versions` -> `d3d12-device-size`
  (Interface 0x000c000a, Version 0x00020000) -> caps -> `d3d12-create-device` ->
  `d3d12-fill-table`.

## WINDOWS CONTRACT

- SM 6.1 is offered only through `D3D12DDICAPS_TYPE_0011_SHADER_MODELS` by a driver
  that negotiated 0033.
- CORE_0033 has CORE_0012's 92 members in the same order. 23 of them are retyped.
  3D_0033 is 3D_0003 with ResourceBarrier and Present retyped and seven members
  appended. The command-queue table stays CORE_0001.
- CORE_0012 and CORE_0033 have the same size, so the table cannot be chosen by
  size. CalcPrivateDeviceSize receives the negotiated Interface/Version before
  FillDDITable runs.
- OPTIONS_0033 adds ViewInstancingTier and BarycentricsSupported. The zero-filled
  answer keeps both at "not supported". The same holds for DepthBoundsTestSupported,
  ProgrammableSamplePositionsTier, CopyQueueTimestampQueriesSupported and
  WriteBufferImmediateQueueFlags.

## AGX/ASAHI CONTRACT

None changes. Every entry projects onto the validated R1/0003 path.

## TRANSLATION

| Changed DDI | Projection |
|---|---|
| shaders `_0026` | `_0010`: the union is copied; ShaderCodeHash is unused because no shader-cache session is offered |
| stream output `_0026` | `_0010`, copied field by field because the embedded shader moved every field |
| depth-stencil `_0025` | `_0010` prefix; `DepthBoundsTestEnable` is refused |
| PSO `_0033` | `_0010` prefix. ViewInstanceCount 0, or 1 at location {0,0}; any flag is refused (masking is not offered). The runtime PSO handle is unused |
| root signature `_0013` | 1.1 is projected to a 1.0 copy in one heap block. The flags only relax 1.0 semantics, and the list is replayed at ExecuteCommandLists, so 1.0 behaviour keeps every 1.1 promise. Version 1.2 is refused |
| queue `_0023` | handles move from parameters into `_0001`; any QueueCreationFlags other than NONE is refused |
| heap and resource `_0030` | `RESOURCE_FLAG_0022_UNORDERED_ACCESS` is cleared, because the 0003 runtime never set it and the line/volume checks reject unknown flags. A non-null protected session is refused |
| allocation info `_0022` | alignments narrowed to 32 bits; every swizzle-transition slot repeats the single 0003 transition. The existing-info call no longer returns a KMT handle |
| debug allocation info `_0014` | no allocation is described |
| list | ResourceBarrier_0022 is a no-op, as before. Present_0028 maps to Present_0003 and sets no override. OMSetDepthBounds(0,1), SetSamplePositions(0,0) and SetProtectedResourceSession(NULL) are accepted and anything else is a list error. SetViewInstanceMask is a no-op, since masking PSOs are refused. AtomicCopyBufferRegion, ResourceResolveSubresourceRegion and WriteBufferImmediate are fail-closed |
| SV_ViewID | dxil-spirv emits constant 0 without multiview (`test_view_id_reads_view_zero`) |

Profile: `APPLE_AGX_D3D12_DDI=0033` (together with SM6) offers {0033, 0012, 0011,
0003}. The default stays 0012 until the hardware run validates it.

## WHAT IS STILL UNKNOWN

- Which caps types the 0033 runtime queries. In particular, whether it uses
  `0022_TEXTURE_LAYOUT` / `0022_SWIZZLE_PATTERN` (1060/1061, unanswered) in place
  of 1000/1001, which the C21 volume layouts answer.
- Whether the runtime calls any appended list DDI during list reset.
- Whether the runtime admits a "View Instancing" flagged shader at
  ViewInstancingTier NOT_SUPPORTED. WARP reports tier 1, so it does not answer this.

## Checkpoint

Run the probe e1868 with `APPLE_AGX_D3D12_DDI=0033` on ARM64 and x64.

- Pass: `d3d12-device-size` shows Interface R3 / Version 0x00030000, the device and
  list tables are filled, C50A PASSes (HighestShaderModel 0x61 and a green
  triangle), and everything else matches the default profile.
- Fail: device creation fails, any `measure-d3d12-rejected` entry or caps
  E_NOTIMPL appears that the default profile does not show, or C50A fails.
- Recovery: the default profile is unchanged (0012).

WARP (builder): C50A PASS, with HighestShaderModel 0x61 and centre 0,255,0.
