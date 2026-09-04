# J313 AGX G2 Channel Memory Owner Plan

**Goal:** Own and zero every normal G13/V13_5 channel state/ring allocation offline and derive the exact ChannelInfo input plus real FWLog address.

**Architecture:** A freestanding owner allocates 35 objects: 12 command states and rings, DevCtrl, Event, FWLog state/real ring/dummy ring, KTrace, and Stats. It assigns deterministic guarded TTBR1 virtual addresses but does not create UAT mappings or publish RegionB.

## Constraints

- Allocate exact logical sizes rounded only at the memory-owner boundary.
- Keep FWLog's real ring separate from the dummy address encoded in ChannelInfo.
- Zero every allocation before exposing any address.
- Roll back in reverse order on each of 35 possible allocation failures.
- Remain disconnected from initdata, DDI, firmware, and hardware.

## Tasks

- [x] Add RED sanitizer and WDK-project tests.
- [x] Implement deterministic allocation, address derivation, and rollback.
- [x] Verify exact ChannelInfo pairing and FWLog split.
- [x] Run focused/full suites, commit code/docs, and push.

## Result

- Code commit: `67187159d0f2ea8337d070c01d41714f0d175e98`
- Focused sanitizer and package suite: `35/35` passed after the expected RED failures.
- Complete public suite: `693/693` passed.
- Every one of the 35 allocation failure points rolled back without an active allocation.
- The owner is WDK-compiled but disconnected from the DDI and UAT publication path.
