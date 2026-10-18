# EXP866 — sequence progress confirmed; later TDR; recovery complete

One package866 (`17ee8894`) applied only R151 source/tests to EXP865's
`26cbd7e6`, with unchanged R143 firmware and the Flush commit excluded.
The sequence-preservation checkpoint advanced on hardware. The overall
ten-minute stability and visible-frame checkpoints failed.

## Execution evidence

The matching-symbol kernel dump records native backend sequence5 at active
fence955, owned by Explorer PID5272 (context generation `14980002`), node0,
engine0. Both expected stamps are fresh: TA `7a000500`, D3 `3d000500`.
Queue completion760 has `AppleAgxG13QueueCompletionSuccess`, corroborated by
G3 `LastCompletedFence=760`. Scheduler completion954 includes CPU work and
is not a GPU job count.

The persistent queues support **four prior native completions**: TA consumed
five entries (initial initBM plus four jobs), and D3 consumed eight entries
(four barrier/work pairs). Current sequence5, successful completion760, and
the serialized submission/completion contract corroborate this count.
This is an inference from queue history, not a durable per-job counter or a
retained list of all four fence owners. The previous stamp words alone cannot
prove completion because job materialization writes those words.

| Active fence955 | TA | D3 |
|---|---:|---:|
| CPU write pointer / expected done | 6 | 10 |
| GPU done pointer | 5 | 8 |
| Observed stamp | `7a000400` | `3d000400` |
| Expected stamp | `7a000500` | `3d000500` |
| Event seen / complete | 0 / 0 | 0 / 0 |

Current private scene61 is queued, started, not GPU-done, and quarantined;
root `9d6014000`, mapping generation1400. Thus the new boundary is a later
job whose queue consumers do not advance, with fresh completion identities.
The cause of that stall remains unknown; relevant MMIO pages are absent.

The current live watchdog dump is `0x141` at43.540s on CPU3. The kernel stop
is `0x116` at43.603s on CPU0, parameters
`(ffffe2051be8b010, fffff80135f16ce0, ffffffffc0000483, 3)`.
ResetFromTimeout remains fail-closed; it identifies the TDR recovery owner,
not the cause of lost GPU progress.

All eight CPU entries occurred. Durable StartStage12/status0 and consumed arm
are retained. No original SSH/Code0 sample succeeded before spontaneous reset.
One scanout snapshot (sequence2) has zero nonzero pixels. DWM1236 and
LogonUI1228 exist in the dump; no sustained desktop, Present or visible frame
is established. Original USB input and RDP health were not observed. NVMe
initialized and current crash dumps were written. Dump IRQ/ACK counts are1/1,
DPC count956; no runtime timer/vGIC snapshot was injected.

## Receipts and provenance

No QUERY or G4 failure receipt is present. Decoded G3 flush is branch5 with
resolve/broker status0; work input is operation8 and262144bytes. The UMD log
has942lines and four SetError reports (three ResourceCopyRegion, one
DrawIndexedInstanced); their relationship to the active queue stall is unproven.

The original ETL could not be copied before the unrequested reset because
original SSH never returned. The collected ETL is explicitly recovery-only
and excluded from original execution claims. The old EXP865 minidump collected
by the bounded lookback is retained but excluded. Dump headers and matching
PDBs identify the current failure; filesystem timestamps alone are not used
for attribution.

Kernel dump, current minidump and watchdog were preserved before package
cleanup. All27 collected guest files (861605328bytes) matched their hashes on
the host. Four exact recovery receipts were separately fetched and checked.
The final host index has48files (874646503bytes), including serial captures,
derived analyses, recovery state and matching symbols. The watchdog collector
saved its files/manifest before its final hashtable summary failed; independent
verification of the JSON manifest confirmed every collected file.

## Recovery and next boundary

Immutable GPU-hidden recovery reached Code45, exactoem5/package866, arm clear
and service stopped. Evidence-gated diagnostic cleanup preceded exact devnode,
package, driver-file, service and signer removal. Ordered restart then used
immutable ordinary EXP377/392 recovery.

Durable verification at `2026-09-28T13:28:11.7369170Z`, boot13:26:42.0820310Z:
one present inert Code28; no package, INF binding, arms, SYS/UMD, service,
signer or diagnostic settings; eight CPUs, two healthy disks, five USB entries,
RDP running. Intentional autologon is preserved. No rearm or retry occurred.

Result: `.local/experiments/EXP866-r151-stamp/hardware-result.json`, SHA256
`b0e9a3d9a31bca8452c3cf3a20bf007f2f673823ee9d50a5895fffb1f82a75f0`.
Final evidence index: `final-evidence-manifest.json`, SHA256
`5be58cef69dc5993024d7210db3045f6b3a9a6a13f1d43c084811511fd837ea7`.
Tracked summaries and debugger evidence: `investigation/evidence/EXP866/`.

The next thread should inspect the exact Explorer955/sequence5 consumer stall
offline. This verdict authorizes no further package, rearm or hardware run.
