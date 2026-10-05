# J313 AGX G2 Channel Graph Integration Plan

**Goal:** Integrate owned normal-channel memory into the shared context-zero UAT graph and encode only the proved ChannelInfo prefix of RegionB.

**Architecture:** The existing initdata graph owns the channel owner, maps all 35 objects with firmware-shared permissions, and invokes the pure ChannelInfo codec on RegionB bytes `0x000..0x10f`. Whole-graph destruction remains the only rollback boundary.

## Constraints

- Preserve one guard page between every data and channel allocation.
- Map all 42 data objects through the same context-zero roots.
- Encode no RegionB byte beyond the exact ChannelInfoSet.
- Roll back all data, channel, UAT table, mapping, and manifest state on every allocation failure.
- Keep `adapter.c`, firmware publication, ASC RUN, interrupts, and hardware disconnected.

## Tasks

- [x] Add RED graph assertions for allocation, mapping, encoding, and rollback.
- [x] Integrate channel ownership and UAT mappings.
- [x] Encode and verify only the RegionB ChannelInfo prefix.
- [x] Run focused/full suites, commit code/docs, and push.

## Result

- Code commit: `73e89138f88d444dc903b9a89d0796c9fa0b6e23`
- Focused sanitizer graph suite passed after the expected RED compile failure.
- Complete public suite: `693/693` passed.
- The graph owns 42 mapped data objects and 46 allocations including UAT pages.
- Only RegionB bytes `0x000..0x10f` are encoded; byte `0x110` and later remain zero.
- The build path remains unreachable from `adapter.c` and `driver.c`.
