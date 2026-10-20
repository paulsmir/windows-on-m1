# C15 R1 typed blend descriptor projection — CPU-only gate

WINDOWS CONTRACT: the pinned WDK26100 `d3d12umddi.h` (SHA256
`d338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28`)
defines `D3D12DDI_BLEND_DESC_0010` as the existing two BOOLs and eight
`D3D12DDI_RENDER_TARGET_BLEND_DESC` records plus a
`D3D12DDI_LIBRARY_REFERENCE_0010`. The calc/create callback signatures take
the complete typed descriptor; they are not interchangeable with the 0003
callbacks. Microsoft's [CreateBlendState_0010 DDI](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_createblendstate_0010)
confirms the typed argument. The library reference has a handle and pipeline
index; the [pinned definition](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/ns-d3d12umddi-d3d12ddi_library_reference_0010)
does not license treating the index as a pointer or changing caller storage.
The prior exact-runtime R1 contract found full ordinary state descriptions on
valid load misses. The accepted zero-native-cache module validates live
same-device optional library handles without borrowing native state.

AGX/ASAHI CONTRACT: this adapter is CPU-only. The current 0003 state object
stores `D3D12DDI_BLEND_DESC` and the current PSO copies those semantic fields.
The owner later maps them to `pipe_blend_state` in
`agx_d3d12_windows.cpp`; Mesa Asahi assigns `create_blend_state`,
`bind_blend_state`, and `delete_blend_state` in `agx_state.c`.
`m1n1`, Mu, ACPI, AGX power, UAT, queues, interrupts and recovery do not own
this descriptor projection. No hardware state is read or changed here.

TRANSLATION: introduce unregistered R1 calc/create functions with exact
`PFND3D12DDI_*_0010` signatures. Validate the readable typed record and its
same-device library reference. Project only AlphaToCoverageEnable,
IndependentBlendEnable and all eight render-target records into a zeroed
legacy `D3D12DDI_BLEND_DESC`, then call the existing 0003 state constructor.
The private size remains the size of that legacy state; the library reference
is not copied into the persisted object. Do not publish CORE_0010, change
GetVersions/GetCaps, or alter the existing 0003 callbacks in this gate.

WHAT IS STILL UNKNOWN: no runtime/GPU uncertainty is needed to implement this
one field projection. This does not establish the full R1 table, other 0010
states/shaders/PSO, pipeline library roundtrip, OOM policy or SM6 execution.
The next deterministic checkpoint is an actual-source CPU fixture: compare
ordinary and valid-library projections byte-for-byte across all semantic
fields, vary the library reference while keeping the state equal, and reject
invalid/dead/foreign references without writing destination canaries. Native
WDK ARM64 compile then checks the exact calling convention and callback types.
No J313 experiment is justified for an unregistered wrapper.

## First CPU checkpoint

`agx_d3d12_r1_blend_projection.h` now contains only the pure field projection;
it does not call the library validator, report DDI errors or register a
callback. The fixture `tests/d3d12_r1_blend_projection_fixture.cpp` compiles
that actual header with a host type model. Its initial compile failed because
the projector did not exist; after implementation Apple clang21 with
ASan/UBSan returned `R1_BLEND_PROJECTION checks=16 failures=0`. The fixture
checks each of the ten render-target fields across eight distinct targets,
destination canaries, NULL refusal and equal semantic state under different
library metadata. This is not a Windows ABI claim: the next gate must compile
against the actual WDK ARM64 and wire typed calc/create wrappers to the
same-device zero-cache reference validator before any CORE_0010 table change.

EXP1621 staged the actual DDI SHA7c1ff5b and projector header SHAe41c4a58
with exact remote SHA verification, then ran the pinned WDK26100 ARM64
`syncheck-d3d12.ps1`: native-owner DDI, NONE DDI and winsys all returned
rc=0 without source diagnostics. This checks the actual Windows declarations
and translation-unit inclusion, not runtime typed callback registration or
hardware execution. Raw builder log SHA256
`ddac7d6b56ae5ad6b0982f2191bba1037db051b79879fc66b6a51330c42730e1`.
