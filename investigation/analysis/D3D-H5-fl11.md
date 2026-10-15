# D3D-H5: feature level 11_0 (2026-10-15, requirement matrix)

Source: Microsoft Learn "Direct3D feature levels" (FL 11_0 column) and
"DXGI format support for Direct3D Feature Level 11.0 Hardware"; d3d10umddi.h
10.0.26100 (D3D11DDI_DEVICEFUNCS, D3D11DDICAPS_*); d3d11.h D3D11_REQ_*.

| FL 11_0 requirement | State | Where |
|---|---|---|
| Shader model 5.0, every stage | translator: VS/GS/PS/CS/HS/DS (classes, doubles refused: optional) | D3D-H3, D3D-H4 |
| Compute shader (8 UAV slots) | cs_4_x CONFIRMED on hardware (EXP1295); cs_5_0 in EXP1296 F/U | D3D-H2 H2b |
| UAVs at PS and CS (typed, raw, structured, counters) | implemented (c64731bd), hardware EXP1296 F/U | agx_d3d11_ddi.cpp |
| Hull and domain shaders | implemented (c8d90f67, ab2afa7b), hardware EXP1296 F/T | D3D-H4 |
| Draw/Dispatch indirect | implemented (a2f0ca46), hardware EXP1296 F/I | agx_d3d11_ddi.cpp |
| BC6H/BC7 | mapped to BPTC (19c01f7c), hardware EXP1296 F/B | Format.cpp projection |
| Textures 16384 (1D/2D/cube), arrays 2048, 3D 2048 | admitted (e9ed2303) | Resource.cpp projection |
| Max anisotropy 16, 32 input slots, 8 RTs | as 10_x (Asahi) | |
| Cubemap arrays, gather4, SM 4.1 (10_1) | translator: gather/lod/sampleinfo/samplepos; cube-array SRVs refused (srv_10) | to do with 10_1 |
| 4x MSAA on every RT format (10_1) | OPEN: interior pixels resolve to the clear colour (probe EXP1296 P) | D3D-G5 |
| 8x MSAA on most RT formats (11_0 format table) | absent on AGX (Apple GPUs sample at most 4x); reported as 0 quality levels | runtime enforcement measured by EXP1296 F (device creation) |
| Typed UAV loads (R32_FLOAT/UINT/SINT) | image loads through the translator | EXP1296 F/U writes only |
| Pipeline/SO statistics | OPEN: PSInvocations per SIMD group, SO statistics 0 (EXP1294) | EXP1296 F/U discriminates the atomic path |

Reporting: D3D11DDICAPS_3DPIPELINESUPPORT gains 10_1 and 11_0 only under
APPLE_AGX_D3D11_FL11 (per process) until EXP1296's cases and the MSAA
defect are resolved.  A default FL 11_0 needs: every F case PASS, MSAA 4x
correct on every render-target format, cube arrays, and a decision on 8x
(no hardware support: report none; WHQL would flag it, applications query
CheckMultisampleQualityLevels).

## EXP1298: FL 11_0 refusal

Boundary: EXP1298 observed OpenAdapter, GetCaps for THREADING (128), SHADER
(129), and 3DPIPELINESUPPORT (130), and two GetSupportedVersions calls, all
successful, followed by `D3D11CreateDevice` returning 0x887a0020 before a
CreateDevice entry. FL 10_0 creation through the same D3D11 interface works
(operator-provided EXP1298 receipt; see `investigation/EXPERIMENTS.md`,
EXP1298). The projected adapter advertises D3D11_0 last and returns the
FL 11_0 pipeline bit only under the switch
(`drivers/apple-agx/mesa/scripts/build-native-asahi-state.py:746-759,
780-823`).

Contract: `D3D10DDIARG_OPENADAPTER` has Interface and runtime Version, but no
separate WDDM version field (`/Volumes/pwdev/public_windows/.local/reference/wdk26100/d3d10umddi.h:7637-7651`).
`D3D10DDIARG_CALCPRIVATEDEVICESIZE` carries Interface, Version and Flags
(same header:7179-7184); CreateDevice carries Interface, Version and the
version-specific function-table union (same header:7126-7142). The header
defines D3D11_0, D3D11_1, WDDM1_3 and WDDM2_0 as distinct interfaces
(same header:7701-7706,7783-7808). Its adapter caps enum names the three
observed queries and later D3D11_1/WDDM caps (same header:7206-7252).
`d3dumddi.h` separately gates WDDM1_3 and WDDM2_0 UMD structures by
`D3D_UMD_INTERFACE_VERSION` (same reference directory:2887-2900,
3223-3235); this does not state an FL 11_0 minimum.

Microsoft says OpenAdapter10_2 supplies the adapter table, GetSupportedVersions
lists supported DDI versions, and CreateDevice completes initialization
([D3D11 DDI initialization](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/initializing-communication-with-the-direct3d-version-11-ddi)).
The runtime obtains threading and maximum pipeline level with GetCaps *before*
CreateDevice; it feeds the chosen pipeline level back in CreateDevice Flags
([threading and pipeline level](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/supporting-threading--command-lists--and-3-d-pipeline)).
These sources do not say that D3D11_1, WDDM1_3, or WDDM2_0 DDI is mandatory
for FL 11_0 on Windows 8 or later. They also do not identify any
D3D11_1DDICAPS_* or D3DWDDM*_DDICAPS_* query as mandatory for FL 11_0; the
observed run made none. The published DXGI error description says
0x887a0020 means a driver problem/device removed state, but does not map it
to a particular validation failure
([DXGI_ERROR](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-error)).

Ranked causes and smallest candidates (no caps/version decision yet):

1. **HYPOTHESIS — pipeline-level answer consistency.** The switch adds 10_1
   and 11_0 pipeline bits; the runtime asks for the pipeline cap before
   refusing, while FL 10_0 works. Inspect the projected cap value and
   GetSupportedVersions result against the WDK encoding and the EXP1298 raw
   receipts (`build-native-asahi-state.py:770-823`; header:7288-7309).
   Smallest candidate: correct one proven encoding/size mismatch, if found.
2. **HYPOTHESIS — DDI version selection.** Only D3D11_0 is advertised last;
   the runtime may reject an FL 11_0 negotiation on this OS despite the
   published documentation not requiring a newer DDI
   (`build-native-asahi-state.py:746-759`; header:7701-7706,7783-7808).
   Smallest candidate: compare the new OpenAdapter Interface/Version and
   CalcPrivateDeviceSize call/return between FL 10_0 and FL 11_0 first;
   consider an additional DDI only if that comparison proves the need.
3. **HYPOTHESIS — another runtime-side validation or adapter state.** No
   CreateDevice entry means resource, shader, and GPU execution paths have
   not been reached (EXP1298 receipt). The generic DXGI status cannot name
   the failing check. Smallest candidate: correlate the new receipts with
   runtime diagnostic events or a controlled same-adapter FL comparison.
