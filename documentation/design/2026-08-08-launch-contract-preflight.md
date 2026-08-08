# Launch Contract Preflight Design

## Purpose

Standalone boot currently regresses in ways that are difficult to distinguish from retained
framebuffer contents, lost USB telemetry, slow decompression, or an early Windows CPU stall. The
known-good host-assisted launch remains available in `/Users/pavel/windows`; it must become the
measured reference rather than an informal source of expected behavior.

This design introduces a shared, structured launch contract. The assisted launcher records the
golden J313 contract. Standalone boot records the same fields, normalizes explicitly dynamic values,
and refuses to enter Windows when a required pre-guest invariant differs.

## Scope

The first implementation targets the M1 MacBook Air (J313) and the existing eight-core Windows
configuration. It covers the state created by m1n1 and consumed by Mu or Windows. It does not attempt
to prove arbitrary Apple Silicon models, benchmark the guest, or validate Windows after the early
CPU-entry audit.

## Principles

1. The assisted launch is the golden behavioral reference.
2. Assisted and standalone paths call the same C snapshot code.
3. Required pre-guest mismatches block guest entry.
4. Dynamic values are normalized only through explicit, reviewed rules.
5. The checker reports field-level differences rather than a single pass/fail result.
6. Capturing and comparing the contract must not change the launch state being measured.
7. No new standalone image is installed until its offline contract checks pass.

## Checkpoints

The contract is sampled at five named checkpoints:

| Checkpoint | Meaning |
| --- | --- |
| `PRE_HV_INIT` | Firmware is loaded and guest boot data is prepared, before hypervisor initialization. |
| `POST_HV_INIT` | PSCI, vGIC, timers, and base EL2 state are initialized. |
| `POST_MAPS` | Stage-2, PCI/NVMe, xHCI, VUART, and display mappings are installed. |
| `PRE_GUEST` | Final boot-CPU state immediately before `hv_start()` snapshots it for secondary CPUs. |
| `CPU_ENTRY` | A per-CPU audit emitted when CPU0 through CPU7 first enter guest execution. |

The first four checkpoints are blocking. `CPU_ENTRY` is a post-launch audit and cannot prevent the
guest from starting.

## Contract Contents

Each snapshot has a versioned header, target identity, checkpoint identifier, monotonic sequence
number, and payload checksum. The payload contains:

- boot arguments, physical RAM bounds, effective guest RAM bounds, and low-memory alias;
- firmware, ADT, boot-args, and framebuffer regions;
- an ADT content digest and size;
- stage-2 mapping descriptors relevant to RAM and passed-through devices;
- boot CPU and secondary CPU topology, affinity, power state, and prepared launch state;
- required EL2/EL12 register fields, including HACR, MDCR, MDSCR, AMX, AP keys, APSTS, and ACTLR;
- vGIC configuration, interrupt routes, and the AIC-to-vINTID mapping used by hardware devices;
- PCI ECAM, NVMe BAR/backend/interrupt state;
- xHCI, DART, VUART, and display configuration;
- guest entry address and the four initial argument registers.

Secrets and machine identifiers are excluded. Pointer values that are not part of the guest ABI are
never treated as stable identities.

## Normalization Rules

Normalization is schema-driven. A field is strict unless the schema explicitly assigns another
comparison rule.

Supported rules are:

- `exact`: values must match exactly;
- `masked`: only documented architectural bits are compared;
- `relative-region`: base may vary, but size, alignment, containment, and overlap relationships must
  match;
- `set`: ordering is ignored but membership must match;
- `digest`: content is compared by size and cryptographic digest;
- `range`: the value must lie inside an explicitly defined safe interval.

Examples of dynamic fields are heap allocations, DART tables, and the physical framebuffer base.
Examples of strict fields are guest IPA addresses, CPU affinity, virtual interrupt IDs, firmware
entry, and security/virtualization register bits required by Windows.

Every normalized mismatch report includes both raw values and the failed semantic rule. Adding a new
normalization rule requires a schema change and a test; the comparator never silently ignores an
unknown field.

## Data Flow

### Golden assisted capture

1. Build and launch the unchanged known-good tree from `/Users/pavel/windows` using the established
   `chainload.py` followed by `run-with-uart.sh` flow.
2. The shared m1n1 collector emits snapshots at all checkpoints.
3. A host capture tool validates ordering and checksums, then writes an immutable raw capture and a
   normalized JSON representation.
4. The capture is reviewed for completeness and sanitized into the public J313 golden contract.

Historical assisted logs are used to verify that all previously important facts are represented,
but they are not used as the primary source of golden values.

### Standalone preflight

1. Standalone creates snapshots through `POST_MAPS` and `PRE_GUEST` using the same collector.
2. The embedded comparator checks them against the golden schema and normalized invariants.
3. On success, standalone prints a concise summary and enters the guest.
4. On failure, it does not call `hv_start()`. It keeps diagnostics available and prints a stable
   machine-readable failure record plus a readable field-level diff.
5. After guest entry, per-CPU audit records show which CPUs reached guest execution and their first
   relevant timer/SGI events.

## Storage and Transport

The canonical in-memory representation is a packed, versioned binary structure with fixed-width
fields. This avoids depending on formatted log text during early boot. The host converts it to JSON
for review and regression tests.

During assisted capture, snapshots are retrieved through the existing proxy. In debug standalone
profiles they are also sent through the existing USB diagnostic channel. A production standalone
profile performs the same embedded preflight but does not require a connected host.

Local raw captures, machine-specific dumps, and investigation notes remain under an ignored local
directory. The public repository contains the schema, comparator, tools, tests, and sanitized J313
golden contract.

## Failure Behavior

A blocking failure has a stable checkpoint, field path, rule, expected value, and actual value. For
example:

```text
PREFLIGHT FAIL checkpoint=PRE_GUEST
field=cpu[1].actlr.EnMDSB rule=exact expected=1 actual=0
guest_entry=blocked
```

Malformed snapshots, unsupported schema versions, missing required checkpoints, duplicate sequence
numbers, and unknown fields are failures. The checker must never fall back to launching Windows
after an incomplete comparison.

## Testing Strategy

The implementation is developed test-first at four levels:

1. Unit tests for snapshot construction, serialization, checksums, and every normalization rule.
2. Golden tests that compare equivalent snapshots with different permitted dynamic addresses.
3. Negative tests for each blocking subsystem and for malformed or incomplete captures.
4. Hardware validation in this order:
   - capture one fresh assisted golden run;
   - replay and compare the golden capture offline;
   - build standalone and inspect all pre-guest snapshots without installing it;
   - install only after offline checks pass;
   - confirm `PREFLIGHT PASS`, CPU0 through CPU7 entry, and Windows progress beyond the static logo.

Passing host tests alone does not establish hardware success. A standalone build is considered
working only after the hardware run reaches the Windows desktop without a preflight mismatch or an
early CPU watchdog failure.

## Repository Integration

The implementation belongs in the public repository at `/Users/pavel/public_windows`. The old tree
at `/Users/pavel/windows` remains read-only and serves as the golden assisted reference. Shared
collector code is implemented in `m1n1_windows`; host capture and comparison tooling lives in the
repository's `tools` or `scripts` directory. Documentation describes how to regenerate a golden
capture without publishing machine-specific data.

Experimental layout-preserving binaries and raw logs are investigation artifacts, not release
inputs. The public build must eventually produce distinct stage-0 and stage-1 artifacts explicitly
rather than reusing one m1n1 binary for both roles.

## Acceptance Criteria

- A fresh assisted run produces all required snapshots automatically.
- The normalized assisted capture is stable across two launches on the same J313.
- Intentional changes to each required subsystem produce a precise blocking diff.
- Standalone cannot call `hv_start()` after a missing or failed blocking checkpoint.
- Permitted heap, framebuffer, and DART address changes do not cause false failures.
- Stage-0 and stage-1 build identities are recorded separately.
- The first standalone hardware validation reports all eight CPU entries and progresses beyond the
  static Windows logo.
