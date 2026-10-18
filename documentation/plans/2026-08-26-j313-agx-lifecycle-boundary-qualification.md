# J313 AGX AddDevice/StartDevice Boundary Qualification Plan

**Goal:** Execute one fail-closed Windows GPU lifecycle measurement that
distinguishes `DxgkDdiAddDevice` from each existing `DxgkDdiStartDevice`
boundary without initializing GPU hardware.

**Architecture:** Keep the EXP-124 G2 Mu firmware, ACPI resources, synthetic
power broker and NVMe-safe m1n1 byte-for-byte unchanged. Replace only the
qualification driver with commit `6692ffb`, which writes device-instance
registry breadcrumbs for AddDevice and StartDevice. Use the hardware-validated
EXP-123 Mu/m1n1 pair for preparation and recovery so the old one-CQE recovery
binary cannot inject storage-reset noise.

**Primary sources inspected:** live EXP-124 binding and registry evidence;
current AppleAgx miniport; Microsoft `DxgkDdiAddDevice`,
`DxgkDdiStartDevice`, `DxgkCbGetDeviceInformation`,
`IoOpenDeviceRegistryKey` and Plug and Play registry documentation; unchanged
Asahi AGX, m1n1 AGX/NVMe and Mu G2 resource implementations reviewed by the
preceding G2 plans.

**Ownership:** Dxgkrnl invokes the lifecycle callbacks; AppleAgx owns only its
private context, exact translated-resource validation and qualification
breadcrumbs; m1n1 owns virtual NVMe and the synthetic broker; Mu owns ACPI
publication; no component is authorized to initialize AGX firmware, RTKit,
UAT, queues or rendering in this experiment.

## Fixed identities

- Experiment: `EXP-20260826-125`; one G2 candidate boot, no retry.
- Root branch: `feature/j313-gpu-acceleration`.
- Root preregistration base: `b6b7f7d` plus this plan/ledger commit.
- Driver source: `6692ffbbe6738b3066854cf42dbe38b524715934`.
- WDK run: `33007284611`, both ARM64 jobs passed.
- Driver manifest:
  `.local/agx-exp125-driver/DRIVER-MANIFEST.json`, SHA-256
  `21d8cd97630389d19c7185ee110c7eac81e78ecee835d8fe940b7344df3505d6`.
- Driver SYS SHA-256:
  `6a8bac7b40dd13e960b87f138b391e4eae2f79373c9623ca1803cd6b1c9a91e6`.
- Catalog SHA-256:
  `87c70750c56d18229a313178421a0dcf1c961f0523bf971defbd66ffba4ee020`.
- Signer thumbprint: `F247053BE6C49EFEB4C8D8AEBF6F47399787B1C2`.
- G2 candidate manifest:
  `.local/agx-power-exp124-profile/MANIFEST.json`, SHA-256
  `02204a6e37a04a323eae05e24b6a35eb7a0c6327b9af98b39d714482d78a0c70`.
- Candidate m1n1 SHA-256:
  `2c39f7723475e6e74fa00b1a88e413ed7e5159a0da1bac5286b6c0442b7d52a9`.
- Candidate Mu SHA-256:
  `70b216c01f3d7acd77b2c24d0a3dc4fa0cccefec631031a413a301d271f6c064`.
- Recovery manifest:
  `.local/experiments/EXP-20260826-123-vnvme-bounded-completion-batch/MANIFEST.json`,
  SHA-256
  `143fd9aa07f9b224c316c5e23e3993991d7308fa178164beadc785e8dade03f9`.
- Recovery Mu SHA-256:
  `4c5e068f664d8ccc94823880de4226e3f7842e08841bc10fea19cbe9e05a519b`;
  recovery m1n1 is the same NVMe-safe `2c39f772...` binary.

## Forbidden actions

- No GPU firmware, RTKit, SGX MMIO, interrupt, UAT, queue, command, fence,
  shader, render, present or display-ownership action.
- No raw PMGR access, forced driver deletion, broad certificate removal,
  recovery-image replacement or second G2 boot.
- Stop before mutation on any hash, manifest, signer, CPU, input, storage,
  package or critical-event mismatch.

## Execution

### 1. Establish a clean NVMe-safe recovery guest

- Shut the currently running guest down normally.
- Boot the exact EXP-123 recovery pair with `display=both`, `debug=monitor`.
- Require within 30 seconds: eight CPUs; `AppleInput`, `stornvme` and
  `USBXHCI` Running; zero present APPL0002; zero AppleAgx package, loaded
  module and pinned EXP-125 signer; zero critical event and zero stornvme
  Event 129 since boot.
- The old AppleAgx service has `DriverDelete=1` and `DeleteFlag=1`. Require the
  recovery reboot to clear it. If it remains, abort before staging and record
  the cleanup failure; do not improvise registry deletion.

### 2. Validate and stage only the exact package

- Re-run both manifest validators immediately before copying.
- Copy `.local/agx-exp125-driver/` to
  `C:\Users\pavel\EXP125Package\` over SSH.
- Run `stage-exp125.ps1 -PackageRoot C:\Users\pavel\EXP125Package` as
  Administrator.
- Record the single new `oemNN.inf`; prove APPL0002 remains absent and the
  package is not loaded under recovery firmware.
- Shut down normally and wait for a fresh responsive proxy generation.

### 3. Execute one G2 lifecycle measurement

- Launch exactly:

  ```sh
  WOM1_AGX_G2_POWER_BROKER=1 \
  M1N1VUART=/dev/cu.usbmodemC02HDNCCQ6L43 \
  scripts/run-assisted.sh \
    --proxy /dev/cu.usbmodemC02HDNCCQ6L41 \
    --vuart /dev/cu.usbmodemC02HDNCCQ6L43 \
    --firmware .local/agx-power-exp124-profile/J313_EFI.fd \
    --m1n1 .local/agx-power-exp124-profile/m1n1.macho \
    --display both --debug monitor --chainload \
    --contract-output investigation/artifacts/EXP-20260826-125-agx-lifecycle-boundary/launch-contract.bin \
    --foreground
  ```

- Require only broker mapping `0x300000000..0x300001000`, ABI 1 and guest
  handoff. Abort on any forbidden GPU action, BugCheck, reset, Event 129,
  input loss or unresponsive guest.
- Within 180 seconds run
  `collect-exp125.ps1 -Root C:\Users\pavel\EXP125`, then preserve its output,
  live `hv.log`, framebuffer metadata and telemetry under
  `investigation/artifacts/EXP-20260826-125-agx-lifecycle-boundary/`.

### 4. Classify the exact boundary

- No AddDevice values: callback was not invoked or device-key open failed;
  correlate with the unchanged service-level DriverEntry values.
- Add stage 1: AddDevice entered but did not return.
- Add stage 2 with nonzero status: exact AddDevice failure.
- Add stage 2 with zero status and no Start values: AddDevice succeeded but
  StartDevice was not invoked.
- Start values: the stage number maps directly to
  `APPLE_AGX_START_STAGE`; the paired DWORD is the exact NTSTATUS.
- Stage 7 with `STATUS_NOT_SUPPORTED` is the designed fail-closed end state,
  not a GPU initialization success.

### 5. Roll back and preserve evidence

- Shut the G2 guest down normally.
- Boot the exact EXP-123 recovery pair and prove APPL0002 absent before
  package mutation.
- Run `rollback-exp125.ps1 -PublishedName <exact-oemNN.inf>`; never use
  `/force`.
- Require zero AppleAgx package/service/module/signer; eight CPUs; healthy
  input, NVMe and xHCI; zero new critical and storage-reset event.
- Hash every evidence file, append the observed result to EXP-125 and authorize
  only the next smallest change justified by the recorded stage/status.

## Falsifiable result

The experiment passes its diagnostic hypothesis only if the exact package
binds and at least the AddDevice stage/status pair survives driver unload while
all forbidden GPU counters remain zero. The platform health gate passes only
if both candidate and recovery boots remain responsive with zero critical and
storage-reset events. Any other result is rejected or partial and is not
retried under EXP-125.
