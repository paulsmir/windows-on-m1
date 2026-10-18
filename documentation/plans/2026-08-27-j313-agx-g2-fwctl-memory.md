# J313 AGX G2 Firmware-Control Memory Implementation Plan

**Goal:** Add the exact G13/V13_5 firmware-control channel state and ring to the offline context-zero initdata graph and encode the firmware-status object that points to them.

**Architecture:** Generated Windows-only constants pin the executable m1n1 channel sizes. A freestanding validate-then-write codec emits the 0x80-byte firmware-status structure. The existing graph owns two additional DMA-visible zeroed objects, maps them through context zero, encodes their virtual addresses into firmware status and retains the same retryable reverse teardown.

**Tech Stack:** Python contract generator, freestanding C11, ASan/UBSan host tests.

## Constraints

- Match executable m1n1 G13/V13_5 `ChannelInfo`, `FWControlStateFields`, `FWCtlMsg` and ring-count values.
- Keep firmware-control state and ring zeroed before any future firmware start.
- Validate all addresses and the complete destination before writing one byte.
- Keep the graph disconnected from `adapter.c`.
- Do not publish TTBRs, power AGX, start ASC, ring a doorbell, send a message or expose rendering.

## Task 1: Pin the channel contract

- [x] Add RED tests for the exact state, message, entry-count and ring sizes.
- [x] Emit the constants only in the Windows generated header.
- [x] Prove ACPI and m1n1 generated outputs remain byte-identical.

## Task 2: Encode firmware status

- [x] Add RED sanitizer tests for the exact 0x80-byte golden layout and all rejection paths.
- [x] Implement the freestanding G13/V13_5 firmware-status codec.
- [x] Prove a rejected encoding leaves destination and manifest unchanged.

## Task 3: Extend the offline graph

- [x] Add RED tests for two new owned objects, seven mappings and eleven allocations.
- [x] Encode the exact state/ring virtual addresses into firmware status.
- [x] Cover every allocation failure and retryable reverse teardown.
- [x] Compile the codec and graph into the WDK project while proving no adapter call site exists.

## Task 4: Verify and record

- [x] Run focused tests and the complete public suite.
- [x] Commit code separately, record exact hashes in `investigation/CHANGES.csv`, close this plan and push.
