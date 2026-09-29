# J313 GPU current state — 2026-09-29

EXP885 = exactEXP884 a16cce1e + R165, candidate23de3f579bbb062d8c22fa98469d97381a25b23f. Original boot625.1s Code0/CPU8/DWM1232/no kernel bugcheck; scanoutseq2 zero; Present unproven. DWM private ACQUIRE HRESULT8876017c remains. Evidence17/17 host size/SHA verified; manifest459432db8a63bd0bd4b2c12368bfd0fc918703b4562bbcd481a9936d12c75e55. No WER dump collected.

Recovery: original ordered restart stalled with SSH alive; forced restart1115; launcher SIGTERM snapshot/reset used after evidence verification. HiddenCode45 exact885oem5/SYS/UMD stopped verified; diagnostic and exact package cleanup passed; ordered cleanup restart completed. Ordinary durableCode28 recovered20:12:53Z boot20:10:38Z, package0/service0/signer0/diagnostics0/CPU8/disks2OK/USB5/RDP running. Autologon untouched. Never remove liveCode0 package.

Next authorized EXP886 = EXP885 + R164 driver code only, candidate0fce102d9575ce4cbfca9b2dc8945e304b24a318, 32MiBprivate pool and984MiBWindows local segment; unchangedR143firmware. Built0/0, source/native provenance matches, PDBs preserved, audit653/653. Not staged. Await ordinary durable clean baseline before preregistration/stage/one600s launch.

WHY CONTINUE COMPARISON: EXP885 removes the observed250s FLUSH_TLB failure within625s, but DWM private-pool exhaustion and zero scanout remain; R164 addresses that measured allocation limit as the next single variable.

Sources/evidence: integration investigation/EXPERIMENTS.md EXP884/885 entries; .local/experiments/EXP885-flush-wait/hardware-evidence-final and hardware-result.json; .local/tandem/HANDOFF-CLAUDE-20260929.md. Goal600s without bugcheck AND nonzero scanout/Present remains unmet.
