# EXP927 CPU-copy escape synchronization plan

WHY THIS HYPOTHESIS: EXP926 DWM LPC thread atNtGdiDdDDIEscape/copy_escape/
transfer_slot/FlushForPresent/_Present; privatePDB localsstep3 offset10000 proves
second64KiB transfer, notQUERY. AllbufferedCPUcopycallsrequestHardwareAccess1,
whichMicrosoftdocumentsasLevelTwo/GPUidle/noDMA. KMDimplementation touchesonly
logicalPTE metadata andmappedRAM withownG3 lock andactive-job/pagingquiescence guards.
RepeatedglobalGPUidle barriers arecloser to the stalledPresent path than heartbeat,
whichdidnotrecur asTDR in926. Physicalmovinggarbage remainsseparate unconfirmed aliascause.

WINDOWS CONTRACT: FULL GRAPHICS, pinnedWDK26100. D3DDDI_ESCAPEFLAGS HardwareAccess
requestssecondlevel synchronization; onlyrequireitwhenKMDoperation needshardware
exclusiveaccess. SoftwareentrypermitsKMD'sown synchronization. NoAdapterSynchronization
remainszero. HardwareAccess0doesnotremoveownlocks orallowbusybufferwrites.
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ns-d3dukmdt-_d3dddi_escapeflags
https://learn.microsoft.com/en-us/windows-hardware/drivers/display/threading-and-synchronization-second-level

AGX/ASAHI CONTRACT: currentG3copies hostmappedlocalRAM aftervalidatingrealhandles,
logicalresidentPTEs/ownership/ranges andperprocessgenerations underownmutex; waits
forprivatejob/pagingquiescence withmutexreleased. NoAGXregister/ASC/UAT/hardware
submissioninthisescape. AdmissionG3TraceUpload recordsCPU-onlyhash inownedmemory.
NativeAsahistaging hold/lock/unlock/lifetime remainsunchanged. Currentm1n1power/retained
mapsandMuACPI/sourceownership from926plan unchanged.

TRANSLATION: UMDcopy_escape emitsFlags.Value0 forbufferedCOPYABI; KMDallows0and
legacyHardwareAccess1 while rejectingallotherflagbits. BothsidesoneCPUescape invariant.
ATOMIC CONTRACT: UMDFlags0 plusKMDpermitFlags0; caller-onlychangeisrejectedby
currentguard4, callee-onlychangeleaveswrongglobalidlecallmode. SupportingMicrosoft
HardwareAccess semantics above, exactKMD CPUonlyimplementation inspected.
WHAT IS STILL UNKNOWN: whetherOSsecondlevelsynchronization is the observedstall
andwhetherPresent/physicalcorrectoutputfollow. Do notnormalizefailedstatus ordeclare
memory-overlapfixed. KernelKMD entry versusOS synchronization ishardwareordering uncertainty.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Actualbufferedsoftware QUERY,
UPLOAD andDOWNLOAD mustexecute androundtrip exactmappedRAM bytes, releasehandle
references, rejectunsupportedflags andretainexistingbusy/paging/range/identityguards.
NewrealKMDreplay testfirstfailsc000000d atsoftwareQUERY in16and64pageprofiles.
No sourceassertions/manufacturedfailingtest. SourceKMD/UMD/src sharedcopyABI/current
realKMDreplay fixtures/pinnedWDK andMicrosoft references inspected; noexternalcodecopy.

- [ ] REDrealKMD softwarecopyroundtrip bothprofiles; rejectwrongflagbits inexistingfixtures.
- [ ] ChangeonlypairedCPU-copyentryflags/guard; preserveallwaits/range/mapping checks.
- [ ] GREENsoftwarecopy andexistingcopy/query suites; commit plusCHANGES40hash.
- [ ] Native/ARM64/WDK zero-warning build927/sign/PDB/sourcehash gates.
- [ ] Onepreregisterednewpackage927 runafterexact926rollbackCode28twice; firstready
  ->oneSDK/Presentcheck ->immediateevidence/rollback; noPresent0stabilitywindow.
- [ ] Only aftercorrectphysicalPresent do recordedstability testing. No927hardwareyet.
