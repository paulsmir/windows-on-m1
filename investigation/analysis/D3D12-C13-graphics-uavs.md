# D3D12 C13: UAVs in graphics shaders

Status 2026-10-17. Compute binds UAVs since C5/C9; draws refused any UAV
visible to the VS or PS (ResolveRoot reason 4). Pixel-shader UAVs carry
order-independent transparency, GPU culling feedback, counters and
debug output in D3D12 renderers.

Sources inspected: WDK 26100 d3d12umddi.h (root UAV parameters and UAV
descriptor ranges with D3D12DDI_SHADER_VISIBILITY); dxbc_to_nir.h (u# is
shader-buffer / image slot # in every stage); fxc ps_5_0 (PS UAV
registers start after the bound render-target outputs, X4509: u1 with
SV_Target0); Mesa asahi agx_state.c (set_shader_buffers/set_shader_images
per stage, fragment stage included).

## WINDOWS CONTRACT

- Root UAVs and UAV table ranges are visible to the stages of their
  ShaderVisibility; D3D12 has no D3D11 OMSetRenderTargetsAndUnordered-
  AccessViews slot split -- the root signature binds u# directly.

## AGX/ASAHI CONTRACT

- set_shader_buffers(stage, 0, 8) / set_shader_images(stage, 0, 8) for
  MESA_SHADER_VERTEX and MESA_SHADER_FRAGMENT, as for compute.

## TRANSLATION

- ResolveRoot no longer refuses graphics UAVs; OwnerBindings' UAVs of the
  VS and PS records are copied into AGX_D3D12_DRAW.{Vs,Ps}.Uavs.
- bind_stage binds a stage's UAVs (untyped buffers u#, typed images u#)
  with its CBVs and SRVs; unbind_stage drops buffers and images after the
  draw or launch, so compute and graphics share one binding path.

## WHAT IS STILL UNKNOWN

- Hardware: fragment-stage atomics on an owner buffer (Asahi supports
  them; first use from the D3D12 owner).

## Checkpoint

Probe C13 (fresh device, mode 4 of the C6b draw): PS root UAV u1
(PIXEL) InterlockedAdd per pixel into a zeroed DEFAULT buffer; C13A PASS =
the C6b image checks and counter == drawn pixels.
