# EXP925 — allocation destroy boundary verdict

The SDK again blocks while creating the fullscreen proxy window. This run does
not distinguish a wait inside the KMD destroy callback from Windows synchronization
before callback entry. It is not a driver fix or proof of desktop presentation.

## Original run

Package 30.0.925.0 from 47f7e2910222e8be1984f1664c2f30a966031e5b adds paired
in-memory allocation destroy entry/exit receipts only. Firmware and SDK unchanged.
Boot 2026-10-02T11:36:07.9711970Z reached Code 0 / Stage 12 / eight CPUs in 43.83 s.
One SDK process 4908 imported both exact BGRA fullscreen backbuffers successfully.
The checkpoint was 11:37:07.1480098Z; snapshot completed 11:37:09.7872185Z.
The 99101-byte snapshot SHA-256 is
`d52f5004ddedcbc5cedeadade0fa614075ada27335499d67d9c2eed78f8f55da`.
Host size and SHA gates passed before rollback.

The main thread waits in DXGI ProxyWindow creation / SetFullscreenState /
CreateSwapChainForHwnd. Worker 8 waits in NtAlpcConnectPort / dwmapi EnsureConnected /
DwmpUpdateProxyWindowForCapture. This reproduces EXP924's client boundary.
The pre-workload probe recorded 202 destroy entries and 202 exits, zero dropped,
with last caller PID 4 / TID 6252. An after-workload probe could not be collected
because SSH was lost. Those initial matching counts prove instrumentation operates;
they cannot locate the later stalled allocation release.

There is no returned fullscreen/clear/SDK Present success receipt. The before-test
physical and virtual Present counts were zero. No accepted desktop is claimed.
The full owner exited with USB read Errno 6; this alone is not a Windows stop code.
The SDK workload was not repeated. Frozen original early trace was subsequently
recovered from the normal guest and every receipt file size/SHA verified.

## Recovery

Fresh bounded SSH was unreachable; the full launcher had exited, both expected USB
endpoints were present, and guarded NOP found Running proxy. Normal GPU-visible
377/392 recovery reached Code 43 / Stage 2 / eight CPUs. Eighteen recovery evidence
artifacts passed the independent host gate
`5be421fc45c4befde2810f50db1d3945646a0ef6d09aca626c5d885b679344e1`.
Only then was exact oem5.inf/package925 removed at 11:44:48Z with an ordered restart.
The normal final guest passed the complete Code28 contract twice at 11:49:15Z and
11:49:53Z, same Boot 11:45:46.2418250Z: one inert APPL0002, package/service/module/
signer/diagnostics absent, eight CPUs, two healthy disks, five USB devices, RDP and SSH.
Three cleanup receipts were separately host size/SHA gated.

## Next causal boundary

EXP924's DWM LPC thread at NtGdiDdDDIDestroyAllocation remains the strongest observed
server-side boundary. EXP925 does not prove whether the kernel driver body, VidMm
synchronization, or retirement prevents its return. Do not guess a capability,
deferral, flush, busy-state drop, or tiling change. Use the recovered trace and
actual Windows stop evidence before designing the next minimal discriminator.

Evidence directory: root `.local/experiments/EXP925-allocation-destroy-boundary`.

## Confirmed new Windows stop (appended after recovery collection)

System event1001 at 11:41:58.8207493Z records the original boot's new stop:
`0x116 (ffff8487be2bc010, fffff80142b2ab40, ffffffffc0000483, 3)`.
The fresh 1066156-byte minidump SHA-256
`dc48006644607a8d3b2b18a1f36053e33e3d3fa4b7b624ddf300868f21165c70`
was already in the host-verified recovery set. Matching private925 PDB decoding
confirms crash time11:37:32.596Z, uptime85.161s, CPU3, IMAGE_VERSION30.0.925.0.
Stack: dxgkrnl TdrBugcheckOnTimeout -> DXGADAPTER PrepareToReset/Reset ->
TdrResetFromTimeoutWorkItem. Argument2 maps to AdmissionDdiResetFromTimeout.
This is an owner pointer, not proof that source line556 itself caused the hang.
Argument3 c0000483 decodes to a fatal-device-error status. Current scheduler_windows.c
can produce that status on private reset failure, an active private render packet,
or platform runtime reset failure (lines453/476/484). The triage dump does not yet
select which return or identify the original timeout. Do not replace it with success.

Official contract: https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/bug-check-0x116---video-tdr-failure .
The separate 635199439-byte MEMORY.DMP copy timestamp is11:32:16.498Z, beforethe
original11:36boot; do not silently attribute that older file to925. The freshminidump
is the current-stop evidence. Next boundary now includes exactTDR/reset/fence state,
which may explain why a DWM allocation release cannot makeprogress.

## Correction: full kernel dump is current; narrower failure boundary

CDB's internal MEMORY.DMP header proves the same crash time11:37:32.596Z,
uptime85.161s and identical116 parameters. Its earlier filesystem timestamp is not
reliable crash attribution. The prior timestamp-only exclusion above is superseded.

The matched PDB and the known original DWM render context give the adapter at
ffff8487bcc02000 and platform runtime at ffff8487bcd25000. The captured destroy
entry and exit counts both equal577, sequence1154, dropped0, and the last same
allocationffff8487bd6d5a00/PID4/TID312 returnedstatus0. Thus no KMD destroy callback
body remained outstanding at the crash. A prior transient wait remains possible.

SchedulerFaulted is0x40bb0: exactly0x40000 |2992. In the sealedsource this is the
pre-submit RTKit heartbeat failure path in backend_platform_windows.c. Completed
fence7961 is behind submitted/active7966; packet remainsActive andpreemptionpending.
The reset receipt capturedc0000483. The original failure is now narrower than a
missing fullscreen callback: a submission stopped before enqueue on heartbeat
failure and its fence did not retire before TDR.

RTKit remainsBootReady/Running1/CpuReady1/StopIdle/CrashlogCrashed0. Its lastreceive
is endpoint0x20, payload0x0042000000000000. This is an AGX event notification in the
current m1n1 FirmwareEP; our heartbeat already accepts this event type. Lastmessage
alone does not prove a protocol violation or missing firmware. Next inspect the
heartbeat/Pong and ASC receive ownership contract, preserve AGX channel handling,
and reproduce a deterministic defect before fixing it. Do not maskresetwithsuccess.

Detailed frozenstate: cdb-flat-context.log andcdb-final-state.log in the evidence
folder. No further hardware run was needed for these conclusions.
