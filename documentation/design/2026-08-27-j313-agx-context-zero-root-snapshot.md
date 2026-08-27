# J313 AGX context-zero root snapshot

## Decision

Before publishing UAT tables or changing RTKit boot, add one opt-in,
read-only Windows qualification profile that records the two inherited
context-zero root words from the fixed J313 `uat-ttbs` region. The profile
must not power AGX, start ASC, send a mailbox message, allocate UAT pages,
publish roots or register render callbacks.

## Evidence and ownership

- Live EXP-138 proved that Windows publishes IOP INIT and firmware consumes it,
  but firmware emits no HELLO.
- Asahi Linux commit `77cb8f24c2381a8abb7272d7bbdec548d6426a8a`
  constructs UAT context zero and stores valid TTBR0/TTBR1 roots before RTKit
  boot.
- Pinned m1n1 follows the same ownership order: UAT exists before the ASC boot
  request.
- Mu exposes SGX MMIO and the synthetic power broker to Windows, while m1n1's
  live config snapshot identifies the fixed `uat-ttbs` base as
  `0x9fffb8000`.
- Microsoft documents `DxgkCbMapMemory` for translated resources assigned to
  the display adapter. The fixed `uat-ttbs` RAM is not currently in AGX `_CRS`,
  so the diagnostic uses a private read-only `MmMapIoSpaceEx` mapping. A
  production publisher must first receive an explicit resource contract; it
  must not reuse this diagnostic shortcut.

Ownership remains unchanged: m1n1 reserves and passes the region, Mu describes
the device, the Windows diagnostic only observes the inherited bytes, and GPU
firmware remains stopped.

## One observable variable

The only new hardware observation is the pair at offsets `0x0` and `0x8` of
the fixed GPU region. The driver records mapping status, both 64-bit words and
a validity classification, then unmaps and returns fail-closed
`STATUS_NOT_SUPPORTED`.

The shared reader is tested to prove byte-for-byte immutability, exact map and
unmap counts, map/unmap failure behavior and pair classification. The Windows
profile is separately tested to prove it cannot call power, ASC, RTKit,
initdata, UAT publication, interrupts or render DDIs.

## Falsifiable result

- Both words zero or invalid: the missing context-zero UAT prerequisite is
  confirmed; the next change may build and publish owned roots before RTKit.
- Both words valid: the hypothesis is rejected; proceed to the next Asahi
  pre-HELLO prerequisite without touching UAT publication.
- Mapping failure: inconclusive; add the fixed region to the Mu/ACPI resource
  contract before any further Windows access.

No reboot is required. Exactly one corrected device hot-cycle is allowed after
the package, signer and stable-system preflight pass. Rollback is removal of
that exact package; EXP-123 remains the cold recovery.
