# Active AGX D3D12 loop — 2026-10-18T15:41Z

Worktree `/Volumes/pwdev/public_windows/.worktrees/integration-ad04-windows-compiler`, branch `integration/ad04-windows-compiler`. Russian reports; autonomous loop; preserve dirtymu. Read skill/AGENTS/CLAUDE method for commands. Older state history is preserved in Git and EXPERIMENTS; this file is the current compact snapshot.

## Guest now: stable recovery; no experiment package

- EXP1390 complete and RECOVERY_DONE: normalGPUvisible exactlyoneCode28 APPL0002/noService/8CPU; AfterBoot2026-10-18T15:39:26.6879020Z/durablemanifestC23A9A76FE3EC042E5DA546FFF85889F89E7D78311EF141F3345E64BA186AB7C. No activecontroller/harness. Finalhostmanifest `7e0214efd6b3e2ed3c03064e39c4bb2fb3cdadd37bbb508be858edde5b975331` at.local/experiments/EXP1390-d3d12-cube-init/final-host-manifest.json; ACTUALthiscommit.
- REC1390 controller toolsession75685 finished. No forcedhostreset/physicalaction/hiddenboot; exactoem5cleanupSSH0, twoorderedrestartsCode32->Code28. FactoryV2Host38909f54/helper7ab9f020/selectorbed44fab unchanged. Normal launcher EXP950/launch-normal2.sh owns currentrecovery guest; do not rawproxy whileowned.
- Normal recovery staysGPUvisible/brokerdisabled; emergencyhidden385onlyifnormalunrecoverable. Full-ownerdriverexperiments inherit pinned m1n1 5b857603/Mu361a2134 from1390; exacthashes in hardware-manifest. No package retention.

## Immediate next: EXP1400 CPU_ONLY (BEFORE8252e93b)

- Prerequisite1390RECOVERY_DONE nowmet. NOUPLOAD/EXECyet atthissnapshot. Run before any newdriverinstall. Correctguestroot `C:\Users\pavel\EXP1400-dxil-owned-cpu-only`.
- Local `/Users/pavel/public_windows/.local/c15-owned-cpu-1400`: zip `cf21149e5ad8f8177f413bdf2b48597859bb7566ce5b417eab58c09906a85976`, runmanifest `707d82904971aa56cf6e9d37cbf5fa6baf6ed42f0813501f5c64c3d46a4d84c7`, runner `530c0d7c1166289a787428daff40082341f23c9b911d19a69e54bc0f9b40141b`.
- Parent stage-and-run1400.ps1 `a4048f0b5663395b2c5775605f362c3d5241825a45f6a0e6d1396493c56cdde1`: pre/postCode28/noService/8CPU/sameBoot, zip+runnerSHA/freshroot/PSsyntax, bounded30sCPUcaller. SCPzip toC:/Users/pavel/; stage asC:/Users/pavel/EXP1400-stage-and-run.ps1; thenairssh powershell -File stage. Collectrootparent-before/afterJSON andout/*, recordACTUAL beforeanydriverinstall. NoGPU/DDI/caps.
- Approved1402EXE `3fffd41cc125de1118181f4319704c21f7f61d75b59fbda7348c14da24c77a1e`,21distributionfiles including19notice/license files; oldpauls-rootpreparedfb64zip NEVERexecuted/fullbackup retained.

## C19 failure and causal repair next

-1390source7df1519222fe7f13f274a43c3dfb063cde129b33/probe67a9fd5c; D3D12slot15:31:12-15:31:23:41oldPASS,C19A-D each64/4096bad/firstx21y0 despiteS_OK/fence1,C7PresentPASS. Othercube/mip/facecolorscorrect; do notblamecubewiring/probeoracle.
- CPUoracle((6*x+3)/128) correct; rawx21.0 givesface0 vscenter21.5 face1, exactonecolumnsignature. Ownerhalf_pixel_centeralready1/fsflagsoriginupperlefttrue/integerfalse. Asahiinteger-onlynir_lower_frag_coord_to_pixel_coord ASSUMESfrontendnir_lower_wpos_center, absentfreshDXBCFS. ThatpassNOTidempotent: neveraddunconditionallytosharedGL/TGSI/nativefactory.
- Smallestfix: freshDXBCFS only inD3D12ownercreate_shader beforecreate_fs_state. D3D11/TGSI/sharednative unchanged. Agent kmd_private_pool owns NEW tests/d3d12_pixel_center_fixture.cpp, test_d3d12_pixel_center.py, pinsJSON andanalysis/D3D12-C19-pixel-center-failure.md. NO productionpatch/compiler runyet.
- Preparedtestmanifest `/Users/pavel/public_windows/.local/c19-pixel-center-offline/before-red.json` SHAb5fa4ac84f46425532e8364589eec4b6d059c3a1a62c3c01017791a9f2659dbd;17sources/27externalpins/6commandsSHA5a4d079b69a77aa35057879b256ffeaaad1596bdfca9f55c8b7bb6d788f16c72. Actualowner+3realC19FXCarrays+actualDXBC/NIR/Asahicoordinatepasses; expectedx21.5 vsraw21,72pairs/ZW/flags/once/VSnegative. Parentmustwrite1403OFFLINEBEFOREbeforeRED; thenminimalfix/sourcefreeze/GREEN. Candidate1404reservedfuturebuild/hardware, NOTREADY.
-1392unfixedQUEUEDHOLD: chain1392unconditionalexit, SHA07e6ee30; oldC20+C21package mustnotlaunch.1391hardwareplanSUPERSEDED; builds1390/1391/1392PASS butlatercandidatesneedC19fix/updatedpreregistration. C20typedextent andC21volume sources/probes alreadyimplemented/offlineverified; see b7959de0 plan and30381c63 source.

## Regression/control result

-1390:73SMOKEPASS+onlyknownMSAA-a000rFAIL;fl11cs-a/x/debugPASS;GL52.7/internalGL59.6, separateCS3771/80.942=46.589fps;corpus64/4zero hangs/pixeldiffs,DWMfaults0. IntentionalLONGJOB1kind4/fence242ab/902ms only; noobservedbugcheck/guesthang. Input/RDP/perCPUtimersnotindependentlytested.
- KMDforeign-cachepolicy0c458548 remainsvalidated:5workers,pressurebefore0->15/requester2564/victimExplorer5144/global895->888/freed7x64KiB/request1MiB/largest17units. No provenlost-unit leak.
- Predecessor1387all41D3D12PASS/C18mapsPASS withdevicezeroe8e0b35a; RECOVERY_DONE/PackageAbsent. Finalmanifest30829edb/ACTUAL44e524b7.1381C13CPUAVowner=-1 solvedbyprivateinitialization, poisonwriterunknown.

## C15 requestedlicense task complete; CPU prep only

- Userrequestedlicense review+plan committed75632532/processrowea9cc8b0; selecteddxil-spirv8434b063 runtimeYESwithMIT/BSD/Khronosnotices/pins, optionalclosuresnotautomaticallyapproved. No externalimplementationcopiedtoGit.
-1395Windows9libsPASS/manifestlive-logexceptionpreserved;1396ownedadapter e0aa4ca4PASSonekey;1397originalSM6fixture4b3cf49a/DXILb22b71c9/5namespacekeys andindependentvectorsPASS.
-1398mixedmapactualNIR c6248a6bPASS:CBVUBO0/SRVtxf0-1/UAVSSBO0-1/actualfreeze120perms/reject-valid+legacy;verdictf2905124/SPV24e3df26.1401semanticsce3798acPASSfixedIR384words/346stores/3negative346each/postIRunchanged;verdict7e9e1482. No generalinterpreter/GPU/ownerproof.
-1402originalC11WindowsARM64/MDstaticlink9libsPASS;hostmanifestdbab8c3c/47original+21dist+52hostfilesverified;EXENOTRUNuntil1400.1399stoppedbeforecompiler atDirectoryInfo pathguard; typefixactualASTregressionPASS; original1399preserved.
- Runtimegate6952c182: exactlocalD3D12Core26100.9549SHA052241d7, constructorversiontableordinals0003=1/0010=2/0011=3;modelinitordinal<3skips1012andretains5.1,consumerclamps.0011firsteligibleforTHISruntime (notuniversalnorm). receiptmanifest19d60ad6.
- TypedR1libraryelision staticpass193949d5insufficientwithoutmatchedPDB/per-versiondispatchassociation; noNULLinheritguess. OwnedCPUlibrarydesign ea8e86b8 isdesignonly; OOM allocatorfailurepolicy andownerconcurrency/reentry remainunclosed. No R1/SM6cap/productionlink change.

## Paths and rules

Scratchpad `/private/tmp/claude-502/-Volumes-pwdev-public-windows/8c9ead37-29d6-4c63-9bb0-35c6eb850f40/scratchpad`; backups `/Users/pavel/public_windows/.local/scratchpad-tools-backup`; XP `/Users/pavel/public_windows/.local/experiments`.
BoundedSSH: perl alarm25 + `/Users/pavel/public_windows/.local/tandem/airssh -o ConnectTimeout=10 'echo up'`. Builder pauls@192.168.1.24/keywindows_builder. Do not issueparallelguestcommands duringrecovery. NeverrequestphysicalbeforeboundedSSH+USB/owner/proxy checks. Before/ACTUAL/CHANGES+exacthash/sign/stackgates eachrun; no attribution. Fullsuite last1561same119baselinefailures; newerfocusedtestsPASS, neverclaimwholesuiteGREEN.
