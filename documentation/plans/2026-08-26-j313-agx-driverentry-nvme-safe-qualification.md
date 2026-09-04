# J313 AGX DriverEntry NVMe-Safe Qualification Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:executing-plans` to execute this plan task-by-task.  The physical
> USB/proxy path has one owner; do not dispatch parallel hardware agents.

**Goal:** Execute one fail-closed Windows GPU DriverEntry boundary measurement
without reintroducing the virtual-NVMe timeout/reset defect.

**Architecture:** Keep the EXP-120 Windows qualification driver, G2 Mu firmware,
ACPI resources and synthetic power broker byte-for-byte unchanged.  Replace only
the old pre-fix m1n1 with clean commit `bee53dc`, whose bounded completion batches
passed EXP-123.  Collect persistent DriverEntry breadcrumbs, then return to the
immutable stable firmware before removing the exact staged package and signer.

**Tech Stack:** J313 m1n1 EL2, Mu UEFI/ACPI, WDDM display miniport,
PowerShell/PnPUtil, Windows Event Log and public assisted launcher.

**Spec:** `documentation/plans/2026-08-26-j313-agx-driverentry-boundary-qualification.md`

## Global Constraints

- Experiment ID is `EXP-20260826-124`; EXP-120 is superseded unexecuted.
- One G2 boot only.  No retry under the same experiment ID.
- No GPU firmware, RTKit, SGX MMIO, interrupt, UAT, queue, command, fence,
  shader, render, present or display-ownership operation is permitted.
- No raw PMGR access and no forced driver-package deletion are permitted.
- Stop before hardware mutation on any hash, manifest, package, certificate,
  cleanup, CPU, input, storage or critical-event mismatch.
- Immutable recovery remains
  `.local/recovery/STABLE-j313-8core-native-input-v1/`.

---

### Task 1: Prove the clean stable preflight

**Files:**
- Read: `.local/recovery/STABLE-j313-8core-native-input-v1/`
- Read: `investigation/artifacts/EXP-20260826-119-agx-startdevice-boundary/cleanup-final.txt`
- Record: `investigation/EXPERIMENTS.md`

**Interfaces:**
- Consumes: live stable Windows over SSH at `pavel@192.168.1.37`.
- Produces: exact zero-state proof for APPL0002, AppleAgx package and pinned signers.

- [x] Remove only old `oem17.inf` using ordinary deletion and the permitted
  non-force `/uninstall` fallback; remove only signer
  `DC81FF63FD2FFE8CDE24F95052C45BB7C0006731` from Root and TrustedPublisher.
- [x] Verify 8 logical processors; `AppleInput`, `stornvme` and `USBXHCI`
  Running; zero present APPL0002; zero AppleAgx package; zero pinned signers;
  zero critical event and zero `stornvme` Event 129 since boot.
- [x] Verify recovery hashes:
  `J313_EFI.fd=4c5e068f664d8ccc94823880de4226e3f7842e08841bc10fea19cbe9e05a519b`
  and
  `m1n1.macho=3b81d82176b9853228b39eb3bb56ceff018cd0542248e872dd1bc1304c32b82e`.

### Task 2: Verify exact candidate identities

**Files:**
- Read: `.local/agx-power-exp120-ci/DRIVER-MANIFEST.json`
- Read: `.local/agx-power-exp124-profile/MANIFEST.json`
- Read: `.local/agx-power-exp120-ci/stage-exp120.ps1`
- Read: `.local/agx-power-exp120-ci/collect-exp120.ps1`
- Read: `.local/agx-power-exp120-ci/rollback-exp120.ps1`

**Interfaces:**
- Consumes: committed m1n1 `bee53dc60bd160c0a64de758974af767c2970baf`.
- Produces: one immutable profile and one exact signed qualification package.

- [x] Verify profile manifest SHA-256
  `02204a6e37a04a323eae05e24b6a35eb7a0c6327b9af98b39d714482d78a0c70`.
- [x] Verify profile members: m1n1
  `2c39f7723475e6e74fa00b1a88e413ed7e5159a0da1bac5286b6c0442b7d52a9`,
  Mu `70b216c01f3d7acd77b2c24d0a3dc4fa0cccefec631031a413a301d271f6c064`
  and SSDT `a6f8f4911030c23b61a2ed8c3a300d1ca438af74accc41e624918930ef55f65b`.
- [x] Verify driver manifest SHA-256
  `6cf7321e32849418a4dbac70cc027db0fedb4b5ab3fbadf6c3b325357c8262ca`
  and signer `442D150255F1F27A6D10CFD8E4BF5F35E8AD28BB`.
- [ ] Re-run both manifest validators immediately before staging.

### Task 3: Stage only the qualification package

**Files:**
- Copy unchanged package to: `C:\Users\pavel\EXP124Package\`
- Produce: `investigation/artifacts/EXP-20260826-124-agx-driverentry-nvme-safe/stage.log`

**Interfaces:**
- Consumes: exact files validated by `stage-exp120.ps1`.
- Produces: one recorded `oemNN.inf`; no present APPL0002 under stable firmware.

- [ ] Copy the exact package and scripts over SSH without changing contents.
- [ ] Execute `stage-exp120.ps1 -PackageRoot C:\Users\pavel\EXP124Package`.
- [ ] Record the exact new published name and prove APPL0002 remains absent.
- [ ] Shut Windows down normally and wait for a fresh responsive proxy generation.

### Task 4: Execute one G2 DriverEntry measurement

**Files:**
- Produce: `investigation/artifacts/EXP-20260826-124-agx-driverentry-nvme-safe/launch-contract.bin`
- Produce: `investigation/artifacts/EXP-20260826-124-agx-driverentry-nvme-safe/hv.log`

**Interfaces:**
- Consumes: staged exact package and manifest-verified EXP-124 firmware profile.
- Produces: exactly one Windows boot exposing APPL0002 and persistent DriverEntry values.

- [ ] Launch exactly:

  ```sh
  WOM1_AGX_G2_POWER_BROKER=1 \
  M1N1VUART=/dev/cu.usbmodemC02HDNCCQ6L43 \
  scripts/run-assisted.sh \
    --proxy /dev/cu.usbmodemC02HDNCCQ6L41 \
    --vuart /dev/cu.usbmodemC02HDNCCQ6L43 \
    --firmware .local/agx-power-exp124-profile/J313_EFI.fd \
    --m1n1 .local/agx-power-exp124-profile/m1n1.macho \
    --display both --debug monitor --chainload \
    --contract-output investigation/artifacts/EXP-20260826-124-agx-driverentry-nvme-safe/launch-contract.bin \
    --foreground
  ```

- [ ] Require only broker mapping `0x300000000..0x300001000`, ABI 1 and guest
  handoff.  Stop on any forbidden GPU action, BugCheck, reset, storage Event 129,
  input loss or unresponsive guest.
- [ ] Within 180 seconds run
  `collect-exp120.ps1 -Root C:\Users\pavel\EXP124`, then copy all evidence to
  the experiment directory together with live `hv.log`, framebuffer metadata
  and telemetry status.
- [ ] Classify: no values = loader boundary; stage 1/pending = entry without
  return; stage 2/nonzero = exact `DxgkInitialize` rejection; stage 2/zero =
  inspect existing StartDevice stages while still remaining fail closed.

### Task 5: Roll back and record the result

**Files:**
- Produce: `investigation/artifacts/EXP-20260826-124-agx-driverentry-nvme-safe/SHA256SUMS`
- Modify: `investigation/EXPERIMENTS.md`
- Modify: `investigation/CHANGES.csv` only after an implementation commit exists.

**Interfaces:**
- Consumes: exact published name and signer from Tasks 2–3.
- Produces: immutable stable Windows with no active GPU candidate and a durable verdict.

- [ ] Shut the G2 guest down normally and boot the immutable stable pair.
- [ ] Prove APPL0002 absent, then run
  `rollback-exp120.ps1 -PublishedName <exact-oemNN.inf>`; never use `/force`.
- [ ] Verify zero AppleAgx package/service/module/signer, 8 CPUs, healthy input,
  NVMe/xHCI and zero new critical or storage-reset event.
- [ ] Hash every evidence file, append the observed result to EXP-124 and mark
  the next software-only correction permitted by the exact NTSTATUS boundary.

## Self-review

- Spec coverage: exact cleanup, identities, one-shot execution, evidence,
  forbidden hardware and rollback are each assigned to a task.
- Placeholder scan: no TBD/TODO or unspecified error-handling step remains.
- Identity consistency: `bee53dc`, profile manifest `02204a6e...`, driver
  manifest `6cf7321e...`, signer `442D1502...` and experiment ID 124 are used
  consistently throughout.
