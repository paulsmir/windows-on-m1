# EXP406 Coherent Full Graphics ABI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and hardware-test one structurally coherent Full Graphics WDDM 3.0 admission package that advances beyond EXP405 without starting the AGX render backend.

**Architecture:** Restore the exact EXP214/RosKmd base and Full Graphics display initialization shape, but keep unimplemented render DDIs fail-closed. Replace EXP406's Windows-visible raw AGX interrupt resources with one m1n1-owned synthetic edge interrupt whose broker status, mask, and acknowledge operations are safe at DIRQL; retain ordinary G2 and non-AGX as recovery artifacts.

**Tech Stack:** C11 m1n1 hypervisor, EDK2/Mu ASL and DSC/FDF profiles, WDK `10.0.28000.2526` ARM64 WDDM KMD/UMD, Python `unittest`, host C sanitizer tests, PowerShell packaging and evidence scripts.

**Spec:** `docs/superpowers/specs/2026-09-04-exp406-coherent-full-graphics-abi-design.md`

## Global Constraints

- `DRIVER_INITIALIZATION_DATA` is exactly 1296 bytes and Version is exactly `DXGKDDI_INTERFACE_VERSION_WDDM3_0` (`0xF003`).
- EXP406 restores the EXP214 vector plus the complete RosKmd Full Graphics display branch; all 162 compiled fields are governed by `investigation/EXP406_FULL_GRAPHICS_ABI_MATRIX.csv`, including the allocation, context, and paging boundaries.
- No physical AGX interrupt 880--888 is published to EXP406 Windows; exactly one synthetic edge INTID 889 is published.
- ISR code is nonpaged and bounded and performs only broker status/read/ack plus interlocked receipts. No registry, file, allocation, wait, logging, power, RTKit, UAT, queue, DCP, or pageable operation is reachable from ISR.
- No AGX power, RTKit, UAT, queue, submission, render success, preemption success, flip success, or fence completion capability is claimed.
- Normal current G2 and immutable non-AGX recovery behavior remain unchanged.
- Hardware uses non-AGX stage-only followed by candidate natural bind, exact evidence collection, exact cleanup, and ordinary current-G2 restoration.

---

### Task 1: Define the synthetic-only EXP406 interrupt contract

**Files:**
- Modify: `config/j313-agx-g2.json`
- Modify: `tools/generate_j313_agx_g2_contract.py`
- Modify: `drivers/apple-agx/shared/include/j313_agx_g2.generated.h`
- Modify: `m1n1_windows/src/hv_agx_g2.generated.h`
- Create: `mu/Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleAgxAbiAdmission.asl.inc`
- Test: `tests/test_j313_agx_g2_contract.py`

**Interfaces:**
- Produces: `G2Contract.synthetic_scanout_guest_interrupt: int`, exact value `889`.
- Produces: `render_abi_admission_asl_include(contract) -> str` with four existing MMIO resources and one `Interrupt(ResourceConsumer, Edge, ActiveHigh, Exclusive) { 889 }`.
- Produces: `J313_AGX_G2_SYNTHETIC_SCANOUT_GUEST_INTID` and `HV_AGX_G2_SYNTHETIC_SCANOUT_GUEST_INTID` constants.

- [ ] **Step 1: Write failing contract tests**

Add assertions that the JSON contract contains a `synthetic_interrupts` object with only `scanout`, that `scanout.guest == 889`, that it does not collide with physical guest routes or reserved 64/865, and that the generated Windows/m1n1 headers contain the exact constants. Add a deterministic test that the admission ASL has exactly four memory resources, one edge interrupt 889, and none of 880--888.

- [ ] **Step 2: Run the focused tests and observe RED**

Run:

```bash
python3 -m unittest tests.test_j313_agx_g2_contract
```

Expected: failure because the synthetic contract and admission ASL renderer do not exist.

- [ ] **Step 3: Implement the contract and regenerate outputs**

Extend the strict JSON schema and immutable dataclass with one exact scanout synthetic INTID. Render that constant into both generated headers. Add a separate admission-ASL renderer; do not remove or change any existing physical route in the normal G2 output.

Run:

```bash
python3 tools/generate_j313_agx_g2_contract.py
```

- [ ] **Step 4: Run focused tests and deterministic generation check**

Run:

```bash
python3 -m unittest tests.test_j313_agx_g2_contract
python3 tools/generate_j313_agx_g2_contract.py --check
```

Expected: PASS and no stale generated output.

- [ ] **Step 5: Commit the contract**

```bash
git add config/j313-agx-g2.json tools/generate_j313_agx_g2_contract.py tests/test_j313_agx_g2_contract.py drivers/apple-agx/shared/include/j313_agx_g2.generated.h m1n1_windows/src/hv_agx_g2.generated.h mu/Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleAgxAbiAdmission.asl.inc
git commit -m "Define EXP406 synthetic scanout interrupt"
```

### Task 2: Add the candidate-only Mu admission profile and m1n1 source

**Files:**
- Create: `mu/Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleAgxAbiAdmissionSsdt.asl`
- Create: `mu/Platform/MacBookAirMid2020Pkg/AcpiTables/DeviceAcpiTablesAgxAbiAdmission.inf`
- Modify: `mu/Platform/MacBookAirMid2020Pkg/MacBookAirMid2020.dsc`
- Modify: `mu/Platform/MacBookAirMid2020Pkg/MacBookAirMid2020.fdf`
- Modify: `m1n1_windows/src/hv_agx_power_mmio.c`
- Modify: `tests/test_j313_agx_g2_mu_profile.py`
- Modify: `tests/test_j313_agx_g2_contract.py`
- Test: `m1n1_windows/tests/hv_agx_scanout_broker_test.c`

**Interfaces:**
- Produces: build flag `J313_AGX_ABI_ADMISSION_PROFILE`, default `FALSE`, mutually exclusive with `J313_AGX_G2_PROFILE`.
- Produces: candidate ACPI storage with baseline tables plus the admission SSDT only.
- Consumes: `HV_AGX_G2_SYNTHETIC_SCANOUT_GUEST_INTID` in `hv_agx_scanout_service_run_once`.

- [ ] **Step 1: Write RED profile and synthetic-source tests**

Require the new DSC/FDF flag to select exactly one ACPI module, require the wrapper to include only the admission ASL include, and require normal G2 to remain unchanged. Require m1n1 scanout injection to use the generated synthetic constant and prohibit lookup of a physical completion route.

- [ ] **Step 2: Run focused tests and observe RED**

```bash
python3 -m unittest tests.test_j313_agx_g2_contract tests.test_j313_agx_g2_mu_profile
```

- [ ] **Step 3: Implement the Mu profile and synthetic m1n1 injection**

Add the two Mu source modules and a three-way profile selection: ABI admission, normal G2, stable. In m1n1 remove the `578 -> 887` validation from the scanout broker and inject only generated synthetic INTID 889. Do not alter physical route registration; those routes remain available to normal G2 but are absent from candidate ACPI.

- [ ] **Step 4: Run Mu/m1n1 contract tests**

```bash
python3 -m unittest tests.test_j313_agx_g2_contract tests.test_j313_agx_g2_mu_profile tests.test_j313_agx_g2_m1n1_policy
```

Expected: PASS.

- [ ] **Step 5: Commit the platform profile**

```bash
git add mu/Platform/MacBookAirMid2020Pkg m1n1_windows/src/hv_agx_power_mmio.c tests/test_j313_agx_g2_contract.py tests/test_j313_agx_g2_mu_profile.py
git commit -m "Add synthetic-only AGX ABI admission profile"
```

### Task 3: Restore the coherent Full Graphics initialization vector

**Files:**
- Modify: `drivers/apple-agx/render-admission/src/driver.c`
- Modify: `drivers/apple-agx/render-admission/include/render_admission.h`
- Modify: `drivers/apple-agx/render-admission/src/display.c`
- Modify: `drivers/apple-agx/render-admission/src/callbacks.c`
- Test: `tests/test_apple_agx_render_admission.py`

**Interfaces:**
- Produces: `AdmissionDdiSetPalette`, `AdmissionDdiGetScanLine`, `AdmissionDdiInterruptRoutine`, `AdmissionDdiDpcRoutine`, and `AdmissionDdiControlInterrupt` pointers in the exact EXP214/RosKmd positions.
- Produces: `AdmissionDdiPresent` always fails before a real present lowering path exists.

- [ ] **Step 1: Write RED vector and truthful-failure tests**

Assert all five missing assignments, exact WDDM 3.0 size/version, zero WDDM3 tail and reserved fields, no false Present success, and no capability bit added.

- [ ] **Step 2: Run the admission test and observe RED**

```bash
python3 -m unittest tests.test_apple_agx_render_admission
```

- [ ] **Step 3: Add SetPalette/GetScanLine and restore the vector**

SetPalette validates adapter/source/arguments and returns `STATUS_NOT_SUPPORTED` for the fixed 32-bpp mode. GetScanLine validates adapter/target/output, zeros the output fields, records an interlocked receipt, and returns `STATUS_NOT_SUPPORTED`. Restore ISR/DPC/ControlInterrupt assignments but implement their bodies only in Task 4. Change Present to return `STATUS_NOT_SUPPORTED` for every valid admission-stage call.

- [ ] **Step 4: Run the admission test**

Expected: vector and display tests PASS; Task 4 interrupt tests may still be RED.

- [ ] **Step 5: Commit the structural vector**

```bash
git add drivers/apple-agx/render-admission tests/test_apple_agx_render_admission.py
git commit -m "Restore coherent Full Graphics callback vector"
```

### Task 4: Implement broker-backed ISR quiesce and acknowledge

**Files:**
- Create: `drivers/apple-agx/render-admission/src/interrupt.c`
- Modify: `drivers/apple-agx/render-admission/src/lifecycle.c`
- Modify: `drivers/apple-agx/render-admission/include/render_admission.h`
- Modify: `drivers/apple-agx/render-admission/AppleAgxRenderAdmission.vcxproj`
- Test: `tests/test_apple_agx_render_admission.py`

**Interfaces:**
- Produces: `AdmissionInterruptStart(context, translated_resources) -> NTSTATUS`.
- Produces: `AdmissionInterruptStop(context) -> NTSTATUS`.
- Produces: nonpaged ISR that owns only `APPLE_AGX_SCANOUT_IRQ_MASK` at broker offset `0x400 + 0x14` and acknowledges by write-one-to-clear.
- Consumes: candidate resource list containing the broker page `0x300000000/0x1000` and exactly one interrupt.

- [ ] **Step 1: Write RED static and behavioral contract tests**

Require a single mapped broker page, an initial zero write to IRQ_ENABLE, bounded status masking, write-back acknowledge, no forbidden ISR APIs, no DPC queue without handled work, and stop ordering mask -> synchronize -> unmap. Add a small host C test for the pure status/mask/ack decision helper if that logic is factored into a freestanding unit.

- [ ] **Step 2: Run tests and observe RED**

```bash
python3 -m unittest tests.test_apple_agx_render_admission
```

- [ ] **Step 3: Implement minimal mapping and interrupt lifecycle**

Map the broker through `DxgkCbMapMemory` during StartDevice after resource validation. Write IRQ_ENABLE zero and clear stale owned status before setting `InterruptReady`. Implement ControlInterrupt only for `DXGK_INTERRUPT_CRTC_VSYNC`; enable is rejected until real scanout is active, disable always masks and clears owned status. Stop closes ingress with `DxgkCbSynchronizeExecution` and unmaps only after the synchronized callback observes it closed.

- [ ] **Step 4: Run admission and integrated offline suites**

```bash
python3 -m unittest tests.test_apple_agx_render_admission tests.test_apple_agx_admission_package tests.test_apple_agx_windows_package tests.test_j313_agx_g2_contract tests.test_j313_agx_g2_m1n1_policy tests.test_j313_agx_g2_mu_profile tests.test_verify_j313_agx_g2_aml
```

- [ ] **Step 5: Commit the interrupt contract**

```bash
git add drivers/apple-agx/render-admission tests/test_apple_agx_render_admission.py
git commit -m "Add safe EXP406 interrupt admission"
```

### Task 5: Build and freeze exact EXP406 artifacts

**Files:**
- Create under `.local/experiments/EXP-20260904-406-coherent-abi-admission/`: source archive, build scripts, logs, package, m1n1, candidate Mu, manifests and hashes.
- Modify: `investigation/EXPERIMENTS.md`

**Interfaces:**
- Produces: signed ARM64 Release KMD/UMD package, candidate Mu with ABI-admission SSDT, and m1n1 with synthetic INTID 889.

- [ ] **Step 1: Preregister EXP406 before any candidate launch**

Record WHY THIS HYPOTHESIS, ATOMIC CONTRACT, WINDOWS CONTRACT, AGX/ASAHI CONTRACT, TRANSLATION, WHAT IS STILL UNKNOWN, commits/diff hashes, exact build commands, recovery hashes, the Type 34 and Type 35 checkpoints, expected ladder and cleanup commands.

- [ ] **Step 2: Build KMD/UMD on FRYZZING using the EXP214-controlled root**

Use inherited `C:\Users\pauls\Directory.Build.props`, WDK/SDK `10.0.28000.2526`, ARM64 Release, code analysis, Universal validation, Inf2Cat and the existing signer. Do not add target-platform overrides or disable validation.

- [ ] **Step 3: Build candidate Mu and m1n1**

Build Mu with `BLD_*_J313_AGX_ABI_ADMISSION_PROFILE=TRUE` and normal G2 false. Build m1n1 from the recorded nested commit and dirty diff. Decompile the candidate SSDT and prove it contains memory resources plus edge 889 and no 880--888.

- [ ] **Step 4: Hash and freeze all artifacts**

Record SHA-256 for source archives, SYS/INF/CAT/CER/PDB/DLL, Mu FD, m1n1 Mach-O, AML and manifests. Reject any unstated hash change.

### Task 6: Execute natural EXP406 bind and exact recovery

**Files:**
- Create evidence under `.local/experiments/EXP-20260904-406-coherent-abi-admission/` and `C:\Users\pavel\AppleAgxEvidence\EXP406`.
- Modify: `investigation/EXPERIMENTS.md`
- Modify: `investigation/GPU_CURRENT_STATE.md`

**Interfaces:**
- Consumes: exact frozen package, candidate Mu/m1n1 and immutable recovery pair.
- Produces: one terminal EXP406 verdict and a restored clean current-G2 baseline.

- [ ] **Step 1: Verify both control planes and current-G2 clean baseline**

Require SSH, foreground runner, APPL0002 Code28, no project package/service/SYS, eight CPUs, healthy input/NVMe/xHCI/SSH and no fresh 41/129/1001.

- [ ] **Step 2: Stage only under current-compatible non-AGX and wait 180 seconds**

Verify exact hashes and signer, run only `pnputil /add-driver` without `/install`, record exact `oemN.inf`, and require every health checkpoint.

- [ ] **Step 3: Natural bind with candidate platform**

Shut down cleanly, verify Running proxy, launch exact EXP406 m1n1 and candidate Mu under a foreground runner, and allow natural APPL0002 selection. Do not run a second serial observer.

- [ ] **Step 4: Collect the first boundary and watchdog-window health**

Capture DxgkInitialize/Add/Start/Type34/35/display/VidPn/render-side receipts, PnP problem, GPU LUID, service/SYS/UMD hashes, System events, dumps and runner routes. A Code0 result must remain healthy for a separate 180 seconds. Physical AGX route enable is an immediate failure; synthetic 889 is allowed.

- [ ] **Step 5: Clean exact package and restore normal G2**

After evidence, boot non-AGX if necessary, delete only the recorded `oemN.inf`, matching stopped service, exact binaries/staging/receipts, then restore ordinary current G2 and prove Code28 clean health.

- [ ] **Step 6: Record verdict and immediately select the next layer**

If admission passes, use the first invoked fail-closed render-side callback to plan allocation/device/context. If admission fails, localize the structural group offline from exact vector memory/image; do not run callback-pointer probes.

### Task 7: Record committed changes

**Files:**
- Modify: `investigation/CHANGES.csv`
- Test: `tests/test_change_ledger.py`

**Interfaces:**
- Consumes: each implementation commit and final EXP406 evidence hashes.
- Produces: one RFC 4180 row per reviewable change with full 40-character commit IDs.

- [ ] **Step 1: Append rows after each implementation commit**

Use `status=implemented` for software-only results and only `validated` when EXP406 hardware evidence supports the change. Do not alter older rows.

- [ ] **Step 2: Verify new rows and repository diff**

```bash
python3 -m unittest tests.test_change_ledger
git diff --check
```

If the known older invalid `change_type=diagnostic` row still fails the global ledger test, separately parse and validate every new EXP406 row and report the inherited failure without rewriting unrelated history.
