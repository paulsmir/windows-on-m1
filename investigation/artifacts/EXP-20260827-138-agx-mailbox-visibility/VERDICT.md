# EXP-20260827-138 verdict

Verdict: **confirmed**. One corrected hot package transaction proved that the
J313 AGX firmware consumes the Windows driver's RTKit IOP INIT message but does
not produce HELLO.

## Exact package

- GitHub Actions run: `33089519306`
- INF SHA-256: `bdda859faf193db12896ba309fa9f20bd247f8b0520c339d05f23c6d18bed160`
- SYS SHA-256: `841dc5cb713ea3a61731a8b915ec0827c18add102f3de31da515fd3f77d4300a`
- CAT SHA-256: `644111b2583b636c5643e39a62c2595e262cfe642c7e6e7cb78dd66d51c7eeab`
- signer: `8E36CF1EC74F76AB5D6532706C59158914AD37A9`

## Hardware receipts

| Receipt | Raw value | Decode |
|---|---:|---|
| A2I before INIT | `0x00025501` | empty, count 0, RPTR 5, WPTR 5, enabled |
| A2I after INIT | `0x00105601` | non-empty, count 1, RPTR 5, WPTR 6, enabled |
| A2I at failure | `0x00026601` | empty, count 0, RPTR 6, WPTR 6, enabled |
| I2A at failure | `0x00023301` | empty, count 0, RPTR 3, WPTR 3, enabled |

Final state was Problem 43, `STATUS_IO_TIMEOUT`, RTKit phase 1, flags `0x81`,
protocol version 0 and ASC CPU status `0x2d`. The four snapshot-valid bits were
all set.

The pointer transition proves publication and firmware consumption. The empty
I2A FIFO proves there was no unread HELLO. Do not add delay, resend INIT or
change mailbox barriers. The next experiment must inspect a source-backed
firmware prerequisite before HELLO; context-zero UAT roots are first because
Asahi publishes them before RTKit boot.

Eight CPUs and Running AppleInput, stornvme and USBXHCI survived. No critical
event or reboot occurred. A delayed query counted 20 stornvme Event 129
records from the run start, so this transaction is not evidence of clean
storage health and must not be retried.
