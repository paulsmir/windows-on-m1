# J313 GPU current state — 2026-09-29

Goal600s without kernelbugcheck AND nonzero scanout/Present remains unmet.

EXP885 (23de3f579bbb062d8c22fa98469d97381a25b23f = exactEXP884+R165) reached625.1s originalCode0/CPU8/DWM1232/no kernelbugcheck; scanoutseq2zero/privateACQUIRE8876017c. Evidence17/17 hostverified before gatedcleanup. Result commit426c395e; raw .local/experiments/EXP885-flush-wait/.

EXP886 (0fce102d9575ce4cbfca9b2dc8945e304b24a318 = EXP885+R164 three driver paths) reached624.7s originalCode0/CPU8/no kernelbugcheck, but scanoutseq3zero and privateACQUIRE8876017c persists. DWM1228 faults~303s C0000005 in dwmcore!KeyframeSequence::Calculate+298; replacementDWM6168 andExplorer4620 aliveat624s. WERdump SHA256cb19d7adbb4b7edae532009c18d958fec6a69cafe278c08712a36e75d0df5491; exact886PDBs preserved, debug-dump outputs identify allocated24Bheapobject containing invalidvptr2e427268 (low32bits ofdwmcore address); writerUNKNOWN. No justified driverfix or deterministic RED yet.

EXP886 stageattempt1lostverifiedtransfer andbootedemptyCode28/no886execution; record isINCONCLUSIVE andpreservedunderattempt1/. Separatedstagingverifiedreceipt beforeorderedshutdownthenactualcandidateboot passedexactpackageidentity. No candidatefailure retry/rearm.

Machine latestverified20:53:58Z boot20:51:54Z: ordinaryGPU-visibleCode28/exactlyoneAPPL0002; package0/arms0/SYS0/UMD0/service0/signer0/diagnostics0/CPU8/disks2OK/USB5/RDPserviceRunning; autologon1/passwordpresent. Recovery evidence .local/experiments/EXP886-private-pool/ordinary-durable-final.log. No liveCode0removal.

Bothoriginalorderedrestarts stalled, withandwithout/f. Evidence verified BEFORE explicitlauncherSIGTERM recovery snapshot/reset; hiddenCode45 exactpackageidentity and stoppedservice verified beforediagnostic/exactpackagecleanup; cleanuporderedrestartcompleted normally. ShutdowncausalownerUNKNOWN; do not callnormalrestartreliable.

WHY CONTINUE COMPARISON: R165 closes the observed250sFLUSH_TLBfailure within two>600swindows, but R164 doesnot close earliestprivateACQUIRE/Presentboundary. Oneofflinepass shows possible globalcapacity/per-owner128unitbudget/retainedscene/table-map limits; HRESULTalone cannot distinguish. Smallestnextdiscriminator isboundedfirstprivateACQUIREfailurebranch+allocator/scene receipt, behaviorunchanged. Do notincreasepoolagainorpatchDWMvtablewithoutwriter evidence. DWMwritertracing isaseparatehypothesis.

Report: investigation/analysis/EXP886-next-boundary.md. Claude review request: .local/tandem/REVIEW-REQUEST-EXP886.md. No EXP887 code/build/stage/run. Exactsource/manifests/BEFORE-AFTER/recovery: investigation/EXPERIMENTS.md EXP885/886, investigation/CHANGES.csv and .local/experiments/EXP886-private-pool/{hardware-result.json,hardware-evidence-final,evidence-host-verified.txt}. Evidence19/19verified901714332B; manifestdbd198f35beef25ca400dde0561b2653f9740c61cd7d5c62bfdedcc77d20844d.
