# D3D12 register spaces and slot limits: source checkpoint

Status: offline source review while EXP1378 runs. No binding implementation or
hardware candidate yet. Follow CODEX_HANDOFF_D3D12.md after the C14 verdict.

## Sources inspected

- Pinned WDK26100 d3d12umddi.h: CREATE_PIPELINE_STATE_0001.hRootSignature;
  CREATE_SHADER_0003 and CREATE_COMPUTE_SHADER_0003 root-signature arguments.
- Microsoft Bytecode changes in SM5.1:
  https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/bytecode-changes-in-sm5-1
- Microsoft resource binding specification:
  https://microsoft.github.io/DirectX-Specs/d3d/ResourceBinding.html
- Current UMD BindTable, ResolveRoot, CompileCompute/CompileGraphics, PSO storage.
- Current dxbc_parse.c parse_operand/parse_instruction and dxbc_to_nir.c
  declaration/constant-buffer/resource/sampler indexing.
- Existing ps51.hlsl/dxbc/asm fixture: CB0 space3, T0[0:7] space1,
  S0[0:1] space2; dynamic sampling and load. Parser test already covers it;
  translator tests explicitly require clean refusal until implemented.
- Asahi agx_pipe.c shader caps: max_const_buffers16, sampler-view/buffer/image
  caps from PIPE_MAX_*; existing owner arrays CBV14, SRV16, UAV8, sampler16.

## WINDOWS CONTRACT

Resource identity is (type, register space, shader register), with per-stage
visibility. SM5.1 declarations carry a range ID and register bounds; resource
operands refer to that ID and a register index. Range ID is not the physical
binding slot. The root signature maps namespaces to root descriptors/constants
or descriptor-table offsets. The selected hardware slot layout must agree
between shader compilation and command replay.

## AGX/ASAHI CONTRACT

Gallium/NIR bindings are per-stage slots. Current DXBC lowering expects SM5.0
physical register indices. Asahi consumes those lowered bindings and has no
D3D12 register-space namespace. No m1n1, Mu, ACPI, interrupt, DMA, power, or
recovery changes follow from this namespace translation.

## TRANSLATION (proposed; implementation decision after C14)

Use one per-stage binding map shared by shader lowering and UMD root resolution.
Keep type/space/register range, assign deterministic dense slot ranges bounded
by the documented Gallium limits, and preserve relative indexing within each
range. Match root entries using type/space/register, not range ID. Different
spaces must never alias because they share a numeric register. Preserve SM5.0
identity behavior for existing D3D11 and D3D12 callers. No external code needed.

A separate slot-capacity issue exists: the owner/UMD SRV arrays stop at 16 while
the translator has MAX_RESOURCES128. Expanding those arrays grows replay/recording frames; measure the stack gate
and use bounded heap storage only if required. ReplayDraw already uses a heap
scratch record; ReplayDispatch still has local owner bindings. Do not silently truncate finite ranges.
Do not combine this independently observable capacity change with the first
register-space hardware checkpoint.

## WHAT IS STILL UNKNOWN

Offline design work remains: finite versus unbounded range allocation, sparse
high registers, table ranges unused by a stage, null/unbound descriptors,
root constants, static samplers, and dynamic/nonuniform resource indexing.
These are deterministic source/specification questions; no diagnostic hardware
is justified to guess the mapping. Preserve fail-closed handling until coherent.

## Smallest later checkpoint and recovery

After offline invariants pass: a fresh-device SM5.1 compute shader with the same
register number in distinct spaces and distinct expected outputs, then graphics
stage visibility/sampler coverage. Package one completed mapping only. Reuse the
validated full-owner launch and exact experiment-local recovery. Register its
own BEFORE/ACTUAL and hashes; this note does not authorize an unimplemented cap.

## First implementation slice: SRV capacity

Microsoft Hardware Tiers (https://learn.microsoft.com/en-us/windows/win32/direct3d12/hardware-support)
requires 128 SRVs per stage at Tier1. Current driver advertises Tier1 but both
UMD kSrvSlots and owner AGX_D3D12_SRV_SLOTS are16; ResolveRoot rejects t16..t127.
Asahi advertises PIPE_MAX_SHADER_SAMPLER_VIEWS=128 and the DXBC translator has
MAX_RESOURCES=128. Raise only that shared SRV capacity to128, keep CBV14, UAV8,
sampler16 and spaces fail-closed. Verify the highest root SRV t127 and table SRV
t126 on the existing compute-SRV workload before adding namespace remapping.
This is one capacity correction; no caps or firmware change. Existing recording
and replay loops are parameterized by the shared count; stack measurements are
mandatory before hardware. The hardware unknown is only actual binding of high
slots on the owner. Compile a t126/t127 variant of the existing C5D shader and
retain C5D as control. No artificial failing test: reproduce the actual t127
rejection in the extracted root-bound predicate, checking last-valid127 and
first-invalid128 plus matched owner capacity.

DXBC baseline before any binding changes: test_dxbc_parse and test_dxbc_to_nir,
11 tests PASS in6.385s; corpus439 translated/13 refused/0 crashes (ps51 remains
intentionally refused). Log /tmp/agx-c16-dxbc-baseline.log.

## SRV capacity implementation plan

Goal: admit t0..t127 at the current Tier1 contract without changing register
spaces, CBV/UAV/sampler capacities, advertised caps, KMD or firmware.
Architecture: keep the current register-to-slot identity map, with one shared
128-slot boundary in UMD command recording/replay and native owner bindings.
Existing Gallium calls and DXBC frontend already support128. C++/C/WDK26100.
Execution: inline using the established agx-d3d12-loop; no delegation needed.

- [x] Add compiled-root-bound regression in tests/test_d3d12_native_owner.py:
      extract the actual SRV bound and refusal predicate; t16/t126/t127 must
      be admitted and t128 refused; shared owner limit must match. Run RED.
- [x] Change kSrvSlots in d3d12/agx_d3d12_ddi.cpp and AGX_D3D12_SRV_SLOTS
      in mesa/winsys/agx_d3d12_windows.h to128. Keep other limits. Run GREEN.
- [x] Retain C5D; add fresh-device C16A probe using root t127, table t126,
      root u7 and root CBV b13, with 16384 exact output words. Compile the
      shader with fxc cs_5_0, build ARM64 probe. No SM5.1 code yet.
- [x] Run affected tests and builder syncheck3x rc0; full host suite once
      before commit, recording known unrelated failures without claiming GREEN.
      Read tandem review, commit implementation, append CHANGES with its hash.
- [ ] Preregister EXP1379 (one driver factor SRV capacity), preserve exact
      recovery; build/sign/hash package and require both stack gates<=16384.
      If capacity growth exceeds a frame, move only that scratch storage to
      the process heap before committing/building the final candidate.
- [ ] Run hardware to C16A and existing D3D12 controls, collect regression
      receipts, recover/remove exact package, record ACTUAL and verdict.

Ownership: UMD and owner initialize/replay slot records, Asahi owns NIR/GPU
bindings, KMD owns residency/interrupts/recovery. Firmware and power are
unchanged. Known EXP1361/1378 global scene-pool capacity failure remains a
separate KMD issue and must be reported if it recurs, not hidden in this fix.

C16A shader generated with fxc cs_5_0: root t127/table t126, root u7, root CBV b13 with bias7; output5*i+207 for16384 words. DDI and owner syntax3x rc0, probe ARM64 SHA256 98a330e1a36fcc65802be6c8059cab4e04d69f09c49e9355bc7b7da02bbdd58c. Targeted32testsPASS. Asahi set_sampler_views starts0 and loops the count, texture upload emits/nulls all indices through nr_bindful_textures; no sampler-limit16 assumption is used for sampler views.

Full host suite1499 tests in165.841s:68failures/51errors/2skips, identical failure signatures to the pre-capacity1498-test baseline (new failures0, removed0). Relevant32tests PASS; syntax3x rc0, ARM64 probe BUILD_PASS. Logs .local/experiments/EXP1379-verification. Tandem historical OPEN items remain DEFER at the unrelated G3/firmware boundary as recorded in the C14 note; R85/R47 hash/profile gates and R48 single-launcher lock remain ACCEPT.
