# J313 AppleAgx G2 Fail-Closed Bind Gate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove once that the qualified ARM64 AppleAgx skeleton binds to the
opt-in J313 G2 ACPI device, validates the exact resources, returns its designed
fail-closed `STATUS_NOT_SUPPORTED`, and leaves Windows fully recoverable without
performing any AGX hardware access.

**Architecture:** Stage the already-qualified package while stable firmware
omits APPL0002, shut Windows down normally, then boot the already-qualified G2
enumeration candidate once.  Windows PnP may load only the read-only skeleton;
the skeleton validates resources and deliberately fails StartDevice before
MMIO.  Capture the failed devnode and logs, return to immutable stable firmware,
then remove only the recorded package and signer certificates.

**Tech Stack:** ARM64 Windows 11, WDDM 2.6 display miniport, PowerShell,
PnPUtil, public assisted m1n1 launcher, Mu UEFI/ACPI, JSON and SHA-256 evidence.

**Spec:** `documentation/plans/2026-08-26-j313-agx-g2-render-kmd.md`

## Global Constraints

- Work only in `/Users/pavel/public_windows` on
  `feature/j313-gpu-acceleration`.
- Preserve `.local/recovery/STABLE-j313-8core-native-input-v1/` byte-for-byte.
- Use G2 manifest SHA-256
  `596ed2f2ad1465fd75e1dd560adc3d5da94ea62d41a68e98e2a955bf0804f2ea`.
- Use driver manifest SHA-256
  `ee9ac4532e4432e2b4e7faedc70ef1f101efd454f1db8f236fbb2710b26e217d`.
- The only accepted signer thumbprint is
  `7772864CB7326B7BFDA2C81C12D07CEF64135A57`.
- Do not rebuild, edit, retry or substitute either candidate inside the run.
- Do not map AGX MMIO, start firmware, enable clocks, route an AGX interrupt,
  create UAT state, submit a command, take display ownership or reboot G2.
- One G2 boot only.  Any mismatch triggers evidence preservation and stable
  rollback; it never broadens the test.

---

### Task 1: Freeze the one-shot experiment contract

**Files:**
- Modify: `investigation/EXPERIMENTS.md`
- Modify: `investigation/CHANGES.csv`

**Interfaces:**
- Consumes: EXP-113 G2 firmware evidence and EXP-115 package evidence.
- Produces: one immutable EXP-116 contract and a fresh evidence identity.

- [ ] **Step 1: Record both exact candidate identities**

Record G2 `m1n1.macho` SHA-256
`0055ef339c5ae9099014e3d8e5158a0533c2df2adb235ad3646abf7fa31ca3d5`,
G2 `J313_EFI.fd` SHA-256
`3d2a2dd1360c073e8413c1fcebb3d3c072c33c3acfc7f1be27873a75e87b3070`,
and all six exact package hashes already listed by EXP-115.

- [ ] **Step 2: Require fresh evidence and approval**

Require absent path
`investigation/artifacts/EXP-20260826-116-agx-g2-bind-failclosed/` and a new
explicit user approval after the preregistration commit is pushed.

- [ ] **Step 3: Commit the preregistration**

```sh
git add investigation/EXPERIMENTS.md investigation/CHANGES.csv
git commit -m "docs: preregister fail-closed AppleAgx bind gate"
git push origin feature/j313-gpu-acceleration
```

### Task 2: Establish the stable pre-mutation baseline

**Files:**
- Create: `investigation/artifacts/EXP-20260826-116-agx-g2-bind-failclosed/baseline.json`
- Create: `investigation/artifacts/EXP-20260826-116-agx-g2-bind-failclosed/candidate-hashes.json`

**Interfaces:**
- Consumes: stable Windows over SSH and the two read-only candidates.
- Produces: an exact baseline used by every later assertion and rollback.

- [ ] **Step 1: Verify recovery and candidates locally**

```sh
(cd .local/recovery/STABLE-j313-8core-native-input-v1 && shasum -a 256 -c SHA256SUMS)
shasum -a 256 .local/agx-g2-enumeration-candidate-v2/*
shasum -a 256 .local/agx-driver-stage-exp114/*
```

Expected: all five recovery entries report `OK`; every candidate hash matches
its immutable manifest.

- [ ] **Step 2: Capture stable Windows state**

Require eight logical processors, AppleInput `Running`, APPL0001 `OK`, zero
present APPL0002 devices, exactly one non-present APPL0002 Problem-45 ghost,
zero AppleAgx package/service/module/certificates and no critical event since
boot.

- [ ] **Step 3: Recompute the six package hashes on Windows**

Use `Get-FileHash -Algorithm SHA256` and reject before mutation if any identity
differs from EXP-115.

### Task 3: Stage the qualified package under stable firmware

**Files:**
- Create: `investigation/artifacts/EXP-20260826-116-agx-g2-bind-failclosed/stage-output.txt`
- Create: `investigation/artifacts/EXP-20260826-116-agx-g2-bind-failclosed/staged-package.json`

**Interfaces:**
- Consumes: exact certificate and hashed `stage-driver.ps1`.
- Produces: one recorded `oemNN.inf` identity and no active device.

- [ ] **Step 1: Import only the exact signer**

Import the CER into LocalMachine Root and TrustedPublisher, then require
`Get-AuthenticodeSignature appleagx.cat` to report `Valid` and the exact
thumbprint.

- [ ] **Step 2: Invoke only the stage-only script**

```powershell
& .\stage-driver.ps1 -InfPath .\AppleAgx.inf
```

Expected: exactly one new `oemNN.inf`; APPL0002 remains absent; no service or
module is created.

- [ ] **Step 3: Shut Windows down normally**

Use the existing SSH shutdown path.  Do not power-cut, reboot or rescan PnP.

### Task 4: Perform the single G2 fail-closed bind observation

**Files:**
- Create: `investigation/artifacts/EXP-20260826-116-agx-g2-bind-failclosed/hv.log`
- Create: `investigation/artifacts/EXP-20260826-116-agx-g2-bind-failclosed/guest-uart.log`
- Create: `investigation/artifacts/EXP-20260826-116-agx-g2-bind-failclosed/bind-state.json`
- Create: `investigation/artifacts/EXP-20260826-116-agx-g2-bind-failclosed/system-events.evtx`

**Interfaces:**
- Consumes: the recorded package identity and accepted G2 candidate.
- Produces: proof of one driver match and designed StartDevice refusal.

- [ ] **Step 1: Launch G2 once with public assisted tooling**

Use `scripts/run-assisted.sh` with chainload, proxy L41, vUART L43,
`--display both`, `--debug monitor`, and the two files from
`.local/agx-g2-enumeration-candidate-v2/`.  Preserve launch-contract, HV and
UART output.

- [ ] **Step 2: Require the exact failed-start state**

Within 180 seconds require responsive SSH, eight CPUs, healthy AppleInput and
NVMe, one present `ACPI\APPL0002\0`, service name `AppleAgx`, exact MMIO and
IRQs 880..888, and PnP Problem 10.  The failure is the intended result because
`AppleAgxDdiStartDevice` returns `STATUS_NOT_SUPPORTED` only after resource
validation.  Reject any Started adapter, display target, BugCheck, watchdog,
storage reset or input loss.

- [ ] **Step 3: Prove absence of hardware activity**

Require no AppleAgx loaded module after failed start and no AGX MMIO, clock,
firmware, UAT, queue, command, interrupt-injection, power or display-ownership
marker in HV/UART logs.  Shut down normally without retrying G2.

### Task 5: Restore stable firmware and remove the exact package

**Files:**
- Create: `investigation/artifacts/EXP-20260826-116-agx-g2-bind-failclosed/rollback-output.txt`
- Create: `investigation/artifacts/EXP-20260826-116-agx-g2-bind-failclosed/final.json`

**Interfaces:**
- Consumes: immutable stable pair, recorded OEM INF and signer thumbprint.
- Produces: the original stable Windows state.

- [ ] **Step 1: Launch only immutable stable recovery**

Require the stable lock screen, SSH, eight CPUs and healthy AppleInput.  The
APPL0002 device must be non-present before package deletion.

- [ ] **Step 2: Delete only the recorded package**

First invoke the hashed non-force rollback script with the exact recorded
`oemNN.inf`.  Only if Windows reports that this exact package is still in use,
and only after proving APPL0002 is non-present, permit
`pnputil /delete-driver oemNN.inf /uninstall`; `/force` remains forbidden.
Remove only the exact signer thumbprint from Root and TrustedPublisher.

- [ ] **Step 3: Verify complete rollback**

Require zero package, service, loaded module and signer entries, zero present
APPL0002, eight CPUs, healthy input, responsive SSH, unchanged display state
and no new critical event.  Verify all five stable recovery hashes again.

### Task 6: Preserve evidence and record the verdict

**Files:**
- Modify: `investigation/EXPERIMENTS.md`
- Modify: `investigation/CHANGES.csv`
- Create: `investigation/artifacts/EXP-20260826-116-agx-g2-bind-failclosed/SHA256SUMS`

**Interfaces:**
- Consumes: every host, firmware, Windows and rollback artifact.
- Produces: an auditable accepted or rejected gate with no implied hardware permission.

- [ ] **Step 1: Hash every evidence file**

Create one sorted SHA-256 index and record its own SHA-256 in the experiment
result.

- [ ] **Step 2: Run repository verification**

```sh
./proxyenv/bin/python -m unittest discover -s tests
(cd .local/recovery/STABLE-j313-8core-native-input-v1 && shasum -a 256 -c SHA256SUMS)
```

Expected: the complete suite passes and all five recovery artifacts report
`OK`.

- [ ] **Step 3: Commit and push the result**

Record the exact outcome in both journals.  A pass authorizes only planning
the firmware/power ownership implementation; it does not authorize firmware
startup, MMIO writes or a render operation.
