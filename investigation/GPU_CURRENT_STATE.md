# GPU current state

Updated 2026-09-08T00:13Z. Authoritative compact state; read this first after
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

EXP591 exact30.0.591.0 is the first physical-panel proof. Receipt stage4/status0
binds frame590 hash to pool hostPA, requested=applied=latched sequence2, active
offset0 and swap10. Operator photo visibly shows the exact white border,
red/green/blue/yellow quadrants and marker for several seconds. Therefore
`VISIBLE_SCANOUT_HW_PROVEN=YES`. Photo SHA
`a225ee18d917252b252b4a156dffa031465f0aa8c35ecf86dd3dc86626ef4fad`;
evidence ZIP SHA
`502e6e26553dffaeec7449480cfc9d450c9d2ce6a5906c05883434bee0462f6a`.
Windows then naturally queued its primary at local offset0 and host logged exact
swap11/D589; the panel became black. Thus DCP/address/format scanout is closed,
while `VISIBLE_AGX_RESULT_HW_PROVEN=NO` and `WINDOWS_DESKTOP_VISIBLE=NO`.
Next: exact cleanup, then display the proven AGX16x16 result through a controlled
CPU-assisted transfer to an inactive full-size pool surface.

EXP592 source commit `faaf98323326b4346f6c670cd315cfbe2e506063`
implements that exact B discriminator. After terminal receipt proves all256 AGX
pixels, it nearest-neighbor scales only those completed pixels into pool surface2
offset0x1f40000, after verifying that the destination is not the active surface
and does not overlap the AGX allocation. It then uses existing QueuePresent/D589.
The receipt binds source GPU/PA/hash/prefix/fence to destination CPU/GPA/PA/hash
and exact applied/latched/active sequence. This is explicitly CPU-assisted;
`FULLY_ACCELERATED_PRESENT=NO`. Build/test next from EXP591 base.

EXP592 R2 is REJECTED. Host logged Windows swap9 and qualification swap10/D589,
then CPU5 entered a repeating data abort at FAR0x204017030 before producer return;
SSH was lost. Exact R2 disassembly maps faulting RVA0xe4a4 to
`AdmissionCaptureQueueFaultSnapshot` reading `SgxBase+0x17030`, not scaler memory.
The direct hook also blocked normal fence retirement while scaling/hashing/latching.
Two validated emergency recovery attempts reached proxy but failed secondary CPU
startup; a physical power cycle is now required after offline work is complete.

Commit `b6a65d7b793cdf0e21e1cef1fc52813b6b827db1` prepared EXP593:
copy only1024 validated AGX bytes before allocation release; finish real Windows
completion/fence normally; then scale/present from the owned copy. Delayed SGX/
queue diagnostics run only while backend phase is Submitted. Its additional
qualification-only reservation reduced the advertised Windows local allocation
range to offset0x1f40000 while broker retained the full pool.

EXP593 exact30.0.593.0 pinned build is ready. ZIP/SYS/INF/CAT/UMD/producer SHA256:
`0fb7a4af48e209c9dcc162ad96ea106d1ddadf7c42c78f35925e49a00781dc3e`,
`e10fa52ddc6683205b386be82192c1492049f254a334480724e7107c33c15c7b`,
`c1dbf4e051c2fdfc02e659ad3515317161b2201d7227a9af111c092c01cf522a`,
`b456e9d6f3a6c317bf97ce730fe715545bf233a095c1bc9b8f6a4b17523ad2b7`,
`7c565fb44ada084b96d2c3a64b99d96fc7c922070777c88419716d18cf433f72`,
`25181fa8f1e529d751b2a4fb1a40dae40f44639e58abe0c8c7e8bc9e3ae7b516`.
EXP593 is REJECTED before producer/AGX. Natural bind triggered bugcheck10E/B;
Arg3 was `STATUS_INVALID_ADDRESS`. Exact dump stack is
`dxgmms2!VIDMM_GLOBAL::CompleteBuildPagingBufferIteration -> MemoryTransferInternal
-> TransferToSystem -> EvictResource`. The qualification-only segment shrink
made otherwise valid VidMm paging exceed the advertised local segment. This is
not a verdict on post-fence transfer. Dump SHA
`f1fb377b3a6545af1d6e7085a514b55bf295c938603733b7fe87be094f1849c0`;
analysis SHA `3c9447881fa015d1a32cde82e732a417530d217059d9267beb6e312fce9553f7`;
host log SHA `1342b1900dc7a04e2a008d7a112c470c76ea98d6fa8dc6ecb0d1fe1b9f10cba6`.
Exact oem5/service cleanup completed in compatible hidden recovery; Windows is
currently healthy with8 CPUs in that recovery guest.

Commit `43ae915c3e451eaa875bc2dcb4e982d3238f9221` prepares EXP594 without changing
segment topology. The producer creates and keeps resident an explicit second
2560x1600 BGRA Windows allocation. Patch validates its exact segment2 placement,
size/format and non-overlap, captures its production local-memory identity, and
the existing post-fence copy scales into that allocation before the proven
QueuePresent/D589 path. The visible receipt binds the allocation token. Focused
tests18 PASS; full AppleAgx suite366 PASS. Build from EXP591 base with the
post-fence/safe-diagnostic changes and no dormant EXP589 reset.

EXP594 exact30.0.594.0 pinned build/sign/Universal/version and producer gates
PASS. ZIP/SYS/INF/CAT/UMD/producer hashes are
`a784cd7abfc1066b939a041e9395bbf6832e20bdd92bb5ed20a0520b437bfa8d`,
`ca0a0fea5fcf6f85b69e4fe0539e479a1c4756c31557346b0498ea344becfff1`,
`c04cc3c7d25e912cea67e0736eb73218dc4d41bd100c69a128ebea23c4dfd24f`,
`bbaa0f23aab36e82f0d5aa55be763ad5eaa0006fd865d4ce7b6c0258be3523e6`,
`ab97ce4abe5ff1affadb5a03b6073263b3c2fd97cf8b7f096b41bf36fcaf749d`,
`23671e3697876d7be4fab18e4395d371600949941adfd4a76e0a84091d1c2614`.
Next: graceful hidden shutdown, ordinary377/392 clean preflight, then one exact
EXP594 natural bind and producer.

EXP594 R2 reached an exact boot-time natural bind: Code0, oem5/SYS hashes match,
service Running and8 CPUs. Its one producer stopped at `D3DKMTCreateAllocation`
with `STATUS_INVALID_PARAMETER`; Render/Patch/Submit/AGX were not reached.
Source identifies the exact owner: `AdmissionDdiCreateAllocation` has a tested
single-allocation contract, while the EXP594 producer supplied NumAllocations2.
This rejects only batching both independent allocations into one call. Host log
SHA `194f230d6d5a3cc134e88766fb8de921ae0cfa473eef21651cc72af04bb75356`;
producer log SHA `527e0b8686e1322f52c93d73e9f999a02557d4a3d7fd1c6ae93401e2f3775cd0`;
evidence JSON SHA `7de2ef7fbf90e5e4c7af5fb7b52cda53533b71f07be82e676230bc3eb8a8623f`.
Exact package cleanup completed.

Commit `48bb3be603729c0dc5ea4e26a6dcadadaa0962ce` prepares producer-only EXP595:
create the 16x16 target and full-size visible destination with two separate
standard single-allocation calls, then keep the proven joint residency/render
list and synchronous lifetime. KMD/package remain byte-exact EXP594. Focused9
and full AppleAgx366 tests PASS. Build/hash only the producer on FRYZZING, then
persist cleanup, restore ordinary, stage exact EXP594 package and run one EXP595
producer through the boot-time natural bind.

EXP595 producer-only FRYZZING build/code-analysis PASS with zero warnings/errors.
Producer source SHA
`0c07d6fb0e575029c094df6c24ef52a2861b624836c84d5767dca6cb1ca7b365`;
producer SHA
`3e1a0a0a144711d5cd773a63985a0fe8f8256bcf6bc2e9c84cc532bb60fcce61`.
The exact signed EXP594 ZIP remains unchanged at
`a784cd7abfc1066b939a041e9395bbf6832e20bdd92bb5ed20a0520b437bfa8d`.
Next: persist current exact cleanup with graceful restart, verify ordinary clean,
stage unchanged package plus EXP595 producer, graceful restart, natural bind/run.

EXP595 hardware CONFIRMED the separate-call fix. Both allocation statuses are0;
MakeResident completed through paging fence7002, Render returned0/queued1, and
terminal receipt ValidMaskff proves physical TA+3D, expected/observed stamps
`7a000100/3d000100`, done2/2, exact fence/completed `0x100`, NotifyInterrupt1,
DPC1, all256 output pixels `0xff112233`, changed1024 and guard0. Terminal SHA
`1222418965bab344c41089c44d4f03994a22159b257e30bad3fe041a2281157f`.
The post-fence visible function was called but its receipt is stage0/status
`0xC0000483`; fence/source GPU/PA are present while all captured-destination
fields remain zero. No scale/QueuePresent/D589 occurred. Host/run/evidence SHA:
`40da8ad28b93c2b14e1cdecebff5d7f4e53c4f78a93c2ada0035eaba543884d8`,
`8c1fa6b333ffdb5cd944638c5e1ed5903309304849b7f47269886971a165e650`,
`454a6f447d7f3391c4f7ef792d0b931c0dcdc3ec7192e74a2c690a839359604e`.
Exact package cleanup completed; one Event129 remains storage telemetry.

Commit `ce9d1151fea1c81c59cfdedd4cd85dd7a92f0a8b` prepares EXP596 with only a
crash-durable early-guard discriminator. The receipt records argument, panel,
captured-destination, scanout-view, range, identity, active-surface, scale, queue
and completion boundaries plus captured-valid/fence. It does not change mapping,
AGX, fence or DCP behavior. Focused12 and full AppleAgx366 tests PASS. Build
exact30.0.596.0 from EXP594 package source plus only these receipt files; reuse
the EXP595 producer.

EXP596 exact30.0.596.0 pinned build/sign/Universal/version gates PASS with the
existing inherited C28251 only. ZIP/SYS/INF/CAT/UMD hashes:
`551623fdb50632915567b958a91d351d8f8f91e9e9b5baafddf20b8eeee2c281`,
`a8c98efc51f0ab28fd776db3670b34b0f86fdbead645cfc5160fb3ca505d789e`,
`b12a6377cec1fa357e05903613d7b153ed4321ad0e4f04d410fbee9de08eabfe`,
`d3a46428c65fb1a493b0f453341d5a9c3c820765b0b681e179011fc6ef6b2524`,
`f906ead852184601b60ca9a58c4f815ea2102eb425a2887cd353275ba1b40ac1`.
Producer remains exact EXP595 SHA `3e1a0a0a...fcce61`. Persist current cleanup,
ordinary clean stage, graceful restart, one boot-time bind and producer.

EXP596 exact natural bind/producer again proved TA/3D/fence. Its new receipt
locates the first visible failure exactly: Guard3 `Panel` passed, then
CapturedValid0/CapturedFence0, so the consumer had no destination. It did not
prove that already-written global state was lost; EXP597 later showed the actual
prepatched route never performed ordinary Patch capture. Visible receipt SHA
`160bcffdd3c6b5f162270fd353d472ccd84b8224976eebd411cb0e8344dafa93`;
terminal SHA `a9f48927fb0c0d366fb9d034382795742dd2268b9218de20446464a5135ba336`.
Exact cleanup completed; Event129 remains storage telemetry.

Commits `af19d5a2284bf775e0ce01c5e44feded0cba9d25` and
`3a9cb758f11d4cbadba5e3374b2bb02e009db01c` prepare EXP597. The visible
destination identity is now part of the exact render packet description, matched
through its state machine and copied into the worker before completion clears the
packet. Post-fence display consumes that worker-local description; global adapter
scratch is removed. Focused14/full366 tests PASS. Build from EXP596 base, reuse
EXP595 producer.

EXP597 R2 exact30.0.597.0 build/sign gates PASS (R1 was builder syntax only).
ZIP/SYS hashes `fc1b3b28511a18c58859e39e7a82fc1265b7e5da795b64a5df2e5aeec37dc6ae` /
`e201e55d1c0554a7ee9e4523bca7d3812e7b1b78cce7e1f56b47c52e39858436`.
Hardware: natural bind, producer and TA/3D/fence PASS. CapturedFence256 confirms
the per-fence packet reaches the worker; packet carry itself is not rejected.
Guard3/CapturedValid0 instead proves its destination part was never populated:
the actual prepatched-adoption route passes NULL, and ordinary Patch capture is
not the route. Receipt SHA
`5049941236a38bc1c5866a8a2e8161ab82cc8d589da4f3aad0145ea2a0a266b9`.
Exact cleanup completed. Next: carry allocation1 placement through existing
PrepatchedRender into AdmissionGdiAdoptPrepatchedPacket, then EXP598.

Commit `0aea8b24843c5277b7d5eca828baf1d500c2bfbf` implements that exact EXP598
route plus its required qualification lifetime. Render validates allocation1
while its resident list is available, captures exact CPU/GPU/PA/bytes/token in a
portable prepatched state, adoption consumes it once into the same per-fence
packet, and mismatch/cancel/error clear it. Visible scale/latch now completes
before Windows fence retirement; the producer retains both allocations for a
bounded10s observation window, with no intervening allocation reuse before exact
Stop/cleanup. This is qualification ownership, not final production Present.
Integration and full AppleAgx tests are GREEN (367 total). Build next.

EXP598 exact30.0.598.0 pinned WDK/SDK26100 build, code analysis, Universal,
Inf2Cat, TestSign, coherent-version and producer gates PASS. ZIP/SYS/INF/CAT/
UMD/producer hashes are
`3030d1c68d3c4c647c5752029396bab05b5179f3f86d39eee947b87c1c3325c1`,
`8bb20bf07187534e9933b04c12ebd4d1b6c170679e61e16a27f0533aa9a2483b`,
`1d7af9d620d21504b687ccabbb49823ca30d380c2f7046d8ff91c9f032e9abeb`,
`a56b183e6e44f9a06198ae3979c194ce87211f72417628c227789d45baa26973`,
`4050cb72d24ff572f69bf7abe0758f78c688fb5d6b7a29a409f60d37a077985d`,
`478dfe1093b98ead912cbf5fff340d33b4d0e91b30719321dcb2b4184fec3cb8`.
Next: persist EXP597 cleanup, ordinary clean preflight, stage and one natural run.

EXP598 machine-side hardware PASS. Natural bind Code0/exact package/8CPU; one
producer returned both allocations0, residency fence7002 and Render0/queued1.
Visible receipt is Guard11/Stage3/Status0: captured fence256, exact allocation
token, source `0x1500fa0000`/PA `0x9bd160000`/hash `16c4fd3d3ba00d25`,
destination offset `0xfb0000`/PA `0x9bd170000`/15.6MiB/hash
`27592755b9c32325`, requested=applied=latched sequence3, active offset0xfb0000,
swap11. Host independently logged A408/D589 swap11. Terminal remains ValidMaskff,
TA/D3 done2/2, exact fence256, NotifyInterrupt/DPC and all256 `0xff112233`.
Visible/terminal hashes `1f96c9e63be63b63df2c5af8e4b685d4580d2a358fae222d9d150a9767f229b3` /
`c93b43da9d09f5233a62681c2092d55eb6fa73e4431903b2597bb0194f89321c`.
System healthy:8CPU/NVMe2/USB5/keyboard1/no bugcheck; Event129x2 telemetry.
Operator physically observed the expected dark-blue full-screen result. Therefore
`VISIBLE_AGX_RESULT_HW_PROVEN=YES`; this is the first physical panel image whose
pixels originated in a Windows-requested AGX TA/3D job and completed exact fence.
Confirmation evidence SHA
`fc3fed4d1a164bf3c499c78aa93b0ea47018a238f6792104cae861663c193a62`.
`PRESENT_TRANSFER=CPU_ASSISTED` and `FULLY_ACCELERATED_PRESENT=NO` remain exact.
Exact EXP598 package/devnode cleanup completed; persist it with graceful restart
and restore ordinary377/392 before the next production Present experiment.

Cleanup persistence and ordinary377/392 are verified: Code28, no package/service/
module,8CPU,NVMe2,USB5,keyboard1,bugcheck0; Event129x2 telemetry. Next EXP599 is
orchestration-only using exact EXP598 artifacts: start producer1, then producer2
after4s while producer1 still holds its displayed allocation. Require distinct
destination offsets, monotonic visible sequences/swaps, two TA/3D/fences and the
second latch before producer1 destroys its allocation.

EXP599 PASS. P1/P2 both result0. Final terminal sequence2/fence259 completed259,
TA stamp7a000200/done3 and D3 stamp3d000200/done4 with interrupt+DPC. First
visible offset0xfb0000 sequence3/swap11 was replaced while P1 remained alive by
distinct offset0x1f60000 sequence4/swap12; final receipt Guard11/Stage3/status0
has ActiveOffsetBefore0xfb0000 and ActiveOffsetAfter0x1f60000. Host logged both
A408/D589 pairs. Thus repeated visible submission and replacement-before-release
are proven for the qualification lifetime. Exact cleanup completed. Event129x10
is recorded as storage telemetry;8CPU/NVMe2/USB5/keyboard1,bugcheck0.
Next first unknown: remove CPU scale by deriving a full-size AGX PBE/tiling job;
do not guess fields from the fixed16x16 EXP208 capture.

EXP599 cleanup is now persisted through a graceful PSCI reset and ordinary
377/392 restart. Current Air state is the clean GPU-visible baseline:
APPL0002 Code28, zero AppleAgx packages/services/modules, 8CPU, NVMe2, USB5,
keyboard1 and bugcheck0. Event129x8 is retained as storage telemetry. Final
ordinary health JSON SHA256 is
`4155b362d49196935ed1db8f876bfbfe7ea3a26e7859f87fc9ac62ad20fa5a47`.

EXP600 implementation commit
`cd4373e8b86578a2ff9a3dbd8da33528ccfc5799` is offline GREEN and
preregistered for build. Pinned m1n1 G13/V13.5 geometry derives a 2560x1600
job with TPC/tilemap/cluster buffers `0x50000/0x6400/0x32000`; they replace
only objects64/65/67 in the unused backend tail. Exact Construct-derived
WorkCommand3D/TA fields and a Mesa-defined linear PBE descriptor/10240-byte
stride are patched atomically and restored byte-exact. The same full-size
Windows allocation is presented through the proven D589 path before fence
retirement; no CPU scale occurs. Full AppleAgx367 tests are GREEN. Hardware is
pending and `FULLY_ACCELERATED_PRESENT=NO` until exact AGX completion, full
surface content and DCP latch are observed; the uniform clear will not by
itself prove nonuniform layout correctness.

## Standing constraints

- Preserve retained-root/broker, platform, memory, display, scheduler and AGX
  backend layers unless direct new evidence reaches them.
- One causal variable per hardware experiment; preregister and hash every build.
- Event129 is storage telemetry unless a reproducible GPU causal link appears.
- Do not touch the separate native-ANS worktree.
- Failed experimental packages are removed after evidence. At the final complete
  accelerated OpenGL/CS1.6 PASS, leave the known-good driver installed and active.
