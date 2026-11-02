# Q13 / EXP2100: fatal RTKit reset boundary

Offline diagnosis of the supplied EXP2100 dump, source base
`3d8c6dfba80c6ab6da903937cd05f6f31a916356`. Two cdb rounds are exhausted.
This change improves reset evidence. **It does not fix the firmware crash or
make a fatal two-flight hang recover without a bugcheck.** No hardware, guest,
builder, package, firmware or other worktree was accessed.

## Conclusion and evidence

Dump paths are relative to `.local/handoff/codex/q13-gm-depth2-tdr/` in the
shared handoff directory. Driver paths below are under `drivers/apple-agx/`.

| Conclusion | Evidence |
|---|---|
| Reset failed immediately at RTKit drain, before provider polling. | `cdb-output-1.txt:365` has three failed resets, `Quiesce=4`, `WaitMs=0`, `Stage=2`; `:1140` has `PollFailure=0x01000008`. `shared/include/apple_agx_rtkit_session.h:17` defines result 8 as FirmwareCrashed. At the task base, `render-admission/src/backend_platform_windows.c:7783` returns hardware error at this drain; `scheduler_windows.c:610` propagates it. |
| Active pair is render with compute prelude `0x182a` -> compute `0x182b`, PID `0x270c`, COMPUTE_ON_3D. | `cdb-output-1.txt:1202`, backend/provider/runtime identities `:671`, `:904`, published notifier flights `:1373`. Kind/barrier values: `render-admission/include/render_job_window.h:37`, `:145`. |
| The snapshot does not support a missing-kick-only or lost-CPU-completion-only explanation. | Both jobs remain published. TA done/newest endpoint is `0x107/0x109`; 3D `0x226/0x228`; compute `0x2d/0x30`. RTKit's fatal result independently blocks polling. This does not exclude an earlier command/barrier bug triggering the crash. |
| Round 2 shows the intended dependency chain, not a proven self-deadlock. | `cdb-output-2.txt:141` set A is `0x182b`; `:194` set B is `0x182a`; `:333` A's compute barrier waits on 3D `0x3d0b1400`; `:338` B's TA barrier waits on compute `0x182a`. Base `backend_platform_windows.c:3160` constructs the terminal dependency; `shared/src/apple_agx_render_shared_memory.c:815` separates the job words. |
| The firmware assertion and original stall cause remain unknown. | Round 2 `:99` malformed session command, `:113` malformed TA stamp read; `:346` through `:382` invalid ring-pointer expressions (following zeros are stale-temporary reads, not ring evidence); `:428` through `:520` zero FWLog samples; `:523` empty Windows crashlog object. No third round. |

The corrected readable firmware words are 3D `0x3d0b1300` and compute `0x1827`
(round 2 `:118`, `:125`). Threshold `0x1651` at `:122` is not a retirement proof.
Old unused barrier bytes and the saturated RcProgress history are not evidence
of this pair's publication. Last-good depth-one GravityMark and synthetic
CPU-completion stalls do not establish fatal RTKit crash recovery.

## Why there is no functional reset patch here

`shared/src/apple_agx_rtkit_session.c:266` preserves a previously observed crash;
`:290` latches an endpoint-1/type-1 runtime crash message after provisioning.
That protocol is separate from a firmware Fault/Timeout event. Local primary
Asahi `drivers/gpu/drm/asahi/gpu.rs:378` marks RTKit crashes fatal and fails jobs
with NoDevice. Its `:1058` recovery resumes only an observed halted firmware.
Neither path supplies a retained-firmware reboot or a Windows reusable-adapter
contract. Base `scheduler_windows.c:534` explicitly records that RTKit stop
cannot be undone in this path.

Microsoft's [ResetFromTimeout contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_resetfromtimeout)
requires that GPU memory access has stopped before return. Windows can then
purge/unmap allocations. With the observed endpoints still unretired, clearing
the RTKit latch, synthesizing completion or returning success would release
memory without that guarantee. Ordinary queue recovery already tolerates
missing success stamps *after* physical retirement
(`shared/src/apple_agx_g13_queue_runtime.c:914`). Relaxing stamps or extending a
timeout cannot fix an immediate RTKit drain failure.

KMD owns publication, DMA lifetime, original fences and reset; firmware owns
execution/retirement; the retained broker owns crashlog backing. m1n1/Mu sources
are absent in this worktree, and this patch makes no new assumptions about
power, routing, ACPI, mappings or inherited launch state. No external code copied.
The handoff README's allowance to state why no correct recovery change exists
is used. The requested no-bugcheck recovery objective remains open.

## Diagnostic change and verification contract

`Wom1GpuFaultPostResume` v3 preserves the 184-byte prefix and appends an 80-byte
CPU-owned snapshot: RTKit running/ready/crash grant/latch, last RX and count,
valid-bit-qualified mailbox words, crashlog GPU address/size, and both pending
fence/kind identities. Capture runs under the existing idle-worker reset owner.
No extra MMIO, crash-buffer dereference, queue mutation or reset-success change.
A dirty destination cannot leak stale suffix fields. v1/v2 remain decodable;
missing capture is unknown. Last RX is not necessarily the cause of a transport
error, and a post-resume-named record does not imply a resume occurred.
The diagnostic schema changes on depth-one resets too; execution defaults do not.

Regression: `test_g4_depth2_rtkit_crash` runs real RTKit, platform/provider,
queue, backend and scheduler code with modeled transport/firmware. All 3x3 typed
pairs plus one-flight controls retain ownership across three crash observations;
the real capture emits their metadata. Its reset case runs the actual reset DDIs
against a real published RP/C queue pair: two engine attempts then adapter reset
fail at RTKit with both fences/owners/scenes retained and no provider poll.
The original production capture fails the metadata assertion (RED); the new
capture passes (GREEN). Separate capture/decoder tests cover dirty storage,
exact wire layout, legacy versions and invalid lengths. These are safety and
diagnostic tests, not a simulation-based claim of physical recovery.

Targeted checks: 16 tests PASS, including the real reset-DDI pair replay,
RTKit/session, two-flight fault suffix/drill recovery, stalled recovery and the
no-user-CRT KMD header gate. Required default baseline:

```text
ran 816 tests in 329 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

The last added two-flight reset case was also run in the final targeted set.
Independent review found no critical/important defect and separately passed all
7 diagnostic tests. Native WDK `/W4 /WX` compilation remains an integration gate.
Commit IDs, exact commands, logs and source SHA-256 manifest are in the handoff
`REPORT.md`; no hardware validation is claimed.

## Integration-owner hardware checkpoint

No run is performed or authorized here. This diagnostic branch does not qualify
depth-two GravityMark for release. At most one explicitly preregistered diagnostic
rerun may distinguish the crash-channel notification from a protocol issue; it
cannot recover or reveal a firmware assertion without a mapped crashlog.
If fatal RTKit is confirmed, do not repeat another receipt-only run: investigate
the retained broker's crashlog/stop/rebootstrap contract in its owning layer.

Pin exact source/package/manifest/firmware/recovery hashes; retain accepted power,
soft-fault, pool, signer and class-mask settings; clear all drill arms and old
receipts. Run bounded Q3RR/Q3RCR controls first. GravityMark order is **HD before
FHD**: D3D12, 100000 asteroids, temporal 0, windowed, ValidateRefs=1, Timeout=300,
1280x720 first, 1920x1080 only after correct HD completion and later DWM/fresh work.
Compare same-package depth-one and the preregistered EXP2100 depth-two mask.
Do not escalate after any crash, refusal or missing evidence. A future recovery
patch must first pass a genuine two-flight physical fault/hang checkpoint with
stopped/retired DMA and exact-once original fences; Q12 CPU stalls are insufficient.

Collect v3 PostResume, GpuReset, firmware fault/recovery, EventDrain, typed pair,
fence/queue receipts, ETL, app logs, correctness/liveness and any dump before exact
package/devnode cleanup to ordinary GPU-visible Code28. Preserve the established
emergency-hidden/dump-first recovery rules. Full command shapes and stop criteria
are in the final section of handoff `DESIGN.md`.
