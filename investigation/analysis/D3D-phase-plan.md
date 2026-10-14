# D3D 11/12 phase: inventory, prerequisites and architecture options (2026-10-14)

M2 (flexible memory) is complete (EXP1262-EXP1264). This document records where Direct3D stands and
how the phase is ordered. Inventory source: a read-only survey of the tree (file:line references in
the session record); contracts: Microsoft Learn (DDI pages cited below).

## Where D3D stands

**UMD:**
- Mesa d3d10umd, a Gallium frontend at 9aa1215f with hash-checked overlays, runs on the Asahi
  Gallium driver and the AGX compiler.
- It exports only `OpenAdapter10_2` and supports only the D3D10_0 DDI. The pipeline level is 10_0,
  so it is feature level 10_0.
- Shader path: SM4 only, DXBC -> TGSI -> NIR -> AGX.
- No D3D11 DDI, no compute shaders or UAVs, no tessellation.
- Geometry shaders: one stream-output pattern only.
- Queries: EVENT only.
- Formats: no MSAA, a narrow format set.
- Render targets: one colour attachment (no MRT); depth at most 4096^2 / 1 MiB.

**D3D12:** nothing exists (no `OpenAdapter12`, no d3d12umddi).

**Submission:**
- UMD context: `pfnCreateContextVirtualCb` with a monitored fence.
- Each Asahi batch becomes a G4 private envelope.
- `DxgkDdiSubmitCommandVirtual` turns it into firmware RunRender/RunCompute.

**KMD:**
- WDDM 3.2, one node, DMA-buffer preemption.
- No hardware queues or native fences.
- `SupportKernelModeCommandBuffer=0`.

**Shared below the API frontends:** the GL ICD (x86 GL 2.1, Mesa WGL) uses the same winsys, submit
path and KMD, and presents through a D3D11 device on this UMD.

## Prerequisites (independent of the D3D architecture)

1. **G4 recovery: a TDR must not bugcheck** (`G4-recovery.md`).
   - Today engine reset with an active packet restarts retained firmware and returns
     DEVICE_HARDWARE_ERROR, which is 0x116.
   - New D3D features will hang the GPU while they are developed, and every hang now costs a guest
     crash.
   - Translation (from `G4-recovery.md`):
     - firmware-preserving ResetEngine: resume a halted firmware, drop the active job, report it as
       LastAbortedFenceId;
     - KMD deadline expiry keeps polling below TdrDelay;
     - refuse submissions that reference unpublished BO ranges.
   - Hardware checkpoint: deterministic fault injection (a render reading an unpublished VA).
2. **Resubmission defect** (branch 7 subsite 0x201; EXP1197, EXP1260).
   - A resubmission after preemption is refused because the slot was cancelled, not preempted.
   - No TDR was logged in EXP1260; the only cancel paths without a TDR are internal runtime
     resets.
   - The cancel caller is now in the receipt (8861c4a2). It is resolved at the next occurrence and
     fixed in the owning path.
3. **Presentation:**
   - Redirected (blt) presentation stays open for windowed D3D.
   - DWM's DDisplay path needs FL 11_0 (EXP952). Today packages set `DisableDDisplay=1`.
   - Reaching FL 11_0 lets DWM run its default path. That is the D3D11 acceptance test.

## Architecture options for D3D 11/12 (decision record, to be confirmed with sources)

| | A: native D3D11 UMD on Mesa (extend d3d10umd) | B: native D3D12 UMD; D3D11/D3D9 through Microsoft's mapping layers | C: both natively (A for D3D11, B for D3D12) |
|---|---|---|---|
| D3D11 path | Enable the D3D11 DDI in the d3d10umd frontend | D3D11On12 (MIT, open source) | A |
| D3D12 path | none | New UMD (`OpenAdapter12`) on the Asahi/Honeykrisp internals | B |
| D3D9 path | separate | D3D9On12 (MIT, open source) | D3D9On12 |
| Shaders | SM5 DXBC -> NIR. Mesa has no SM5 parser, so either extend ShaderParse or use vkd3d-shader (LGPL; needs a licence review) | DXIL -> NIR through dxil-spirv (MIT) -> SPIR-V -> spirv_to_nir | both |
| Cost / benefit | Fastest D3D11; no D3D12 | One modern UMD, three APIs; D3D11 through a translation layer | Best end state, most work |

Every option needs the same groundwork: compute and UAVs inside render batches (a KMD contract
for compute work in a render submission, which the winsys refuses today), MRT in the KMD, MSAA,
queries and the FL 11_0 format set.

- The D3D11 path pays off first: DWM DDisplay, CS2, and the GL ICD's D3D11 presenter all depend
  on it.
- D3D12 needs hardware queues or at least monitored-fence semantics, residency (MakeResident and
  Evict), and the D3D12 DDI's command lists. Honeykrisp's Vulkan model maps onto these more
  directly than Gallium does.

**Recommendation:**
- Start with the groundwork and FL 11_0 through option A's DDI enablement, since D3D11 unlocks the
  most and reuses the existing frontend.
- Decide the D3D12 shape (B vs C) once FL 11_0 works and the compute contract exists.
- Confirm the mapping-layer options against Microsoft's D3D11On12/D3D9On12 documentation and
  licences before relying on them.

## Order

1. G4 recovery (prerequisite).
2. Resubmission defect (prerequisite; driven by evidence).
3. Groundwork:
   - the compute-in-render KMD contract;
   - MRT;
   - MSAA;
   - queries.
4. The D3D11 DDI and FL 11_0, with SM5 shaders. Acceptance: DWM DDisplay without
   `DisableDDisplay`, then CS2.
5. The D3D12 UMD (shape decided in step 4), with D3D9 through a mapping layer.
