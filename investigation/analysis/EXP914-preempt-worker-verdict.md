# EXP914: worker handoff fixed; physical presentation still absent

Correction038cd941, packaged from49b4c56c as30.0.914.0, passed the actual RED/GREEN notification/worker replay, relevant queue/lifetime/local-output tests, pinnedWDK26100 KMD/UMD/probe0warnings0errors and exact CAT/PDB/native-archive checks. Full1193 retained baseline15F38E2S plus two unchanged active serial-lock tests; both had passed in the preceding idle suite.

## Hardware verdict
CONFIRMED for the worker-retirement correction. Originalboot2026-10-01T15:47:16.8701340Z ran772.839seconds Code0/CPU8/DWM1224/Explorer5200/SSH without observedTDR/stop. Both tracked DWM contexts have EnvelopeStage0 (no13/8 or other envelope reject). Exact first frozen ETL, decoded using installed Microsoft provider metadata, maps DWM process4c8 to Apple adapter and contextffffb38f9f9afde0. QueueSubmitSequence5 was preempted at2494, resubmitted2495 and preempted again, then resubmitted2496 and completed with bPreempted=false. This exercises real nonpaging resubmission successfully; it is stronger than absence of a crash. Evidence early-dwm-preempt-completion-proof.json and early-preempt-etl-input.json under root.local/experiments/EXP914-preempt-worker-retirement. Raw ETL wallclock has the previously observed bias; rely on same-trace ordering/identity, not absolute eventUTC versus CIMBoot.

Desktop verdict FAIL. Native DWM issued three pfnPresentCb calls returningS_OK, source40003200/VA1f0000, but physicalPresent/virtualPresent counters remained0 and initialSourceAddress/Commit remained1. DCP current swap9/seq2 atIOVA102a0000/PA8e0110000: matching +120/+300/+600 snapshots all4096000zero pixels/cache_clean0. No claim of visible updating desktop or accepted working package.

## Remaining boundary
One bounded read-only probe in consoleSession1 (temporary task removed) reports AppleLUID4064d sources1/type10b: render/display/post/ACG, not software or indirect. Session0 sourcecounts are inapplicable to console routing. Driver DXGI callback/context forwarding matches pinnedWDK; retain documented NO_REDIRECTION status in the absence of the D3D9 shared hardware contract. No speculative capability/status change.

After the baseline window and original evidence gate, one normal/thread-info DWM1224 minidump104942B was captured. SHA84931804d8bb45f7013b2022f42dd5e1af0a0ccdca5a1c40b0db2d4c5bbbe434 independently verified onhost andbuilder. MiniDumpWriteDump returnedtrue; optionalProcess.StartTime caused only the receipt to fail. A separate read-only receipt repair verified existingMDMP, identity, originalBootBefore/After andCode0; no second dump. CDB resolves exact Microsoft symbols: compositor thread574 is in dwmcore!CScheduler::WaitForWork through CComposition::ProcessComposition; token/input threads wait for messages. This snapshot does not show a blocked UMD/GPU call, but does not prove why no work is scheduled. Localdll/PDB identity is package914. DWMStartTime is explicitly unavailable.

Read-only recovery power metadata showsACLine1/noBattery128/AC VIDEOIDLE0; DC180 is inactive, consolepavelSession1active. No power setting changed. Original attempt to query policy overlapped orderedrestart andreturnedSSH255; no data inferred from it. No fresh DWM crash in inspected application records; queued historical WER reports are not current failures.

NextEXP915 observes DWM internal surface candidates/rejections and scheduler/device-state decisions using one extra provider in the same bounded AutoLogger, exactpackage914 reinstalled afterrollback. Plan EXP915-dwm-boot-events-plan.md. No driver behavior patch is justified at this remaining boundary yet.

## Evidence and recovery
hardware-evidence-original: 14payloads plusmanifest, independenthostgate143ca771aa50eca7b327ca0ff7022ccc7188b3abd97caf7b6cafc263715ddc56; boot2026-10-01T15:47:16.8701340Z.

hardware-evidence-recovery: 13payloads plusmanifest, independenthostgate0daa407b0a8ff71fd2a9011505fab004d9b9b8c4efdb9a6ccada3a6b56421b70; boot2026-10-01T16:09:25.4320040Z.

Frozenperiodic/frame160files gate4641b25a228a02a925d390dfb3fb46c24c3bb91f3a2be650a78773e2b4c6b912. Workers stopped before collection. Originalorderedrestart16:06:08 completed normally. Immutable377/392 visible recoveryCode43/Start2/oem5/Stopped/armnull; all hostgates preceded diagnostic/exactpackage removal. Three256MiB ownedETLduplicates were size/SHA verified before deletion, hostcopies retained; free4474441728/shadows0. Exactcleanup16:19:52 scheduled normalrestart. No hidden boot or liveCode0removal.

Finalordinaryboot2026-10-01T16:20:12.4493630Z checked2026-10-01T16:25:00.8319642Z and2026-10-01T16:26:19.4122546Z: oneinertAPPL0002 Code28, package/SYS/UMD/service/signer/arm/diagnostics0,CPU8/disks2OK/USB5/SSH/RDP/autologon1; free4488622080/shadows0. Cleanupreceipts independenthostgate5d2b8b57b6da359ba407a1bf2d77d74e3582c0bcf311e52bc1f61037e834f4d6. USB was enumerated, not interactively exercised. AcceptedpackageNONE.
