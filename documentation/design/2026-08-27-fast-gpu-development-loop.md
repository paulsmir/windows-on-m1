# Fast J313 GPU development loop

## Goal

Reduce hardware reboots, repeated test execution, log volume, and session context
while preserving the evidence needed to make safe progress on the J313 Windows
GPU driver.

## Development modes

The workflow has three explicit modes.

### Windows hot cycle

Use this mode by default for changes confined to the Windows AppleAgx package or
portable shared GPU code. Build and sign the package, transfer it over SSH,
verify the exact hashes and signer, restart only `ACPI\APPL0002\0`, wait for the
driver's final `Wom1StartDeviceStatus` receipt, and collect a bounded JSON result.
Do not infer success from `pnputil` completion or a transient PnP `OK` state.

A hot cycle changes one hardware-visible variable. It performs one device start
and does not retry a failed state-changing transaction. It preserves the prior
package identity and never uses `/force`. A failed cycle records the exact
rollback identity; rollback is automatic only when a separately reviewed script
can prove that PnP can safely unload the candidate.

### Assisted cold cycle

Use a display-`both` assisted boot only when m1n1, Mu, ACPI, stage-2, DART,
interrupt routing, inherited hardware state, or the boot contract changes. It is
also the recovery path when Windows, SSH, or PnP can no longer perform a safe hot
cycle. Never attach the proxy client directly while the guest is running.

### Standalone qualification

Use standalone boot only at a milestone, before a release tag, before merging a
validated feature to `main`, or when the standalone loader itself changed.
Standalone is not part of ordinary driver iteration.

## Test tiers

1. Run the exact unit test affected by every source change.
2. Run the focused GPU suite before producing a driver package.
3. Run package and signature checks before transferring an artifact.
4. Run the complete repository suite before a significant push, merge, or tag.
5. Run one Windows device cycle for one falsifiable hardware hypothesis.
6. Cold-boot only for a lower-layer change or failed hot-cycle recovery.
7. Standalone-boot only for milestone qualification.

Repetition is justified only when evidence was lost or the result was explicitly
classified as nondeterministic. A failed or rejected hardware result is not
silently retried.

## Context contract

`investigation/CURRENT_STATE.md` is the compact session entry point. It records:

- the last immutable stable recovery point;
- the active repository, branch, and pinned root/m1n1/Mu commits;
- the live boot and installed package identities;
- the last confirmed hardware boundary;
- the current falsifiable hypothesis;
- the single next action and its rollback.

The file stays short and contains no raw logs. Detailed immutable history remains
in `investigation/EXPERIMENTS.md`; machine-readable changes remain in
`investigation/CHANGES.csv`; raw evidence remains under ignored `.local`
directories. A context helper prints only the compact state, repository identity,
preserved dirt, and the tail of the change ledger.

Primary source summaries may be reused only while their pinned source commits
remain unchanged. New hardware boundaries still follow `AGENTS.md`: live state,
Asahi, m1n1, Mu/ACPI, official WDK, then comparison with the stable contracts.

## Hot-cycle completion contract

The runner clears old `Wom1*` receipts before the device operation. After
`pnputil` requests the restart, it polls the device-parameter registry until a
fresh `Wom1StartDeviceStatus` exists or a bounded timeout expires. Only then may
it snapshot PnP state and evaluate health. The result distinguishes:

- package/configuration request accepted;
- StartDevice still pending;
- StartDevice completed successfully;
- StartDevice completed with a deliberate or unexpected failure;
- timeout without a final driver receipt.

Every result includes package hashes, signer, old and new package identities,
receipts, elapsed time, eight-CPU/input/storage/xHCI health, critical events, and
fresh `stornvme` Event 129 records.

## Recovery and stop rules

Stop without another device operation when any of these occurs:

- SSH or the desktop stops responding;
- a fresh critical event or Event 129 appears;
- package, signer, device, CPU, or service identity drifts;
- the final receipt is missing at timeout;
- safe rollback cannot identify exactly one prior package.

Use exact EXP-123 for cold recovery. The validated stable branch and tag remain
the release rollback; experimental driver cycles must not overwrite them.
