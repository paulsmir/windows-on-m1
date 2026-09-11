# J313 AGX G2 ChannelInfo Codec Plan

**Goal:** Encode the exact 17-entry G13/V13_5 `ChannelInfoSet` offline before any channel memory is connected to RegionB or hardware.

**Architecture:** Extend the generated Windows-only inventory with the normal state stride and the special six-state FWLog allocation plus dummy ring. Add a freestanding validate-then-write codec that accepts already-owned virtual addresses and emits only the `0x110`-byte ordered table used by RegionB.

## Constraints

- Preserve the executable m1n1 order: 12 command queues, DevCtrl, Event, FWLog, KTrace, Stats.
- Preserve FWLog's exceptional split: the table points at the dummy ring while RegionB later points at the real combined ring.
- Reject unaligned, low-half, duplicate, dirty, incomplete, or version-mismatched inputs without changing destination or manifest.
- Keep the codec disconnected from `adapter.c`, allocation, firmware, doorbells, interrupts, and hardware.

## Tasks

- [x] Add RED generator, codec, and WDK-project tests.
- [x] Generate exact FWLog state/dummy-ring constants.
- [x] Implement and sanitizer-test the freestanding codec.
- [x] Run the focused and complete suites, commit code and documentation, and push.

## Result

- Code commit: `b3ee04ffd742a554f988df481ad53027fafa9274`
- Focused contract, sanitizer, and package suite: `48/48` passed after the expected RED failures.
- Complete public suite: `692/692` passed.
- The codec is WDK-compiled but has no `adapter.c` or `driver.c` call site.
- No package was built or installed and no hardware state changed.
