# EXP640: bounded noncached output reads

## Root evidence

EXP636 and EXP638 kernel dumps independently stop CPU4 in the same byte loop
inside `AdmissionTerminalReceiptCaptureOutput`, with IRQL zero and the guest
IRQ mask clear. In both runs the GPU/fence and full pixel comparison already
completed; the second sequential byte pass was hashing a 16 MiB surface.
EXP637 runs the same helper sixteen times on CPU4 over user-mode noncached
section memory and passes. EXP639 moves the work to a driver-owned thread but
still resets while processing frame4, rejecting the shared worker pool as the
root cause.

The remaining adjacent difference is the backing: the production surface is a
DXGK contiguous physical memory object explicitly created with
`DXGK_MEMORY_CACHING_TYPE_NON_CACHED` and mapped through
`DxgkCbMapPhysicalMemory`. One output operation performs two uninterrupted
passes over that mapping and takes about nine seconds.

## Minimal correction

Preserve the exact pixel, poison, mismatch, guard and FNV-1a result. Add a
portable progress form of the existing verifier. It processes at most 256 KiB
between progress callbacks in both the pixel and byte-hash passes. It builds
the output fields in a local receipt copy and publishes them only after every
range succeeds, so cancellation cannot expose a partial valid result.

The Windows callback runs only on the driver-owned PASSIVE thread, rejects
stop/reset, and performs a one-millisecond non-alertable relative delay. At
the observed throughput, each uninterrupted physical-memory read interval is
well below the prior multi-second loop; 128 progress points add bounded delay
while preserving the existing 15-second producer wait budget. No scan occurs
under a spin lock or in completion/DPC.

The callback is qualification-only. AGX commands, hardware completion, Windows
fence notification, DCP presentation, allocation identity and ownership are
unchanged. The dedicated thread from EXP639 remains because it independently
satisfies Microsoft's system-worker contract.

## Proof

The executable helper test was RED before the new progress API. It verifies two
literal progress boundaries, exact hand-derived pixel/hash result, and atomic
abort with no `VALID_OUTPUT` publication or partial counters. The full render
suite is 122 GREEN. Pinned WDK build/sign/Universal gates remain mandatory.

EXP640 runs once on the release platform with the unchanged sixteen-frame
producer. PASS requires all sixteen exact outputs/latches, HOLD, explicit
owned-primary retirement and teardown. If `0x101` repeats, preserve the exact
chunk/phase via the existing output records and dump; do not stack another
timing change. Restore the clean ordinary baseline after evidence.
