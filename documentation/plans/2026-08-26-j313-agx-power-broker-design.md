# J313 AGX Narrow Power Broker Design

## Purpose

Provide the Windows AGX KMD with a bounded platform transition for the two
clock/power dependencies that cannot safely be exposed as raw PMGR MMIO.  The
broker is control-plane only.  Firmware, UAT, queues, fences, interrupts and
render submissions remain direct KMD/hardware operations.

## Source observations

- The live J313 contract names only `/arm-io/gfx-asc` and `/arm-io/sgx` as AGX
  power dependencies and exposes the ASC and SGX apertures separately.
- m1n1's accepted G1 lifecycle enables `gfx-asc` before `sgx`, starts the ASC,
  boots RTKit firmware and requires a bounded heartbeat.
- Asahi's kernel driver maps typed ASC/SGX resources, starts the ASC CPU by the
  reviewed control bit, and performs steady-state work through firmware rather
  than broad raw MMIO.
- The Windows G2 KMD currently validates `_CRS` and deliberately returns
  `STATUS_NOT_SUPPORTED` before any MMIO.

## Rejected alternatives

1. **Raw PMGR in `_CRS`.** This gives a display miniport access to unrelated
   system power domains and makes malformed writes system-wide.  Rejected.
2. **Host-owned firmware.** Prebooting AGX in Python leaves reset, suspend and
   failure ownership split across the host and Windows.  Rejected for the
   production path.
3. **Per-command hypercalls.** This serializes the render path through EL2 and
   loses native throughput.  Rejected.

## Selected interface

A single 4-KiB synthetic MMIO page implements a versioned register ABI:

- immutable magic, ABI version and capability bits;
- command register accepting only `QUERY`, `POWER_ON` and `POWER_OFF`;
- monotonically increasing guest request sequence;
- host receipt sequence, state, result and diagnostic counters;
- no guest-provided address, path, size, timeout or register value.

The broker state machine is `OFF -> ENABLING_ASC -> ENABLING_SGX -> ON` and
the reverse for shutdown.  `POWER_ON` while `ON` and `POWER_OFF` while `OFF`
are successful no-ops with a new receipt.  Unknown commands, stale or repeated
request sequences, and requests while a transition is active fail closed.

The platform layer is the only code allowed to resolve the two fixed ADT paths.
It enables `gfx-asc` then `sgx`, and disables in reverse order only after the KMD
has stopped firmware.  A partial enable rolls back the completed prefix.  Every
transition has a fixed deadline and leaves a durable result/receipt.

## Windows ownership

`DxgkDdiStartDevice` validates the exact resource contract, requests
`POWER_ON`, waits for the matching bounded receipt, then maps only the generated
ASC/SGX apertures.  Later tasks start firmware directly from the KMD.  Stop and
reset first quiesce firmware/UAT/queues, then request `POWER_OFF`.

No broker access is permitted from submit, fence, ISR, DPC or present paths.

## EXP-118 checkpoint

The first hardware gate exposes the broker but leaves AppleAgx enumeration-only.
It proves only exact ABI discovery, one `POWER_ON` receipt, idempotent `QUERY`,
and normal shutdown followed by cold recovery.  It does not start ASC firmware,
create a UAT root, submit work or claim acceleration.

Failure is any ABI mismatch, missing receipt, wrong transition order, timeout,
unexpected AGX/PMGR access, Windows instability or inability to return to the
immutable stable artifact.
