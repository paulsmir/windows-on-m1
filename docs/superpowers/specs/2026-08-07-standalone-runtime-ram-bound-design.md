# Standalone Runtime RAM-Bound Design

## Problem

The assisted boot path derives the top of normal RAM from the iBoot boot
arguments. On the tested J313 system that value is `0x9df708000`.

The standalone manifest instead contains a static maximum of `0xa00000000`.
The autonomous runtime currently uses that value both when it advertises RAM to
UEFI and when it creates the stage-2 mapping. As a result, the guest is told that
roughly 521 MiB above the actual end of normal RAM is usable. Firmware and the
Windows boot manager can run for a while, but Windows eventually allocates from
that invalid range and may reset, enter automatic repair, or hang.

The display, NVMe, USB, and SMP paths are not part of this change. The current
public assisted build has already reached the Windows desktop on the internal
2560x1600 panel with all eight CPUs online.

## Decision

Treat the manifest RAM end as an upper limit, not as an assertion about the
machine's complete physical memory map.

At standalone startup, compute the platform RAM end from the boot arguments:

```text
platform_ram_end = boot_args.phys_base + boot_args.mem_size
effective_ram_end = min(manifest.ram_end, platform_ram_end)
```

The addition must be checked for overflow. The result must be greater than the
guest physical base. If validation fails, standalone startup must stop with a
clear diagnostic instead of constructing a partial or unsafe guest map.

The same `effective_ram_end` must be used for both:

- the RAM size passed to UEFI in the guest boot arguments; and
- the stage-2 mapping of normal guest RAM.

This keeps the advertised memory map and the actual mapping identical.

## Implementation Shape

Add a small pure helper that resolves and validates the effective end address.
Keeping the arithmetic outside the hardware runtime makes the policy testable on
the build host.

The autonomous runtime will resolve the bound once, store it in its runtime
state, and pass that stored value to boot-data preparation and stage-2 mapping.
No J313-specific address will be hardcoded.

## Tests

Host-side unit tests will cover at least:

1. A manifest end above the platform end is clamped to the platform end.
2. A manifest end below the platform end remains unchanged.
3. Overflow in `phys_base + mem_size` is rejected.
4. An effective end at or below the guest physical base is rejected.

The implementation will be written test-first. After unit tests pass, the
standalone image will be rebuilt and its manifest inspected. Physical validation
will then compare standalone boot behavior against the already validated
assisted baseline.

## Non-Goals

- Reclaiming the current `Hardware reserved` memory.
- Changing the low-memory alias.
- Redesigning the standalone manifest format.
- Changing display profiles, firmware, NVMe, USB, SMP, or Windows installation.

The 2.8 GiB reported by Windows as hardware-reserved is a separate memory-layout
issue: approximately 1.8 GiB lies below the current guest RAM base, and roughly
1 GiB backs the low-memory alias. This fix prevents standalone from advertising
nonexistent high memory; it does not increase the approximately 5.2 GiB currently
available to Windows.

## Alternatives Rejected

### Hardcode the observed J313 RAM end

Using `0x9df708000` would fix one firmware and memory configuration but would be
fragile across machines and boot environments.

### Reimplement the assisted Python loader in standalone

Porting the complete assisted memory-map construction would mix a much larger
refactor into a narrowly identified safety bug. The runtime-bound clamp restores
the essential invariant with a small, reviewable change.
