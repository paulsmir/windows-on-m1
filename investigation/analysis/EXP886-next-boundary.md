# EXP886: next causal boundary

Both authorized candidate packages reached >600 seconds without a kernel bugcheck. Neither produced nonzero scanout. R165 removes the observed EXP884 FLUSH_TLB failure within these windows; R164 does not suffice for Present.

Exact candidate: 0fce102d9575ce4cbfca9b2dc8945e304b24a318, EXP885 plus R164 three driver paths. Firmware R143 unchanged. Full experiment and transition record: investigation/EXPERIMENTS.md. Raw evidence: .local/experiments/EXP886-private-pool/hardware-evidence-final; host size/SHA verified19/19,901714332B before cleanup.

## DWM failure proven, writer unknown

Original DWM1228 faults around303s with C0000005 in dwmcore!KeyframeSequence::Calculate+298. It executes ldr x8,[x20], then ldr x9,[x8,#0x20]. x20=245002e01c0 belongs to an allocated LFH object (requested24B, block32B); its stored first QWORD is2e427268, the low32 bits of a dwmcore address7ffd2e427268. Read fault2e427288. CopyFrom immediately before this reads a separate expression source and branches into SetValue; that disassembly does not identify a writer of the corrupted object. Separate uDWM Event Thread is retiring buffers via AdmissionUmdScreenDestroyBuffer -> runtime DeallocateCB, but contemporaneous teardown is not causal proof. No owning layer or deterministic reproduction is established. Do not invent a RED test or patch this pointer based only on the stack.

WER dwm.exe.1228.dmp311898108B SHA256cb19d7adbb4b7edae532009c18d958fec6a69cafe278c08712a36e75d0df5491. Exact package886 PDBs preserved with preserve-symbols.ps1 before other builds. debug-dump.py outputs dwm-analysis.txt, dwm-pointer.txt, dwm-heap.txt, dwm-copy-source.txt; no kernel bugcheck dump from this run.

## Earliest Present blocker remains private ACQUIRE

Verified UMD logs still contain private-escape HRESULT8876017c for DWM1024x1024 and other process16x16 requests. This does not identify which STATUS_INSUFFICIENT_RESOURCES branch failed. Offline one-pass inspection: gpuva_g3_windows.c AdmissionGpuvaG3PrivateEscape can fail private table setup, CPU scene allocation, PrivatePrepare, or mapping publication. Shared private_pool.h has512 global64KiB units but128 per-owner units; private_storage.h retains4MiB manager heap plus six scene extents. Existing private_storage_test explicitly permits two full-size scenes and refuses a third. Therefore another pool increase alone is unjustified; global capacity, per-owner budget, retained scenes, and table/map allocation are still distinguishable alternatives.

Smallest next observation for supervisor review: bounded first-private-ACQUIRE-failure receipt captured at the owning KMD branch, including status/branch, process/context identity, requested geometry and range bytes, global/owner allocated units, largest free extent, scene count/queued/release-requested state, and any table/map failure predicate. Record receipt only; retain exact EXP886 behavior, firmware, caps, signer and recovery. It distinguishes capacity/quota/retirement from table/map failure. Do not run another hardware package before this receipt is designed, verified and preregistered. DWM writer tracing is a separate hypothesis and must not be bundled with that experiment.

## Recovery issues

Unforced EXP885 and forced EXP886 ordered restarts both stalled with SSH alive. Original evidence was verified before explicit launcherSIGTERM snapshot/reset. HiddenCode45 exact identity was verified before diagnostic/package cleanup. EXP886 staging attempt1 lost transferred state and reached only emptyCode28; it is inconclusive, never counted as driver execution. Separated staging captured/verifies receipt before ordered shutdown and yielded the actual accepted886Code0 boot. Autologon remains enabled.
