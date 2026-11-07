# DXIL module profile agreement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. The user explicitly authorized autonomous execution through agx-d3d12-loop; retain that method and root ownership of tracked edits, builder and hardware.

**Goal:** Reject a PROGRAM whose framing and actual DXIL module disagree with the selected compute6.0/DXIL1.0 owned-admission profile.

**Architecture:** Add one original typed scalar query inside the selected external CAPI owner, expose it through the existing checked facade, and compare profiles before resource conversion. Keep generic framing, shader translation, firmware, public DDIs and caps unchanged. External bodies remain outside Git under the existing reviewed MIT closure.

**Tech Stack:** Original C/C++ checked adapter; pinned dxil-spirv8434 plus accepted1421 composite; existing83TUs/9archives; pinned SDK26100 DXC; host ASan/UBSan; Windows ARM64 existing CRT/exception policy.

**Spec:** investigation/analysis/D3D12-C15-dxil-profile-agreement.md

## Global Constraints

- No publicSM6/caps/productionDXIL-link claim from this checkpoint.
- Real DXCcs6.0/6.1 fixtures only, bitcode immutable during numeric PROGRAM-header changes; no mockmetadata, malformedmetadata synthesis, fuzzing or fault sweeps.
- Existing external source/cache and receipts remain immutable; fresh four-file projection retains full MIT notices and all other bytes.
- Existing checked external failures remain REPORTED_ERROR; PROJECT_OOM is reserved for project allocation failure.
- Hardware1530 owns its guest until complete recovery. Next1534 is CPU only, after RECOVERY_DONE/normal inert PackageAbsent and its own BEFORE.

## Review Focus

- Realcs6.0 must keep exact old source/map/workgroup and nonempty SPIR-V success (Task1).
- Realcs6.1 with masked6.0header must retain actualmodule6.1 and be refused (Task1).
- Realcs6.0 with DXIL1.1header must be refused without editing LLVM bytes (Task1).
- A nominal conversion on the same thread after each refusal must succeed (Task1).
- C/C++ scalar API must preserve actual6.0/1.0 and6.1/1.1 across the Windows ARM64 ABI (Task2).

### Task1: Deterministic actual-module profile admission

**Files:** external private projection only: dxil_spirv_c.h/.cpp and bc/metadata.hpp/.cpp; project drivers/apple-agx/mesa/dxil/agx_dxil_checked.h/.cpp and agx_dxil_owned.c; original tests/profile_agreement_fixture.c plus opt-in pinned host recipe.

**Interfaces:** `dxil_spv_compute_profile` has four uint32_t fields shader_major, shader_minor, dxil_major, dxil_minor. `dxil_spv_parsed_blob_get_compute_profile(blob, profile*)` reads the real owned LLVMBC module; `agx_checked_compute_profile(blob, profile*) noexcept` clears reported failures. `const MDOperand *MDNode::getOperandOrNull(unsigned) const` adds no layout or existing API change. Existing `agx_dxil_convert_owned` signature remains.

- [x] Produce and verify actual DXCcs6.0/6.1 from identical HLSL, validation enabled; EXP1532 fixture phase RC0 and actualmetadata6.0/1.0 versus6.1/1.1.
- [ ] Run original actual adapter/archives with the real fixture; capture each case and exact expected profile-refusal mismatch. Do not declare RED if old converter already refuses.
- [ ] After confirmed RED, qualify the four-file original external extension and three-file project proposal. Require namedmetadata cardinality, typed32-bit scalars and compute name; no default profile fallback or opaque casts. Do not adopt external bodies in Git.
- [ ] Rebuild the exact affected83/9 graph from a fresh source projection and run the same actual-module fixture with getter checks. Require zero failures, immutable inputs, workgroup64,1,1, one rawUAV slot0 and same-thread recovery controls.
- [ ] Run existing real nominal/owned/384word controls with reviewed exact source/archives; preserve accepted diagnostics. Run affected tests, then full suite once before implementation commit and compare exact known baseline failures.
- [ ] Commit original project implementation/tests/projector and notice/provenance metadata without attribution; append CHANGES with the resulting40-character commit and implemented status.

### Task2: Native ABI and physical CPU checkpoint

**Files:** fresh private EXP1533 native source/manifest/owned builder controller and EXP1534 CPU-only fixture harness; original fixture from Task1.

**Interfaces:** consume Task1's exact sourceprojection and four-scalar CAPI plus actual fixture pins; produce hash-bound native objects/archives/EXE and actual CPU oracle. No D3D/driver/GPU APIs are introduced.

- [ ] Before1533, pin source/tool/actual include closure and retain effective exception/CRT policies, build fresh83/9 and fixture, require compiler/link RC0, actualAA64 objects and all pre/post inputs unchanged.
- [ ] Before1534, require1530 evidence collected and normal visible RECOVERY_DONE/PackageAbsent/noService8CPU, exact EXE/input hashes and bounded owned child; execute only the fixture, collect raw output and all input/output hashes.
- [ ] Require all real-module scalar/admission/nominal controls PASS on Windows ARM64. Record ACTUAL and a qualified CPU-only verdict with limits; add CHANGES validation only for the boundary actually observed. PublicSM6/DDI admission remains a separate future gate.
