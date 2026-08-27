# J313 AGX G2 RegionB Dependency Inventory Plan

**Goal:** Pin the exact executable G13/V13_5 channel and RegionB child-object sizes needed before extending the owned initdata graph.

**Architecture:** The Windows-only generated contract records semantic channel groups and mandatory RegionB child objects. Values come from executable m1n1 constructs, not historical size comments. No allocation or hardware path consumes the inventory in this increment.

## Constraints

- Keep ACPI and m1n1 policy generation byte-identical.
- Represent the six FWLog rings as their one combined m1n1 allocation.
- Distinguish logical content size from 16-KiB allocation rounding.
- Do not fabricate RegionC dynamic power data.
- Do not connect the inventory to `adapter.c` or hardware.

## Tasks

- [x] Add RED generated-header assertions for channel geometry.
- [x] Add RED generated-header assertions for mandatory RegionB child sizes.
- [x] Emit Windows-only constants with validation and derived combined sizes.
- [x] Run focused and complete tests, record exact commit and push.

## Result

- Code commit: `18374e814464413388de7b3d76d653c16a3b7ca0`
- Focused contract suite: `13/13` passed after the expected RED failure.
- Complete public suite: `691/691` passed.
- ACPI and m1n1 generated outputs remained byte-identical.
- No driver package was built or installed and no hardware state changed.
