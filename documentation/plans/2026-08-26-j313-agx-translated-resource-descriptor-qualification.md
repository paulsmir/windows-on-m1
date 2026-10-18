# J313 AGX Translated Resource Descriptor Qualification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Measure the exact translated resource descriptors delivered by dxgkrnl to AppleAgx in one fail-closed G2 boot without weakening validation or initializing GPU hardware.

**Architecture:** Keep the EXP-124 G2 Mu firmware, ACPI resources, synthetic power broker and NVMe-safe m1n1 byte-for-byte unchanged. Replace only the qualification driver with CI run `33010381345`, which persists a bounded normalized view of the translated resource list before the unchanged validator rejects it. Prepare and recover with the validated EXP-123 pair and classify boot-time storage resets separately from the quiet runtime window.

**Tech Stack:** ARM64 WDDM display miniport, WDK CI test signing, PowerShell PnP/registry collection, Mu ACPI, m1n1 hypervisor, Windows 11 ARM64.

**Spec:** `investigation/EXPERIMENTS.md` entries EXP-124 and EXP-125; Microsoft Learn `DXGK_DEVICE_INFO`, `Raw and Translated Resources`, `IRP_MN_START_DEVICE`, and `CM_PARTIAL_RESOURCE_DESCRIPTOR` contracts.

## Global Constraints

- Experiment identity is `EXP-20260826-126`; exactly one G2 candidate boot and no retry.
- Root branch is `feature/j313-gpu-acceleration`.
- The sole candidate variable relative to EXP-125 is qualification-only translated-resource breadcrumbs from commit `5d58cfb95640bc725d6ec42f4980f4f6e8fa7e7a`.
- No GPU firmware, RTKit, SGX MMIO access, interrupt object, UAT, queue, command, fence, shader, render, present or display-ownership action is permitted.
- No forced driver deletion, broad certificate removal, recovery-image replacement or registry deletion is permitted.
- Abort before mutation on any identity, signer, CPU, input, storage, package or critical-event mismatch.

## Fixed Identities

- WDK run: `33010381345`; default and power-qualification ARM64 jobs passed.
- Driver manifest: `.local/agx-exp126-driver/DRIVER-MANIFEST.json`, SHA-256 `122c0ee602e047cf23bcc81a389657c53d3a49bd24749354ed660beeb3fbca3b`.
- Driver SYS SHA-256: `2dc6317b80cef81822748aa7bb068415ec3de71a44fb2bbd963872a334230451`.
- Catalog SHA-256: `bfa914e439f54ddcc31115dc181b147234878988155382a4cdf2ba32abc9e0fd`.
- Certificate SHA-256: `ef08f7a3aa769a31d682ccb80156c0525f23b2352890a9b1a95e7d290cc7a00d`.
- Signer SHA-1: `419A261FEC73D775202BAC41300EF47F37531580`.
- G2 candidate manifest: `.local/agx-power-exp124-profile/MANIFEST.json`, SHA-256 `02204a6e37a04a323eae05e24b6a35eb7a0c6327b9af98b39d714482d78a0c70`.
- Candidate Mu SHA-256: `70b216c01f3d7acd77b2c24d0a3dc4fa0cccefec631031a413a301d271f6c064`.
- Candidate m1n1 SHA-256: `2c39f7723475e6e74fa00b1a88e413ed7e5159a0da1bac5286b6c0442b7d52a9`.
- Recovery manifest: `.local/experiments/EXP-20260826-123-vnvme-bounded-completion-batch/MANIFEST.json`, SHA-256 `143fd9aa07f9b224c316c5e23e3993991d7308fa178164beadc785e8dade03f9`.
- Recovery Mu SHA-256: `4c5e068f664d8ccc94823880de4226e3f7842e08841bc10fea19cbe9e05a519b`.
- Evidence destination: `investigation/artifacts/EXP-20260826-126-agx-translated-resources/`; it must be absent before execution.

---

### Task 1: Establish a bounded diagnostic implementation

**Files:**
- Modify: `drivers/apple-agx/windows/include/apple_agx_driver.h`
- Modify: `drivers/apple-agx/windows/src/adapter.c`
- Modify: `drivers/apple-agx/windows/src/driver_diagnostics.c`
- Test: `tests/test_apple_agx_windows_package.py`

**Interfaces:**
- Consumes: `DXGK_DEVICE_INFO.TranslatedResourceList` after successful `DxgkCbGetDeviceInformation`.
- Produces: `AppleAgxRecordTranslatedResources(PDEVICE_OBJECT, PCM_RESOURCE_LIST)` and `Wom1Resource*` DWORDs under the APPL0002 `Device Parameters` key.

- [x] **Step 1: Write a failing test requiring bounded translated descriptor breadcrumbs before validation.**
- [x] **Step 2: Run the focused test and observe failure because the recorder does not exist.**
- [x] **Step 3: Implement a qualification-only recorder capped at 16 descriptors.**
- [x] **Step 4: Record list counts, type/share/flags, MMIO base/length and IRQ level/vector/affinity without hardware access.**
- [x] **Step 5: Run focused tests (21/21), canonical proxyenv tests (662/662) and `git diff --check`.**
- [x] **Step 6: Commit as `5d58cfb95640bc725d6ec42f4980f4f6e8fa7e7a` and pass WDK run `33010381345`.**

### Task 2: Prove the recovery guest is clean and runtime-quiet

**Files:**
- Read: `.local/experiments/EXP-20260826-123-vnvme-bounded-completion-batch/MANIFEST.json`
- Create at runtime: `investigation/artifacts/EXP-20260826-126-agx-translated-resources/recovery-preflight.txt`

**Interfaces:**
- Consumes: exact EXP-123 recovery pair already running after EXP-125 rollback.
- Produces: a timestamped go/no-go state for package staging.

- [ ] **Step 1: Validate the recovery manifest and both artifact hashes.**
- [ ] **Step 2: Require eight CPUs, zero APPL0002, zero AppleAgx service/package/module/signer, and Running AppleInput, stornvme and USBXHCI.**
- [ ] **Step 3: Record the known two boot-time Event 129 resets and require a quiet window with zero Event 129 after `2026-08-26T22:14:12+02:00`.**
- [ ] **Step 4: Require zero critical events and a responsive SSH guest; abort before staging on failure.**

### Task 3: Stage only the exact EXP-126 package

**Files:**
- Read: `.local/agx-exp126-driver/DRIVER-MANIFEST.json`
- Use: `.local/agx-exp126-driver/stage-exp126.ps1`
- Create on guest: `C:\Users\pavel\EXP126Package\`

**Interfaces:**
- Consumes: exact eight-file manifest and signer.
- Produces: one staged package identity saved as `$publishedName` from the before/after driver-store difference.

- [ ] **Step 1: Revalidate all eight files against `DRIVER-MANIFEST.json`.**
- [ ] **Step 2: Copy only `.local/agx-exp126-driver/` to `C:\Users\pavel\EXP126Package\`.**
- [ ] **Step 3: Run `stage-exp126.ps1 -PackageRoot C:\Users\pavel\EXP126Package` as Administrator.**
- [ ] **Step 4: Record the single new `oemNN.inf` as `$publishedName`; require APPL0002 absent and AppleAgx unloaded under recovery firmware.**
- [ ] **Step 5: Shut down normally and wait for a fresh proxy.**

### Task 4: Execute one translated-resource measurement

**Files:**
- Use: `.local/agx-power-exp124-profile/J313_EFI.fd`
- Use: `.local/agx-power-exp124-profile/m1n1.macho`
- Use on guest: `C:\Users\pavel\EXP126Package\collect-exp126.ps1`
- Create: `investigation/artifacts/EXP-20260826-126-agx-translated-resources/`

**Interfaces:**
- Consumes: one staged EXP-126 package and unchanged G2 profile.
- Produces: one sanitized JSON state, PnP resource text, launch contract, local `hv.log`, display metadata and telemetry.

- [ ] **Step 1: Launch exactly:**

  ```sh
  WOM1_AGX_G2_POWER_BROKER=1 \
  M1N1VUART=/dev/cu.usbmodemC02HDNCCQ6L43 \
  scripts/run-assisted.sh \
    --proxy /dev/cu.usbmodemC02HDNCCQ6L41 \
    --vuart /dev/cu.usbmodemC02HDNCCQ6L43 \
    --firmware .local/agx-power-exp124-profile/J313_EFI.fd \
    --m1n1 .local/agx-power-exp124-profile/m1n1.macho \
    --display both --debug monitor --chainload \
    --contract-output investigation/artifacts/EXP-20260826-126-agx-translated-resources/launch-contract.bin \
    --foreground
  ```

- [ ] **Step 2: Require one APPL0002, expected Problem 43, eight CPUs, healthy input/storage/xHCI, zero critical events and zero Event 129 in the candidate.**
- [ ] **Step 3: Run `collect-exp126.ps1 -Root C:\Users\pavel\EXP126` once within 180 seconds.**
- [ ] **Step 4: Require full-count 1, descriptor-count 11, overflow 0, two exact MMIO descriptors and nine interrupt descriptors.**
- [ ] **Step 5: Preserve only the sanitized JSON and PnP text plus local HV/display evidence; do not export full registry or event logs.**
- [ ] **Step 6: Audit for zero broker command, firmware, RTKit, SGX MMIO, interrupt, UAT, queue, render and present action.**

### Task 5: Classify, roll back and close EXP-126

**Files:**
- Use on guest: `C:\Users\pavel\EXP126Package\rollback-exp126.ps1`
- Modify: `investigation/EXPERIMENTS.md`
- Modify: `investigation/CHANGES.csv`

**Interfaces:**
- Consumes: exact descriptor JSON and `$publishedName`.
- Produces: one evidence-backed validator correction boundary and a clean recovery guest.

- [ ] **Step 1: Compare translated MMIO identities with generated G2 constants and treat translated IRQ vectors as system vectors, not firmware GSIs.**
- [ ] **Step 2: Shut down the G2 guest normally and boot the exact EXP-123 recovery pair.**
- [ ] **Step 3: Run `rollback-exp126.ps1 -PublishedName $publishedName`; never use `/force`.**
- [ ] **Step 4: Complete one cleanup reboot if PnP marks the service for deletion.**
- [ ] **Step 5: Require no APPL0002/package/service/signer, eight CPUs, healthy input/NVMe/xHCI, zero critical events and no new runtime Event 129 after the boot-time baseline.**
- [ ] **Step 6: Hash evidence, append the observed result, update the change ledger, run verification and push the branch.**

## Falsifiable Result

The diagnostic hypothesis passes only if all translated descriptor values survive driver unload, the unchanged validator still fails at stage 3, every forbidden GPU action remains absent and recovery removes the exact package without force. Any missing descriptor, overflow, candidate Event 129, critical event, input/storage loss, hash mismatch or second G2 boot rejects EXP-126. A successful measurement authorizes only the minimal resource-validator correction supported by the recorded representation.
