# J313 AGX context-zero root resource contract

## Decision

Windows may access the fixed 16-KiB J313 `gpu-region` only when all three
ownership layers describe the same range:

1. Mu exposes `0x9fffb8000..0x9fffbbfff` as the second AGX0 `_CRS` memory
   resource.
2. m1n1 installs an explicit 16-KiB identity stage-2 software mapping for the
   range in both assisted and autonomous launch paths.
3. AppleAgx accepts that exact translated PnP resource and maps it through
   `DxgkCbMapMemory`.

The source of truth is `config/j313-agx-g2.json`, bound to the accepted G1R
inventory. The contract version is 3 because the Windows-visible resource list
changed. Generated Mu, m1n1 and Windows constants must remain byte-for-byte
deterministic.

## Evidence and rejected design

EXP-138 proved that the ASC consumed IOP INIT but did not produce RTKit HELLO.
Asahi and pinned m1n1 publish context-zero UAT roots before RTKit boot, making
the two root words the next read-only checkpoint.

EXP-139 attempted to inspect them with a private `MmMapIoSpaceEx` mapping. The
range was absent from AGX0 `_CRS`, absent from the translated-resource parser,
and not independently guaranteed by the launch stage-2 contract. The Air lost
SSH and ICMP during the only transaction and no receipt was recovered. The
method is rejected and must not be retried.

## Layer ownership

- The accepted J313 inventory owns the physical address and length.
- Mu owns discovery and Windows PnP resource assignment.
- m1n1 owns guest stage-2 visibility in assisted and standalone launches.
- dxgkrnl owns the kernel mapping lifetime through its map/unmap callbacks.
- AppleAgx owns validation and read-only inspection; it does not invent a
  physical address, acquire GPU power, write roots, start ASC, or submit work in
  this checkpoint.

## Qualification checkpoint

The next hardware package may only read TTBR0 and TTBR1 after all generated
contract, AML, m1n1 host, m1n1 firmware and WDK builds pass. A single package
transaction must produce the UAT receipts while retaining eight CPUs, input,
USB, NVMe and SSH. Any mapping failure, Event 129, bugcheck, reboot, or lost
health rejects the experiment without retry. Recovery remains EXP-123.
