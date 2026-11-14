# Read-only miniport telemetry

This is a metadata query, not an NVMe pass-through or controller-control API.
The driver accepts exactly one private SRB_IO_CONTROL command. It never accepts
addresses, register selectors, resets, writes, counter clearing or arbitrary data.

The transport is documented by Microsoft:
[SRB_IO_CONTROL](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddscsi/ns-ntddscsi-_srb_io_control)
and [IOCTL_SCSI_MINIPORT](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddscsi/ni-ntddscsi-ioctl_scsi_miniport).
The outer IOCTL is Microsoft's IOCTL_SCSI_MINIPORT; its access flags require an
appropriately opened adapter handle even though this private operation is read-only.
The future query tool must discover the running SCSIAdapter interface with SetupDi,
not assume every driver creates a `ScsiN:` symbolic link.

## Exact wire layout

All integers are little-endian. Total input and output size is exactly 508 bytes.
The 28-byte SRB_IO_CONTROL header and each telemetry record are copied to/from
aligned local objects; the wire buffer may be unaligned. Do not cast a combined
C struct onto it: natural padding before a 64-bit payload would change offsets.

| Offset | Bytes | Field |
|---|---:|---|
| 0 | 4 | HeaderLength = 28 |
| 4 | 8 | Signature = ASCII `APANS001`, no terminator |
| 12 | 4 | Timeout in seconds, 1 through 60 |
| 16 | 4 | ControlCode = `0x001b6000` |
| 20 | 4 | ReturnCode = 0 on success |
| 24 | 4 | Length = 480 |
| 28 | 4 | Private version = 1 |
| 32 | 4 | Private payload size = 480 |
| 36 | 4 | Requested sources: 1 (Windows) or 3 (Windows + optional EL2); output is available-source mask |
| 40 | 4 | Reserved = 0 |
| 44 | 232 | Generated `ans_telemetry`, source 1 (Windows) |
| 276 | 232 | Generated `ans_telemetry`, source 2 (EL2), or zero if absent |

The private ControlCode is CTL_CODE(FILE_DEVICE_SCSI, 0x800, METHOD_BUFFERED,
FILE_READ_ACCESS). Kernel compilation asserts the header size and code against
the pinned WDK. Bad signature, version, sizes, reserved data, source mask, timeout
or control code returns SRB_STATUS_INVALID_REQUEST and does not modify the buffer.
There is no blocking operation: only bounded copies and optional software-counter
CSR reads are performed.

## Meaning and validity

The canonical 232-byte record, fields and validity bits come from the generated
broker ABI. Query consumers must check magic, size, source and valid_fields and
show unavailable fields as N/A. Do not merge the records or infer one origin's
interrupt count from the other. Generation is the current/last accepted generation
at that origin; a difference between origins is diagnostic information.

Windows counters are updated under the existing InterruptLock. BuildIo touches
only its request-local failure flag; StartIo accounts for that failure once.

- starts/stops/resets: native adapter initialization/stop/reset attempts, including
  explicit restarts and qualification cleanup; they are not asserted physical
  power-transition counts. Default qualification records its Initialize attempt.
- submitted: confirmed successful broker AUTHORIZE responses for NVMe commands,
  including admin commands. Deferred software tasks and SCSI metadata responses
  do not increment it. Current/max outstanding count those admissions until an
  authenticated CQE and successful REAP. This is not proof of simultaneous physical
  media execution; correlate with the separate EL2 record and qualification traces.
- completed: authenticated CQE + successful matching REAP, including failed NVMe
  statuses and admin commands. Reset retirement is not synthesized completion.
- reads/writes/flushes: confirmed I/O-queue opcode admissions only. Admin Create SQ
  opcode 1 is not counted as a data WRITE. The driver should report zero writes.
- bytes_read/bytes_written: validated successful I/O command lengths only; no
  Identify bytes, SCSI response metadata or failed transfers are added.
- ISR entries and CQEs count actual callback entries and validated cached CQEs.
  Windows spurious_irqs means its ISR could not claim a completion source.
- timeouts count actual admin deadline/poll-budget expiry, not malformed CQEs or
  broker errors. Normal admin polling is not mislabeled recovered missed IRQs.
- invalid CQE/tag, broker rejection and PRP failure counters describe the checks
  performed by this driver. Windows PRP failures include SG-to-logical-PRP failures
  and observed broker BAD_PRP rejection; they are not invented host translations.

Windows clears validity for physical IRQs, guest injection, TCB invalidation failures
and runtime poll-recovered completions because it does not implement/observe those
providers. Counter overflow clears that field's validity rather than wrapping.
An uncertain AUTHORIZE invalidates historical submitted/completed, relevant I/O
opcode, current and maximum-outstanding fields. A real stop receipt reestablishes
known current outstanding = 0; it cannot repair an unknown historical maximum.

If the broker advertises ANS_FEATURE_TELEMETRY, reading the lowest DWORD at its
telemetry offset latches the complete record. Subsequent DWORDs read that same
snapshot. This does not issue a disk command, drain a CQ or read physical registers.
Absent/malformed peer data clears its available-source bit. The driver never maps
an untrusted address to provide diagnostics.

Ratios and rates belong in the user-mode query tool: CQEs/ISR entries is N/A when
the denominator is zero; throughput requires a measured interval and consistent
generations/valid fields. Admin polling explains why early CQEs can precede ISR
entries. These counters alone are not a native-storage hardware qualification.

`tests/telemetry_test.c` covers real counter arithmetic, admin/I/O opcode distinction,
QD high-water tracking, failure byte accounting, overflow/uncertainty validity,
stopped retirement and deliberately unaligned query buffers with guard bytes.

## ANS-DIRECT record (source 3)

In direct mode there is no broker. A query with `ANS_IOCTL_BROKER` returns the driver's
direct-runtime record in the second slot instead: `source = 3` and `generation = 0x544345524944` ("DIRECT").

| Field | Meaning in direct mode |
| --- | --- |
| `physical_irqs` | IOP mailbox messages received |
| `guest_irq_injections` | mailbox echoes sent (syslog LOG, ioreport 0x8/0xc) |
| `broker_rejections` | unknown mailbox messages (counted, not answered) |
| `tcb_inval_failures` | nonzero advisory NVMMU TCB_STAT reads |
| `cqes`, `invalid_cqe` | CQEs consumed and rejected by the direct queue engine |
| `poll_recovered_completions` | CQEs reaped by the 50 ms timer because their interrupt had not arrived after a full period (missed-interrupt recovery, diag 0xAA). The timer reap runs the ISR drain, so each reap also adds one to the Windows record's `isr_entries` (and to `spurious_irqs` if the interrupt routine emptied the queue first); only this field and 0xAA say an interrupt was missed |
| `last_stage` | `enum ansd_stage` |
| `last_error` | bit 0 = faulted, bit 1 = IOP crashed |

Only these fields are set in `valid_fields`.

## UNMAP receipt (control code 0x001b6004)

Same SRB_IO_CONTROL header and signature as the telemetry query, total size exactly 988
bytes (28 + 960). Read-only: it never changes the policy. Payload offsets (little-endian):

| Offset | Bytes | Field |
|---|---:|---|
| 0, 4 | 4, 4 | version = 1, payload size = 960 |
| 8, 12 | 4, 4 | effective mode (0 off, 1 observe, 2 deallocate), Identify ONCS |
| 16, 24 | 8, 8 | fence start, fence end (namespace LBAs, end exclusive) |
| 32 | 8 | namespace blocks |
| 40..64 | 4 x 8 | UNMAP commands, accepted, empty, rejected |
| 72, 80 | 8, 8 | validated ranges, blocks (before mode and fence) |
| 88 | 8 | accepted commands not sent because of the mode |
| 96, 104 | 8, 8 | ranges, blocks dropped by the fence |
| 112, 120 | 8, 8 | ranges, blocks handed to Dataset Management |
| 128..152 | 4 x 8 | DSM commands queued, completed ok, completed with error, not sent |
| 160, 164 | 4, 4 | last NVMe error status, last reject (key<<16, asc<<8, ascq) |
| 168, 176 | 8, 8 | hull of all validated ranges: first LBA, end LBA |
| 184, 188 | 4, 4 | extent count, total ranges recorded in the ring |
| 192 | 16 x 16 | extents (start, end), ascending, disjoint |
| 448 | 32 x 16 | ring: LBA (8), count (4), sequence (4); slot = sequence mod 32 |

## Reject receipt (control code 0x001b6008)

Same header and signature, total size exactly 324 bytes (28 + 296). Read-only. Counts requests the
miniport refuses as not implemented since the adapter was found: SRB functions other than the ones
it serves (kind 1, code = SRB function), SRB_IO_CONTROL requests that are none of its own queries
(kind 2, code = the request's ControlCode, 0xffffffff if the buffer is too short) and CDBs answered
ILLEGAL REQUEST with INVALID COMMAND OPERATION CODE (20h) or INVALID FIELD IN CDB (24h) (kind 3,
code = additional sense code << 8 | opcode; up to 0.4.9 only 20h was counted and code was the opcode). Payload offsets
(little-endian): 0 version = 1, 4 payload size = 296, 8/16/24 totals for kinds 1/2/3, 32 rejects
that found no free slot, 40 sixteen entries of {kind 4, code 4, count 8} in first-seen order.
