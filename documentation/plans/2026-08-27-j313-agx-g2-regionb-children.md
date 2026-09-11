# J313 AGX G2 Fixed RegionB Children Plan

**Goal:** Own, map, and encode every fixed G13/V13_5 RegionB child object without making firmware startup reachable.

**Architecture:** Generated constants pin the executable m1n1 layout. A pure fail-closed codec writes only the fourteen reviewed pointer slots, while a separate owner allocates eleven page-rounded objects. The shared initdata graph maps each object at a guarded TTBR1 address and maps the buffer-manager allocation a second time at the required fixed TTBR0 GPU address.

**Safety boundary:** `adapter.c` and `driver.c` remain disconnected. No GPU power, cache publication, ASC `RUN`, interrupt, queue, or render path is added by this increment.

## Completed work

- [x] Generate exact RegionB object sizes and pointer offsets from the executable G13/V13_5 reference.
- [x] Add a validate-before-write codec that preserves RegionB and its manifest on every rejection.
- [x] Preserve the existing `ChannelInfoSet` prefix while encoding only reviewed pointer slots.
- [x] Allocate and zero eleven fixed RegionB child objects with deterministic guarded TTBR1 virtual addresses.
- [x] Reuse the real FWLog ring owned by channel memory.
- [x] Encode the required duplicate `hwdata_b` pointer.
- [x] Map one buffer-manager allocation at both its CPU/high virtual address and fixed GPU address `0x420000000`.
- [x] Roll back every allocation and UAT-map failure without leaving owned memory.

## Verification

- TDD RED observed for both the missing codec and missing memory owner.
- Focused sanitizer and generated-contract tests: 16/16 passed.
- Complete public suite: 695/695 passed in the repository Python environment.
- Integrated graph: 54 UAT mappings, six UAT pages, 59 total allocations.
- All 59 allocation-failure positions roll back with zero active allocations.
- No package was built, staged, installed, or executed on hardware.

## Next gate

Construct dynamic J313 RegionC/HWData from exact ADT and chip values. Fixed GPU-region publication and ASC startup remain forbidden until RegionC, cache publication, and rollback checks are complete.
