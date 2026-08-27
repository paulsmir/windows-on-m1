# J313 AGX Powered ASC Status Hardware Qualification

**Goal:** Prove in one fail-closed G2 boot that ASC CPU status is readable only
inside a bounded, confirmed GPU power session, followed by verified power-off
and release of both mappings.

## Fixed identities

- Experiment: `EXP-20260827-135`; one cold candidate execution, no retry.
- Source: `22f52ea2c70ae124cf31fe17d42fcc862b4e52b1` on
  `feature/j313-gpu-acceleration`.
- WDK workflow: `33060903300`; six of six ARM64 profiles passed.
- Artifact: `AppleAgx-ARM64-PoweredStatusQualification`.
- INF SHA-256: `860e397ad0e0c52cf445563eebcc5deea9bcdcb4b7884446af15ade7c1659040`.
- SYS SHA-256: `205374f85a44111c9703ae4f5b6dc2708989935e1bd61a4055f5c4b243545a73`.
- Catalog SHA-256: `65330655f83217c21f683e4818fef5fc9778c3057ad2f2930c0ede54cb919bcf`.
- Certificate SHA-256: `c5f849914871be0cba4d1a3c7a155cab1e3e908a55c29ec4d2d7c4c10df08f07`.
- Signature JSON SHA-256: `40fa2952129ed973fd1c98e49d96abea8978570937bbb93b1fe1523797bf734c`.
- Signer: `794EF33891227194A91530A1F9C33F93C2DE1B9B`.
- G2 manifest / firmware / m1n1 SHA-256:
  `02204a6e37a04a323eae05e24b6a35eb7a0c6327b9af98b39d714482d78a0c70` /
  `70b216c01f3d7acd77b2c24d0a3dc4fa0cccefec631031a413a301d271f6c064` /
  `2c39f7723475e6e74fa00b1a88e413ed7e5159a0da1bac5286b6c0442b7d52a9`.
- Recovery manifest / firmware / m1n1 SHA-256:
  `143fd9aa07f9b224c316c5e23e3993991d7308fa178164beadc785e8dade03f9` /
  `4c5e068f664d8ccc94823880de4226e3f7842e08841bc10fea19cbe9e05a519b` /
  `2c39f7723475e6e74fa00b1a88e413ed7e5159a0da1bac5286b6c0442b7d52a9`.

## Mandatory preflight

The exact EXP-123 recovery boot must report eight CPUs, Running AppleInput,
stornvme and USBXHCI, zero critical events and zero Event 129 since boot. No
APPL0002 package, service, module or pinned signer may be present. Revalidate
every identity above before mutation. Copy the candidate launch contract and
`hv.log` to the ignored experiment directory before any later launcher can
truncate shared logs.

## Single authorized execution

1. Import only the pinned certificate, stage only the pinned package and record
   its exact `oemNN.inf`; clear stale `Wom1*` receipts.
2. Shut down normally and cold-launch the exact G2 pair with `display=both`,
   `debug=monitor` and the G2 power broker enabled.
3. Restart only `ACPI\APPL0002\0` once.
4. Require this exact order: SGX map and ASC subview validation; broker ON;
   QUERY confirming ON; exactly one 32-bit ASC CPU-status read at `0x48`;
   broker OFF; broker unmap; SGX unmap; deliberate stage-7
   `STATUS_NOT_SUPPORTED`.
5. Preserve launch contract, `hv.log`, receipts, PnP state and health evidence
   before shutdown.
6. Boot exact EXP-123 recovery, remove only the recorded package and signer
   without force, and prove the original clean health state.

## Forbidden actions and pass condition

No CPU RUN write, mailbox traffic, firmware start, RTKit exchange, interrupt,
UAT publication, allocation, queue, command, render, present or display
ownership is permitted. Passing requires fresh zero-status receipts for map,
subview, power acquire, one read, power release and both unmaps, deliberate
stage-7 failure, healthy eight-core Windows, zero Event 129 and critical events,
and exact clean rollback. Any missing or duplicate read, abort, power-off
failure, health regression or identity drift rejects and closes without retry.
