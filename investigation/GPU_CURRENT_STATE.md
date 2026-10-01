EXP920 HOST ENOSPC repaired: pwdevfullkilledmonitor/framepollandleftguestcopydirs. WindowsstillSAMEBOOT22:15:48.762806Z Code0CPU8 at876.881374s; noGPUresetobserved. Originalowner96431active. Finalobservationrestoredwithcoveragegap; oncepostbaselinecompositionrunner96392 stillpending/shouldtriggernow, inspectlog beforeanymanualaction. NoDWMtermination. Migrated33completedfiles to /Users/pavel/J313-evidence-archive/2026-10-02 withverifiedcopies andsymlinks; externalfree8.5GiB. NeedrecoverremainingguestEXP920-live-snapshot dirs, originalcollector/hostgates; markserial+600unavailableiflost. Uservisualquestionstillpending, noresponsepresumed.

EXP920 firstoriginalboot2026-10-01T22:15:48.7628060Z reachedCode0/CPU8/DWM1248/Explorer4680 through103s, no earlyreset. Launcher96389/monitor96390/postbaselineprobe96392. Useroptionalvisualquestionpending (black/desktop/unavailable), no replypresumed; continue750s/physicalPresent/currentDCP/probe. Collect-original/restart helpersboundtoactualboot. NO DWMtermination.

EXP920 ACTIVE: exact920packageinstalled, launcherPID96389/monitorPID96390/postbaselineprobePID96392. Root.local/experiments/EXP920-preempt-notifier-race; manifestfb0f03d4/payloaddf83e269; fixa5c2c1af+faultorigin tags. NeedfirstCode0 then750s/currentDCP/creationprobe; NO DWMtermination. Ifearlycrashpreservetaggeddump. Prior919cleanrecoveryclosedbeforestage; do notremove liveCode0.

# J313 GPU current state — EXP919 cleanup / EXP920 building

User explicitly says continue until fixed. Desktop is still FAIL; accepted package NONE.

## Current machine/action
EXP919 fullyclosed. Ordinary377/392 boot2026-10-01T21:59:38.551232Z,
checks22:01:54/22:03:02 Code28/oneAPPL0002/pkg-module-service-signer-arm-diagnostics0,
CPU8/disks2USB5/SSH/RDPservice/autologon1/free5034749952/shadows0.
Cleanlauncher93926 (verifyfresh). Cleanuphostgate13d4be526d0dcf33f52ef2dcc2cd7f1187e2e284f292f2b67f68ab31005219fc.
EXP920built/sealedNOTSTAGED; needsfreshstage/disk/hostreceipts/dualcontrol.
Manifestfb0f03d40b528634e2bfd00a67526942f28c1c38eb35211408901e57683cf0bd;
payloaddf83e2691c91409e49bd0c893ff3d3603ac06746fc1c737650c4b29a67d5313e.
NativeKMD/UMD/probe+x64contract0/0; nativecontract/sign/PDB/nativearchivePASS.
Finalfull1195 exactbaseline15F38E2S, no newIDs; initialfixtureCASmissing fixed
separately(testonly), originalpackage source d16f42c0 unchanged.
SamecompositionprobeCD7D043E after750s via59989cf1 runner; NO DWMtermination in920.

## EXP919 actual failure (DWM test DID NOT RUN)
Same918package, earlyWindowsPSCIresetbeforefirstSSH. No reinit invocation receipt;
runner79618 stoppedwithoutintervention. HeaderprovesCPU5/System119/2/80000011
at38.769s; normalPagingflags1/fence735/DMA4100120 size100/private780.
Exact918PDB objects: hContextffffb708f8e4c1c0 -> devicefcf76490 -> adapterfcd02000.
Scheduler atfcd7eb50: Completed731/LastSubmitted732, queue0/active0,
preempted732 count1, phaseIdle/pending0. CpuQueue0/PagingPending0/Stopping0,
Dispatched0/SchedulerInitialized1 butSchedulerFaulted1.
RenderPacketEmpty, BackendImageReady1/no boundfence/job, backendReady,
WorkScheduled0/WorkersActive0/Stopping0/Resetting0. TerminalResultInvalidState
withTerminalPending0 is normalClearPending default, NOT GPUfailure proof.
Firstfault-setting site was not captured. MMIO pagesnotindump, useonlyreadablefields.

## Confirmed software defect / next EXP920
PreemptCommand cachesnotifyNow thenunlocks; WorkerFinished/DPC canclaim/notify/
commitfirst. OldTryNotify thenfailsclaim(Idle/Claimed) andPreemptCommand wrongly
setsSchedulerFaulted. ActualproductionPreempt/Try/Worker+realsharedscheduler
RED reproducesexactIdle/fault1/oneNotify/731/732; fixGREEN. This isconfirmed
source race matchingdump, NOT uniquelyprovenoriginalfirstsetter.
Fixa5c2c1af: underlock benignIdle/Claimed/Wait attemptsreturnaccepted/deferred;
realSync/Commit/interface/unknownphasefailurespreserved. WorkScheduledguard and
postcommitwake retained. Diagnostic23faultsets nowfirstCAS filetag|line;
allBooleansemantics/reset0unchanged andpublicBooleanreceipt normalized.
Filetags high16:1scheduler,2paging,3submission,4backend;low16source line inexactbuild.

EXP920 source d16f42c07a9ae52ea9c3d8e9c428348b373f7087/package920 builtNOTSTAGED,
root `.local/experiments/EXP920-preempt-notifier-race/`; build/sign/PDB andfinalfullsuitecomplete.
Actual3ASan/UBsanreplaysGREEN; firstfaulttagpreservationtested. FirstGREENattempt
hadinvalidnegativeassumptionaboutopaqueFence0; correctedtounknownphase99 after
sharedsourceinspection, no productionchangeforthatassumption. ActualREDkept;
overwritteninitialGREENlog truthfullyrecordedintranscriptnote. Neednative0/0,
sign/PDB/fulltest exactID gates. NoDWMtermination in920. Confirmnormalstartup,
>=750s, physicalPresent/currentDCP; samecreationprobe canverifyb529retained.

## Preserved evidence / cleanup gates
Kernel596888485B SHA f582779824a83c3ed2a160cd06f6973593ce5ae4fd01795430112a1ed84f04d9
hostandbuilderverified. Builder C:/Users/pauls/EXP919-kernel-audit exact918PDB/SYS.
CDB commands mustuseNEWLINES: .sympath consumessemicolonsaspath. Initialmisparsed
logretained; kernel-analysis.txt/context/objects/device/scheduler/queue-state/
backend-state/runtime txt areactualresults. Readerreports96%,lastlinecompleted.
Direct8files+inventoryhostgatee0048b8094acf5923afc54f9fc81b83ae4d1402f01919ef17aa169a6282793ea;
original6hostloggateb4c7acca3f4a215ee49a0468cb0d3b74ff8ef887bb39adf8b69fa20d680faaa8.
No originalETL/Code0receipt; recoveryETLnotoriginal. Diagnosticcleanup usedexact
mixedinventoryaliasSHA0b52357f, notfakeoriginalETLreceipt. ExactsourceWindowsMemoryDMP
duplicate removed21:52 afterbothcopiesverified; free5025148928/shadow0. Nootherdumpdeleted.
RecoveryCode43boot21:03:43.181521Z/Stopped/CPU8, nohidden/forcedsignal in919.

## References
analysis/EXP920-preempt-notification-race-plan.md; EXPERIMENTS.md EXP919/920 only.
Tests test_preempt_notification_competing_callers, oldpreemptworker/workqueue.
Priorb5291548 displayableBGRAfix ishardwarevalidated byEXP918 sameprobecreate/bufferS_OK;
EXP918773.877sCode0butphysicalPresent0/DCPswap9seq2zero. DWMtest919inconclusive,
neverclaimrestarttested. Source918d5c68559PDBs areinEXP918-displayable-resource.
Do not broadlyrewrite modernDDI orchangeNO_REDIRECTION withoutcausalproof.
Event468 isDx_Flip_Consumed, notoutputbinding; activeApple2560x1600proven.

Recovery377 SHAfae3444cc289cf52ea12b81b9db8f3d8bf24bd084f899a751321d2048d9a525a;
392 SHA16c177182e96b63eac852dcfb185cebba9c1d91943c6402106a640848ddc5e06.
Hidden385 SHA279bd36ad3bbb1ee5e2393fa965343ea856b4c2b0dd4df2b2add6a8010e3f32c emergencyonly.
NeverproxyNOP whileSSH/launcheractive; INSPECT returnedSSHresultbeforedependentaction.
Allhostevidencebeforeexactpackagecleanup, thennormalGPUvisibleCode28twice.
