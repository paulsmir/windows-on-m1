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
