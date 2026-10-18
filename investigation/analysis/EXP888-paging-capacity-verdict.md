# EXP888 — original-window failure and recovered boundary

Candidate `efb76f3d3e2800f29c1b266c278e39322572bdb6`, package30.0.888.0, exactEXP887+R167. Build2 KMD/UMD0warnings0errors; build1 never installed. User-approved single original window, no retry/rearm.

## Observed result

- Originalboot00:12:40.877014Z; last SSH sample182.411362s, Code0/Start12/status0/CPU8/DWM1240/Explorer4460.
- Full kernel dump and minidump agree: debugtime00:15:49.977Z, uptime189.635s, CPU1/System, exact888module. `VIDEO_MEMORY_MANAGEMENT_INTERNAL`0x10E, parameters0xB/fffff60c2627cf78/ffffffffc0000483/ffff888ff4105000.
- Stack: VidMm worker -> allocation page-in -> CommitVirtualAddressRange -> UpdatePageTable -> CompleteBuildPagingBufferIteration -> bugcheck. This is not the EXP884 FLUSH_TLB/BUSY case; P2 decodes as `DXGK_OPERATION_UPDATE_PAGE_TABLE`.
- P2: GPU_PHYSICAL mode2, leaflevel0, tableoffset0x3b694000, StartIndex0x1210, count0x400, processhandleffff888ff5ac4010, no64KBentryarray.
- Correlated retained pagingfailure Version3/104B: Branch7(LeafGraph), level0, index0x122c (within failing update), statusC0000483, GraphLastStatus7(CAPACITY), GraphUncertain0; tableIPA0x91b694000/brokerIPA0x9dee34000, PTEflags0x41, pageaddress0x569c, childIPA0x8e569c000, tablecachebranch3.
- PrivateACQUIRE first observation Bytes0 at55s and no retained privatefailure after loss. The prior private-global refusal was not observed; this alone does not validate R167 across a600swindow.
- Scanoutseq2/raw4096000pixels/nonzero0 before loss. Acceptance FAIL: neither600s nor nonzeroPresent. No observed user-mode crash attributable to888; date-filtered event files also contain older887 records and must not be treated as888 failures.

## Source-supported boundary, not an invented fix

Inspected candidate KMD `gpuva_g3_paging_windows.c` (`AdmissionG3UpdateLeaf`), shared `apple_agx_gpuva_g3_graph.c` (`GraphTryLeafBacking`), pagingreceipt ABI, and broker `hv_agx_gpuva_v5.c/.h` (register_backing). Broker source hashes9f6d0d0effd18c835f35a587505d9687a49efee490bb73e24a2d0b8831fed0ef/4fd2a7d4b128fc7741d3c2aa111594f5f03ae679d41f55b54998150c3b829eeb match the EXP856 frozenR143 final-tree record. Header has8192backing grants. Source commits alone are not sufficient provenance; the final-tree match resolves this.

Status7 is acknowledged broker CAPACITY. Current graph registration reports unavailable fallback only for SYSTEM backing, not LOCAL. The failing PTE maps localIPA0x8e569c000. Registration capacity refusal therefore reaches LeafGraph/C0000483 and VidMm's unrecoverable invalid-return check. The receipt localizes the boundary but does not measure all live grants or distinguish necessary registrations from leaked/stale lifetime retention. Do not increase an array blindly, silently acknowledge an unmapped local GPU allocation, replace failure with SUCCESS, or reinterpret this as TLB uncertainty.

Microsoft primary sources inspected2026-09-30: https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/bug-check-0x10e---video-memory-management-internal confirms0xB invalid BuildPagingBuffer return; https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_buildpagingbuffer describes supported return/paging-buffer rules. Classification FULL GRAPHICS; do not transfer DISPLAY_ONLY/KMDOD admission assumptions. A DMA-buffer shortage return is not evidence of broker resource shortage and cannot be substituted blindly.

Next causal target for Claude review: reconcile advertised public/private backing/residency and the bounded broker grant lifecycle, derive supported FULL GRAPHICS paging/backpressure behavior from the pinned WDK/Microsoft contract, and reproduce exact local-backing capacity exhaustion offline before implementing the owning correction. No new hardware experiment and no production fix in this task. Existing deterministic R167tests do not prove broker grant capacity behavior.

## Evidence and recovery

`.local/experiments/EXP888-private-capacity-va/`: heartbeat/window-state/full.log, early `periodic-original-boot/20260930T001439Z`3/3size/SHAverified, preserved888PDBs, `kernel-mini.txt`, `kernel-full.txt`, `paging-detail.txt`, `paging-process.txt`, `hardware-evidence-final/`27/27files868000737B hostverified. ManifestSHA256d26f0d71f965643938ba76b6e23e77aa593176b8ce6487af237e1b9664b22c69. KernelSHAfd4043342aff69da32fd4384ea890cc071fdce587040d179cffa53117666ed98. Original stoppedETL unavailable after spontaneous reset; early liveETL remains partial original evidence, hiddenETL is recovery-only. Filetimestamps and historical events are not a reliable attribution substitute for debugger uptime/module evidence.

Emergency hidden justified by originalguestloss/spontaneousPSCIreset; no host reset. HiddenCode45/exact888/oem5/SYS/UMDhashes/stoppedservice verified; collection first refused an untransferred extra script, then watchdog final summary errored on OrderedDictionary Measure-Object AFTER complete manifestwrite. These are collection-environment errors, not extra candidate runs. Independent host verification over every manifest item passed before any package cleanup.

At00:23:54Z diagnostics cleaned only after manifest and Code45 rechecks; exact888package/devnode removed00:24:20Z then ordered restart accepted. No liveCode0 removal. Final ordinary boot00:24:35Z, durable00:26:46Z Code28/present1/package0/arms0/SYS0/UMD0/service0/signer0/diagnostics0/CPU8/disks2OK/USB5/RDPserviceRunning/autologon1/passwordpresent. Physical display/keyboard/RDPsession activity not independently observed; no such health claims. Recovery proof `ordinary-durable-final.log`.
