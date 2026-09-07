# GPU current state

Updated 2026-09-07T20:34Z. Authoritative compact state; read this first after
context reset. Detailed evidence remains in `EXPERIMENTS.md` and experiment-local
archives. Continue in the current main process without implementation agents.

## Hardware-proven baseline

- EXP475: retained-root broker and full production context-0 inventory; application
  endpoints 0x20/0x21.
- EXP477/478: native initdata, FirmwareStart, DC_Init, UpdateIdleTimestamp,
  BackendRuntimeStart=0, Ready, arena/context/queues and cleanup.
- EXP581: first Windows-originated physical TA+3D execution and exact Windows
  fence completion. Completion ingress is polling; physical GPU IRQ is unproven.
- EXP585: typed USC store pair redirects PBE writes to the Windows target.
- EXP586: correct offscreen hardware target contents: 256 pixels `0xff112233`,
  1024 changed bytes, intact guard, TA/D3 done2/2, fence255, NotifyInterrupt/DPC.
  Evidence ZIP SHA256
  `900559100f24fbacd5e46d3e05ec9f66988aa9b3f7a1c22753d446fd974c303b`.

The physical panel remained black throughout EXP586/587 because the current
producer does not issue Present and no new DCP latch is requested. Therefore
`OFFSCREEN_RENDER_TARGET_HW_PROVEN=YES` and `VISIBLE_PRESENT_HW_PROVEN=NO`.
Do not describe the black screen as a completed visible-frame result.

## EXP587 verdict and exact first boundary

EXP587 exact 30.0.587.0 first producer invocation passed: sequence1, exact
TA/D3 completion, fence259 and 256 correct pixels. The second producer invocation
never reached terminal materialization. Windows reset with `0x119/2`; dump stack
is `dxgmms2!VidSchiSendToExecutionQueue -> VidSchiSubmitPagingCommand`, and
argument2 is `STATUS_DEVICE_BUSY` returned by SubmitCommand.

Crash-durable pre-submit receipt localizes the cause before the second job:
`Wom1PreSubmitHeartbeatResult=6` (protocol violation), RX endpoint `0x20`, payload
`0x0042000000000000`. This is an exact AGX application event-wake message, not
the management PONG that the old heartbeat assumed must be the next mailbox
message. SchedulerFaulted then caused a later VidMm submit to return DEVICE_BUSY
and dxgkrnl bugchecked. Thus EXP587 is INCONCLUSIVE for the persistent shared-
object fix; the second job did not reach BuildActiveJob.

Evidence:

- full archive SHA256
  `e709fbb50ba8efb1d3a958561c96cae02cdeb81f5a95feec0ace30cca78443c7`;
- dump SHA256
  `e6c9ef4bfd3c38bb4eef884ec2ea27c55f46c47dbe5fb259f52252e46a4206f3`;
- debugger analysis SHA256
  `41ad56cc3b86882919639bbabe9fa98baf77c827d858bb0352a2f2c89f987a27`.

Exact EXP587 package/service/SYS/UMD cleanup is complete. The Air is currently in
the validated GPU-hidden emergency recovery guest; Wi-Fi SSH
`pavel@192.168.1.37` works. Restore ordinary GPU-visible 377/392 and verify its
Code28/no-package baseline before staging the next package.

## EXP588 — repeated-submit hardware PASS

Commit `5d3367531c2f39d2bba57e795e530d01b98e772a` changes one causal variable:
after sending heartbeat Ping, wait within the same absolute deadline for the
exact endpoint-0 management PONG while accepting only interleaved endpoint-0x20
type-0x42 event wakes. Any other non-PONG message remains a protocol violation.
No event-ring, firmware, queue, submission, completion, display or Present logic
changes.

Deterministic test was RED on the valid wake/PONG sequence and GREEN after the
fix. It also proves endpoint0x20/type0x43 and management Ping remain rejected.
Focused RTKit/backend/render suite: 29 PASS; one pre-existing unrelated cleanup
source-text assertion remains RED.

EXP588 ran once from a clean ordinary baseline and passed both ordered producer
calls. Terminal receipt one: sequence1, fence255, TA/D3 stamps
`0x7a000100/0x3d000100`, done2/2. Receipt two: sequence2, fence258, stamps
`0x7a000200/0x3d000200`, done3/4. Both have ValidMaskff, backend/completion0,
NotifyInterrupt/DPC, clean Ready exit, 256 expected `0xff112233` pixels,
1024 changed bytes and intact guard. Heartbeat result is0 and RX count advances
7 to10. No bugcheck/reset; Event129x2 remains storage telemetry.

This confirms both the EXP588 event-wake/PONG fix and the EXP587 persistent queue
state split. Evidence archive SHA256
`4961be1cd3bfcf8d6c7ee79ad8cdf4f1bedd7cca6364f648bf108e27cc87b742`;
terminal receipt hashes are
`2d1af4c171abbe79fff4000c824f5ceabc4069a4e667ab07e5b1c3d20acd46f4`
and `7e3b3900786c323fc825163d0dcef88c75190a8b1310f1422ec15f22ffb9c0f6`.
Exact candidate cleanup completed and ordinary377/392 is restored: Code28, no
package/service/module, 8CPU/NVMe2/USB5/keyboard1 and no fresh bugcheck.

EXP588 exact 30.0.588.0 pinned build is frozen. WDK/SDK 26100, MSVC 14.44,
KMD/UMD, code analysis, Universal validation, Inf2Cat, TestSign and coherent
version gates passed with zero errors and inherited C28251 only. ZIP/SYS/INF/CAT/
UMD/producer hashes are respectively
`a474b01029f9750cc9a8544007b4f4bdac85ad87bedeecbbe55daf9c8cef6801`,
`482bb7e8b97c4fc9acc47f3c0cc51b48c39769e258f5f21c0c3bda3d490d0d0c`,
`b911dfda0bc91333641024c8206c1c21ce003d3c570e031152ad1259a5902b86`,
`76ac642612f76df64223cb0d62665f6ca18a9e221a6e0ed9f648ea12b9096338`,
`0634b1138f2219601da18527047e95c9941665dda917b6fe238d6efb47e1b94b`,
`b081d56593ea66777c7792e57a3eb42ea0672d06ca35ff5b60d8165d2e55865d`.

## Current next boundary — visible scanout first

The latest operator instruction makes the nearest mandatory result a meaningful
physical-panel image. EXP589 reset support is offline GREEN and its exact signed
artifact was built, but it was never staged or run and is deferred. Do not mix
it into the display candidate.

Source commit `d4b88bc86178db602c73ab2c2c330a8d76533a69` adds the bounded
queue-lifetime restart: only when the backend image is Ready and has no bound/job
state, set its firmware-local Sequence to0 after the old provider is destroyed
and before the new provider/queues start. Windows fence IDs are untouched.
`sequence1 -> sequence2 -> restart -> InitBM sequence1` was RED before the API
and is GREEN; restart during an active binding is rejected atomically. Full
AppleAgx regression is 364 PASS.

Commit `d823de565f2aaf28faef47ecf13a358b681ee07f` adds only a producer-side
qualification mode using the pinned WDK public `D3DKMT_ESCAPE_TDRDBGCTRL` with
`D3DKMT_TDRDBGCTRLTYPE_ENGINETDR`, node0. It does not add a private KMD escape or
alter production render commands. EXP589 will use it immediately after Render,
then run a fresh normal producer after dxgkrnl reset recovery.

Display source commit `63285c06aa76db3f03c50120a35a8b6e841beb29` adds a
dedicated `VisibleScanoutQualification` profile. It uses the existing 56MiB
driver-owned scanout pool, fills offset0 before any Windows primary publication
with frame590 (white border, red/green/blue/yellow quadrants and binary marker),
then uses the existing fixed-panel QueuePresent/D589 path. A single 216-byte
receipt binds CPU/GPA/HPA, broker pool PA, surface offset/geometry/format/hash,
requested/applied/latched sequence, active offset and swap ID. Validation requires
the pool PA to equal the filled surface backing and all three sequences/offsets
to match. No capability, render, RTKit, scheduler, VA or VSync behavior changes.
Relevant tests29 PASS; full AppleAgx suite365 PASS.

Build EXP590 from the exact EXP588 builder base plus only this display overlay,
excluding EXP589 reset files. One natural bind must leave the diagnostic marker
active after exact A408/D589 and preserve the receipt. Machine evidence alone
does not prove the physical panel; after it is latched, request only visual/photo
confirmation if direct physical observation remains unavailable.

EXP590 R1 was a builder-only failure before artifact creation because the base
normal profile omits existing submit-diagnostic declarations. Commit
`1dc525fa384794b1ebea47f4d0c007e42c363f2d` makes the visible profile inherit
those already hardware-used receipts; no runtime submit behavior changes. R2
exact30.0.590.0 build/sign/Universal gates PASS. ZIP/SYS/INF/CAT/UMD hashes:
`87c426c8895c2152782058c6ff5efb3f5116c1aabe8e872234911e7fdf842710`,
`0ea3c5565319d7cc09cc31f74214584a7025a61d1c844afbb3b42f39a81193de`,
`d48e09cb68a98ec21445ae4e81f612b4d2e6f989983d4c87aed64feccbd9a9d7`,
`8e036e35c6254e1f6781bed9a154ce2cef881e3eda63b8e99994d584da00015e`,
`0fa35f0b5f7012029c58310fcfea0c078da4d1205fd1c6332c9cd51878abb9eb`.
Ordinary377/392 remains clean and running. Stage R2 only.

EXP590 R2 hardware is INCONCLUSIVE before visible proof. The driver filled frame590
and host logged exact A408/D589 swap9, but the receipt ended stage3 with
`STATUS_IO_TIMEOUT` after3403ms; StartDevice unwound to Code43. The latch deadline
was incorrectly based before the full15.6MiB pattern fill/hash. Evidence ZIP SHA
`08d6ec03b7e02375c566228429a67ef07dc9c0baeb548a1dd6016f1350c3333f`;
receipt SHA `ba5ff133780a41187678fbfb69fafc4040eb0624625c01e85f9110bd9fa419de`.
Exact cleanup and ordinary restore completed. Commit
`df2284e5e3afbfc4093fbb403f8515fd6fc94476` moves only the existing2s deadline
start to immediately before QueuePresent. Next EXP591 reruns frame590 once.

## Standing constraints

- Preserve retained-root/broker, platform, memory, display, scheduler and AGX
  backend layers unless direct new evidence reaches them.
- One causal variable per hardware experiment; preregister and hash every build.
- Event129 is storage telemetry unless a reproducible GPU causal link appears.
- Do not touch the separate native-ANS worktree.
- Failed experimental packages are removed after evidence. At the final complete
  accelerated OpenGL/CS1.6 PASS, leave the known-good driver installed and active.
