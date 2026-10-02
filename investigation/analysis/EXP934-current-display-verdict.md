# EXP934: current display investigation result

The WinPE guard failure was repaired and offline CHKDSK completed in17.87seconds.
It corrected NTFS metadata including file201F2 (previously SYSTEM.LOG2). Independent
RegFlushKey changed from1016 to0, and C: remained notdirty after subsequent boots.

Two independently reproduced private-HVC defects were corrected: m1n1 skipped the
instruction after HVC, and pinned MSVC __hvc consumed X8 while our private ABI returned
X0. Actual epilogue and ARM64 machine-code regressions passed. EXP933 confirmed
ArmPhase6/Status0/Hypercall1 on J313; its later failure was VIDEO_TDR_FAILURE0x116.

The exact933 kernel dump localized the first scheduler fault to the pre-submit
management heartbeat: software fence9189 was active but never sent to hardware;
heartbeatTimeout3 after5099ms with eight runtime notifications received. Asahi/m1n1
queue paths do not require a management pong before every job. EXP934 replaces that
prerequisite with bounded nonblocking ASC notification consumption, retaining real
job/manager/queue/completion and crash/unknown-message/MMIO failure guards.

Source2602f23dc12498265326746299888c656ca147f6, package30.0.934.0, m1n1bdcf8715,
MuR143e54 unchanged. WDK/native provenance/signature/package hash/PDB gates passed.
Worker-span RED/GREEN tests and native ASC/session/queue tests passed. A preexisting
legacy-project registration assertion remains failing against unchanged old project;
it is not a failure in the actual render-admission project or native lifecycle test.

Hardware934 reached Code0/Stage12, eight CPUs, SSH, successful HVC and continuing
DWM submissions/completions. VSync notifications continue. Present/VirtualPresent
remain0; correct physical desktop has not been demonstrated. This is not a release.

One SDK stimulus reached both backbuffer imports then waited in DXGI fullscreen
proxy-window creation/ALPC connection. A paired DWM LPC snapshot was in allocation
of a512KiB command encoder during border drawing, while the compositor waited for
its nexttick. A single stack sample does not prove the allocator is stuck: live
counters continued to advance. Next investigation must distinguish transient/slow
allocation and composition from a persistent wait, without changing capability bits
or asserting that a physically visible fragment is a blank panel.

Evidence: ROOT.local/experiments/EXP934-direct-gpuva-submit/frame-query.out,
frame-after-sdk.out, paired-evidence/host-gate.json (8b9eee2a375554a75218b037b134180a159c592da5202ea7fadb12f093a9e469),
and matched cdb-sdk-creation.decoded.log / cdb-dwm-paired-sdk.decoded.log.
Original933 kernel and four verified files remain in internal evidence archive.
