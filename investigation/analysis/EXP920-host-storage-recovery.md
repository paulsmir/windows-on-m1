# EXP920 host evidence storage exhaustion and recovery

The external project volume `/Volumes/pwdev` reached capacity during EXP920.
The guest was still running Code0/CPU8. ENOSPC stopped frame polling and prevented
monitor final-state updates; periodic guest snapshots continued briefly but could
not be copied to the host. This is a host observation failure, not a GPU reset.

Host status at diagnosis: external300GiB/32MiB available, internal105GiB available.
The monitor final file remained at359s although its log contained good samples
through487s. The periodic worker was explicitly stopped after PID/path ownership
verification. The hypervisor launcher was not interrupted.

33 completed immutable evidence files were relocated: 32 frozen ETLs from
EXP916/917/918 and the EXP919 kernel dump. Destination:
`/Users/pavel/J313-evidence-archive/2026-10-02/`.
Each source was checked against its existing size/SHA receipt. Each destination
copy was fsynced and independently size/SHA verified before the source file was
removed and its original path replaced with a symlink. Reopening the original
path was verified again. No evidence bytes were discarded. The initial attempt
to create a temporary symlink failed ENOSPC before any source removal; resumption
used the already verified copy, then freed the original file and created its link.

`migration-plan.json` and append-only `migration-log.jsonl` in that archive retain
all source/destination paths, expected hashes and completed replacement records.
The project volume recovered about8.5GiB. Four guest snapshots left behind after
failed host transfers were recovered into EXP920/recovered-original-snapshots,
with independent receipt/file hashes and original boot checks; only then were
the exact guest duplicates removed. Partial original host copies were not
silently overwritten.

Same guest boot22:15:48.762806Z was verified at22:28:10 and22:30:25 (876.881374s),
Code0/CPU8. The final observation was restored with an explicit coverage-gap
annotation, allowing the already registered one-shot composition probe to run
once. It succeeded at22:32:12 and its output passed the host SHA gate.

Limits: do not claim continuous ETL/frame capture over the gap. The +600s UART
snapshot is unavailable if it was lost while tee writes failed; do not substitute
an earlier snapshot. UMD logs and the final stopped ETL must be collected before
recovery. User physical-screen observation remains pending.

Next-run workflow: verify at least5GiB free on the actual host evidence volume
before staging/launching. This budgets up to13 circular256MiB snapshot copies,
original/recovery collectors and a kernel dump. Pause host copying before free
space drops below512MiB, preserving space for control logs; relocate completed
verified evidence before resuming. These are host evidence controls, separate
from the existing guest4GiB/shadow0 gate. No GPU behavior change or artificial
RED test is required for this environment recovery.
