# EXP909: public-local render admission exercised; original TDR; recovery closed

## Result and proven scope

Desktop mission remains unmet. Exactly one package909 original boot ran; it terminated with 0x116 VIDEO_TDR_FAILURE at kernel uptime34.016s. It could not produce the required750s or late120/300/600 window. No unchanged package rerun, no synthetic display success, no accepted package left installed.

The deterministic G4 bounds correction36b99d41ec46f8a785770c7ac18b2a324eaf502e uses LocalView for ordinary non-Present render outputs; DCP ScanoutView stays56MiB. Hardware exercised the correction: active native ticket935 has destination local span2d10000..3ce2000, crossing old3800000 bound. This confirms admission, not successful rendering or desktop sufficiency. Keep software status=implemented.

The source/device/POST audit remains in EXP908-display-source-audit.md: Apple StartDevice/Adapter events independently prove1 source/1child. SSHsession0 sources0 does not prove missing sources. SecondLUID61e2 is BasicRender1414:008C; BasicDisplay61a8 is separate. No caps/ACPI/POST/source-count workaround was made. PID1220 and1228 have separate contexts/lifetimes; do not call1220 historical without a boot/session ticket.

## Source, tests, build and exact package

Behavior36b99d41ec46f8a785770c7ac18b2a324eaf502e; diagnostic type/session3c7e1bd89ec250408a2fa8323a52e420c413deb7. Exact source/native caf4575e7e999b6023075fd979ae725dee4ffe0c,540 driver files. Same actual extracted Envelope+real ScanoutView ASan/UBSan replay RED C000000D/branch9/subsite2 -> GREEN high local output queued; unchanged scanout/unmapped/discontiguous/local-range guards refuse. Full1190 baseline16FAIL40ERROR2SKIP ->16FAIL39ERROR2SKIP; exact IDs unchanged except intentional new RED becomes GREEN. Existing unrelated baseline retained, including two active-launcher guard IDs. Schema2 GREEN.

Pinned WDK26100 KMD/UMD/probe0 warnings0errors; single ABI/SAL/IRQL/initialization/format review before build. INF/SYS/UMD catalog membership and expected test signer checked; builder trust failure was only untrusted test root, guest valid-signature gate passed after exact-root import. Independent package/PDB size/SHA and PE/PDB GUID-age matched. KMD GUID4796577e44fb834095c40d94d2825050,UMD284ec60a8287ff498d24175532ef858a,age1.

Package30.0.909.0: INF7cffc2b35fada23157b5817ff09654b2334a49993d5e2df473dac841f660250c; SYS7d97a1451eb0d2a7df0f4b37f606ea7f82e4e5ec6c6541a238cf38709d6c3ed6; UMD16296c5338e87bb6ec3392bdfc386bf7079f62cc9f7fc9c78cf1e1578dd21c14; CAT8576318cd78921d6fd59aafe420b810b463f2df38cc18d6e45de54f3b90ccb1c. SourceZIPd064efd30a804a125889b4d3600fd16ac75375020b139726aa07ff2869c47b3d; manifest23ef4c33cc3f480cee9a1106b5fe3e6db6fa56069cacbbfcc8ee81bd92e67b0d. Hardware469f082446c691691e3174e4b0e27c8b9b3c64378dfb1eb86737e697293b7d16. Immutable m1n1/Mu/AML/recovery hashes remain in hardware-manifest; no firmware/ABI/renderer/shader/scheduler capability change.

## Original boot and localized failure

Dump Kernel-MEMORY.DMP616844121B SHA8abc484e675404334d4d2738a9226254e5110857c64403b476e5b4a575c0bebb. ARM64/CPU8; debugger time06:09:19.590Z, uptime34.016s -> raw kernel boot06:08:45.574Z. WMI original boot receipt unavailable because no successful SSH before loss. Exact private PDB909 and original PSCI stackfffffb8a08f1f000/modulebases ntbb200000/Appleb9970000 distinguish this from failed visible-recovery reset. Filesystem time06:05:55 is not used as boot attribution.

0x116 parameters: ffff80868206a010,fffff802b999b320,ffffffffc0000483,3; debugger CPU3; bucket AppleAgxRenderAdmission; stack dxgkrnl TdrResetFromTimeout and exact AdmissionDdiResetFromTimeout symbol.

Own-PDB/primary-source pool tagAGRm and sizeofADMISSION_CONTEXT7e658 locate unique rounded7f000 adapterffff80867fb13000. Captured Tdr: Active3/contextffff80867be22e00/submission935, schedulercompleted934,lastsubmitted935,active935,paging0,privateReset0,resetC0000483. Actual context has privateFence935,cancelFence935,cancelUncertain1,root9d48f4000,setRootCount1,commandVA b0000/bytes118. Backend G4Native1,BoundFence935,Ready1,JobReady0. Packet destinationGPUVA860000,CPUtokenffff99f002d10000,hostPA8e2d10000,bytesfd2000; visible destination fields0.

Exact source reset substep: private reset passed; active packet's matching privateFence935 triggers AdmissionGpuvaG3PrivateCancel(...,TRUE), SchedulerFaulted1 and STATUS_DEVICE_HARDWARE_ERROR rather than acknowledging an uncertain GPU completion. This explains failed recovery from TDR; it does not explain why native ticket935 did not complete. Monitored UMD fences are a separate namespace. No causal evidence ties this initiating timeout to VSync. No physical desktop or ordinary frame-update proof exists. Initial same-surface snapshotseq2/PA8e0110000 waszero/cache_clean0; DCP exactD589 swap10 occurred, but no late snapshots before loss.

Public dxgkrnl TDR type and dxgkdx extension unavailable on builder; private KMD PDB still provided exact ticket/reset branch. Remaining blocker is actual native GPU completion for ticket935 under root9d48f4000/private generation3. Do not enlarge pools/change caps/fake completion or shader-fix without packet-level evidence.

## Evidence and completed recovery

No frame/periodic workers started because firstCode0 SSH receipt never arrived. Monitor process group/children stopped, no trailing local snapshots. Original host logs/contract gated independently before recovery. Preferred immutable visible377/392 also reset beforeSSH; failed attempt separately archived, no unsafe cleanup. Hidden385 was therefore required. Hidden boot06:27:23.119296Z:Code45/exactoem5/909/armnull/serviceStopped/hashes verified.

14 recovery files plus2directdump sources independentlyhostsize/SHA checked; old093026 minidump retained but not attributed909. OriginalETL freeze unavailable; recoveryETL explicitlynotoriginal. Large kernel dump transferred directly tohost toavoid guestduplicates. Final precleanup37filemanifest9cc47ce82eafb3de5e6c9eeecc07ce8ba5cdb1e21d874fd567ab59bed2c2a5e1 passed BEFORE Code45 diagnosticcleanup07:02:27 andexactdevnode/oem5/service/SYS/UMD/signercleanup07:05:45. No liveCode0 removal.

Staging and cleanup both return without automatic shutdown; actual receipt files are retrieved and independentlyhost-gated before separate power transition. First cleanuprestart gate stopped below4GiB and did not continue. Exactly twoownstopped157286400B recoveryETLfiles, bothguestrecheckedSHA4ec688cd3c65355531aee95721b711e0a1ea0f5a03b0d664d9a3d7bd5f283dc3 andhostverified, removed07:11:14; free4.146->4.461GB. Kernel/olderminidumps retained. Orderedrestart accepted07:12:39 after linkedcleanup receipts.

Final immutable ordinary377/392 boot07:15:28.519375Z has two durable receipts07:16:45/07:20:17: Code28/exactlyoneinertAPPL0002/package0/arms0/SYS0/UMD0/service0/signer0/diagnostics0/CPU8/disks2OK/USB5/SSH/RDP/autologon1/passwordpresent; final Cfree4466741248B. Autologon unchanged. Ordinary launcher retained; no collector/candidate rearm. No accepted AppleAgx package. Exact symbols/package/manifests and immutable ordinary/hidden rollback artifacts retained.

Artifacts under repository root.local/experiments/EXP909-render-local-view: hardware-result.json,source/build/sign/PDB manifests,RED/GREEN andbaseline evidence in root.local/analysis/EXP908-display, originalfull.log/contract.bin,visible-recovery-attempt1,hardware-evidence-hidden, dump-analysis-cf.txt/adapter-state.txt/packet-owner.txt, final-host-manifest.json and ordinary-durable-first/final.json. AFTER ledger and compactstate close this experiment; next causal target is native ticket935 completion, separate from fail-closed reset refusal.
