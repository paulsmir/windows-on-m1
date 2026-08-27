# J313 AGX Lifecycle Contract Qualification Plan

**Goal:** Prove that the full fail-closed render-only callback contract reaches
the current `StartDevice` boundary in one G2 Windows boot while the package is
compile-incapable of power, MMIO or firmware ownership.

**Architecture:** Keep the EXP-124 G2 firmware and NVMe-safe m1n1 unchanged,
and preserve the exact EXP-123 recovery pair. Install only the CI-signed
`lifecycle-qualification` package. That package exposes persistent lifecycle
receipts but compile-disables the power broker and inert MMIO mapper. After the
first cold G2 boot, further lifecycle-only packages may be replaced in the
same Windows session through the hash-pinned, signer-pinned and device-scoped
runner. The first power, MMIO, firmware, interrupt, UAT, queue, render, present
or display-ownership change still requires exact cold recovery.

## Fixed identities

- Experiment: `EXP-20260827-130`.
- Root branch/head at preregistration:
  `feature/j313-gpu-acceleration` /
  `09f054d7526bd4b96952089cf23f5dc491bf6646`.
- Lifecycle implementation source:
  `6609ab08a046edbe54b4795fda87700ac04e7412`.
- Full fail-closed render-only contract source:
  `fc7132968cc931392d1b845faf10717110c55d2e`.
- Same-boot runner source: `09f054d7526bd4b96952089cf23f5dc491bf6646`.
- WDK run: `33026918148`; default, lifecycle, power and MMIO ARM64 jobs
  passed strict code analysis; every qualification signature check passed.
- Driver SYS SHA-256:
  `2cd6a077c09bbf2cbafbda6baad695aae9b4eb6ec0cc691b48694a904aee2e03`.
- INF SHA-256:
  `408169ecdbadde5e35164a47ea7d7196cbc6b28b7600689299a414543fe6321d`.
- Catalog SHA-256:
  `c205d3ffaef417767f5380502c3773ec82ad9a25305b61a549af15dda2ca480e`.
- Certificate SHA-256:
  `e0104ef99471447bf9ce1231550876ca74f3aa749af0b15ceba391b8a4ac0781`.
- Signature manifest SHA-256:
  `e7c469327488f8e02da396788d354f88b7700604846fd9f681fa3728c65be5ba`.
- Signer thumbprint: `A7847E0FB9AEAF201CD0CA24D9822CBF55632536`.
- G2 manifest SHA-256:
  `02204a6e37a04a323eae05e24b6a35eb7a0c6327b9af98b39d714482d78a0c70`.
- Recovery manifest SHA-256:
  `143fd9aa07f9b224c316c5e23e3993991d7308fa178164beadc785e8dade03f9`.
- Candidate and recovery m1n1 SHA-256:
  `2c39f7723475e6e74fa00b1a88e413ed7e5159a0da1bac5286b6c0442b7d52a9`.
- Evidence path:
  `investigation/artifacts/EXP-20260827-130-agx-lifecycle-contract/`; it must
  be absent or empty before the candidate starts.

## Procedure

- [ ] Revalidate every fixed identity and require exact responsive EXP-123
  recovery with eight CPUs; Running AppleInput, NVMe and xHCI; no present
  APPL0002; zero active AppleAgx package/service/module/signer; zero critical
  events and zero Event 129 through the quiet window.
- [ ] Import only the pinned certificate, stage only the exact package, record
  the generated `oemNN.inf`, prove APPL0002 remains absent, and shut down
  normally.
- [ ] Launch one exact EXP-124 G2 candidate through the public assisted launcher
  with `display=both`, `debug=monitor`, the pinned Mu/m1n1 pair and no power or
  MMIO qualification environment flag.
- [ ] Run `cycle-lifecycle-driver.ps1` against the exact package and signer.
  Preserve its unique JSON result together with the launch contract, monitor
  log and framebuffer metadata.
- [ ] Require fresh DriverEntry stage 2 and successful DxgkInitialize,
  AddDevice stage 2/status zero, and a fresh StartDevice receipt. The expected
  designed endpoint is stage 7 with `STATUS_NOT_SUPPORTED` (`0xC00000BB`).
- [ ] Require every power and MMIO receipt absent; no GPU pointer dereference,
  register access, firmware, RTKit, interrupt connection, UAT, queue, command,
  render, present or display ownership; eight CPUs; Running input/NVMe/xHCI;
  zero critical events and zero Event 129.
- [ ] If this inert iteration passes, additional lifecycle-only source revisions
  may use the same G2 session by supplying the exact prior `oemNN.inf` to the
  runner. Any health failure ends same-boot iteration immediately.
- [ ] Before any hardware-owning profile, shut down normally, boot exact
  EXP-123 recovery, remove only the recorded package and signer without
  `/force`, and prove the final recovery quiet window clean.
- [ ] Publish only a sanitized verdict; raw host and guest evidence remains
  ignored locally.

## Falsifiable result

The hypothesis passes only if fresh current-package receipts cross the old
post-AddDevice boundary and reach the deliberate fail-closed StartDevice end
without any hardware-path receipt or platform-health regression. Missing or
stale receipts, Problem 31 before StartDevice, identity drift, Event 129,
critical event, input/storage/xHCI loss, forbidden GPU action, forced package
deletion or incomplete recovery rejects the experiment. Same-boot iteration is
never evidence for safe power, MMIO or firmware ownership.
