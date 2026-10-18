# C23 limited committed Texture1D translation

## WINDOWS CONTRACT

RENDER_ONLY resource/query/copy/view work inside the existing full-graphics
0003 opt-in development profile. Pinned WDK26100 supplies Texture1D resource,
Tex1D SRV/UAV and resolved allocation/subresource records. Public resource and
layout rules are in Microsoft D3D12_RESOURCE_DESC and D3D12_TEXTURE_LAYOUT.
Public ROW_MAJOR is not used for this path. No capability advertisement changes.

The implemented domain is committed DEFAULT, nonarray, single-sample Texture1D,
R32_UINT or RGBA8_UNORM, width1..16384 including NPOT, with any legal requested
mip count. Arrays, RTV/DSV, placed/shared/cross-adapter and CPU Map remain refused.
This limited checkpoint does not establish complete Texture1D or FL11 conformance.
Optional CheckFormatSupport flags do not suppress mandatory baseline requirements.

## AGX/ASAHI CONTRACT

Primary implementation inspection: Mesa agx_pipe.c resource creation/modifiers
and format support; agx_state.c image/texture descriptors; layout.c/layout.h
AIL miptree/size/sparse estimate; agx_nir_lower_texture.c one-dimensional texture
and image lowering; existing project D3D11 Texture1D translation. For height1,
all requested AIL mip tiles are1x1. Queried offsets and allocation size retain
native padding. Potential sparse-table extent is metadata, not active storage.
Writable image legalization is permitted to replace backing; the single-layer
mapping requires preserved requested offsets and rounded allocation extent.
Query comparison verifies this CPU condition; actual GPU transition is unproven.

## TRANSLATION

The owner builds one PIPE_TEXTURE_1D template for pure sizing and actual creation.
The complete native query, not a hand-built reduced result, supplies offsets,
stride and size. Query remains screen/context/BO-free. Creation uses the queried
modifier and existing owner resource/backing lifecycle.

DDI resolves UNKNOWN to the existing internal0x105 4BPP1x1 pattern and transition
0xff. Allocation/reflection/subresource callbacks preserve the same result.
Physical mip intervals are aligned, disjoint and in allocation bounds. Existing
checked copy machinery represents each 1D mip with height/depth1 and real runtime
buffer pitches; it preserves command initialization and negative range refusal.

SRV views preserve component mapping and selected mip range. Owned SRV dimension
metadata distinguishes 1D from other targets. New owned eight-slot UAV metadata
is collected from actual image/SSBO instructions before NIR consumption for
compute, VS and PS. Binding rejects 1D/non-1D mismatches in both directions and
skips unused physical1D slots. Existing non-1D policy is unchanged. No extra
per-binding stack arrays or explicit PIPE_BIND_SHADER_IMAGE are introduced.

## Verification and provenance

Original query regression reproduced refusal of all65534 width/one-or-full-chain
cases despite successful actual native queries. Candidate passed those65534
cases and458782 requested mips, including packed WDK pattern decoding, actual
AIL tile bytes/NPOT padding, writable offsets/rounded extent and zero query BO/
device calls. Actual create/reflection and12 negative cases passed. Actual views,
cross-dimension binding, unused1D null binding, copy origins/pitches and10 copy
refusals passed. Earlier fixture compiler failures remain recorded as failures.

Two original HLSL fixtures were compiled with pinned SDK26100 FXC. Actual DXBC
passed the current translator, dimension metadata checks, NIR validation and
Asahi preprocessing/validation. Load/GetDimensions uses one coordinate/component;
UAV fixture proves typed image load/store and the raw-output fixture remains OTHER.
Source bytecode was unchanged. Durable five-test module reads CURRENT production
functions and uses actual native query/AIL; it has no private candidate dependency.
All5 durable tests passed in7.931s. Adapted legacy25 tests passed in3.343s, retaining
old oracles. The full pass ran1575 tests in204.713s:119 known baseline signatures
plus9 fixture errors caused by the new extraction closure/call argument; targeted
adaptation closed those9. The full suite is not reported as GREEN.

Three real Windows syntax profiles returned0. The57-step ARM64 probe built at
254464bytes, SHA256ccf5127118f9c41958851183e415005122cf805226df39c6461bd4c48d4a3d55.
Existing54 steps are retained; C23A/B/C use separate fresh devices for copies,
SRV Load/GetDimensions and selected-mip typed UAV load/store, with NPOT37/mips2,
exact readback and untouched mip/padding poison. No probe was executed on hardware.

Immutable input a8751697210230093b22695fcd3e03817b0b72ad is exactly EXP1424 source
507812fb plus six production files, probe and two FXC headers. It was frozen only
as a build input and has not been built or launched as a driver package. Query
GREEN manifestc20da0f0696713ef9d28ca80397d45a3409a38adbf25fedf6a49589ce9d6c701;
NIR1b87013858ace956306b9380d571737853963f9a29b9e6514c6f3bc8b3e69858;
view/copy2a908f9ff8525b48906e60ec2d8fe45cc7753a8520ccd1ac10369c979537098b;
Windows94-file manifestde3dc7526e21c8581bcf1d288f89d1e13c2aac71839d02151c096f20e32917e3.
Detailed BEFORE/ACTUAL and original stopped attempts are in EXPERIMENTS.md.

## WHAT IS STILL UNKNOWN

EXP1424 proves successful volume DDI query but public allocation rejection after
callback. Exact saved runtime arithmetic identifies zero additional-data alignment
as its cause. This device-dependent-layout contract also gates C23. Hardware C23
moves to EXP1427 after a separate alignment-only EXP1426 verdict; the old C23
preparation labelled1426 remains unlaunched. No register, power, DMA, KMD, m1n1,
Mu, recovery, default-enable, R1 or SM6 change is part of C23.

The smallest hardware checkpoint is the57-step probe with C23A/B/C exact data and
pixel-independent oracles, after public allocation admission is proven. Root
must freeze a new source/package/probe, record BEFORE, verify signing/stack/hashes,
then collect all regressions and remove the exact experiment package through
normal GPU-visible ordered recovery. No package carryover or hidden normal boot.
