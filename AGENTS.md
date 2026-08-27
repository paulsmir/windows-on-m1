# Project Working Rules

## Source-first engineering

Before designing or changing platform, firmware, hypervisor, or Windows-driver
behavior, inspect the relevant primary implementations and specifications.

Required order:

1. Record the live J313 ADT, registers, interrupt routes, memory map, and current
   working/failing trace when the question depends on machine state.
2. Read the corresponding Asahi Linux driver and device-tree implementation to
   understand the hardware protocol, sequencing, power domains, clocks, DART,
   interrupts, and known quirks.
3. Read the current m1n1 implementation to understand which hardware state is
   inherited, initialized, preserved, virtualized, or shut down before guest entry.
4. Read the current Mu platform code and generated ACPI to understand what firmware
   exposes to Windows and what state survives ExitBootServices.
5. Read the relevant official Microsoft Windows/WDK documentation and samples to
   select the supported Windows driver, ACPI, power, graphics, audio, storage, or
   input architecture.
6. Compare the proposed path with the last hardware-validated assisted and
   standalone launch contracts before editing code.

For every implementation plan, record:

- the source files/specifications inspected;
- the observed hardware and software contract;
- which layer owns initialization, runtime operation, interrupts, DMA, power, and
  recovery;
- differences between Asahi, m1n1, Mu, and Windows expectations;
- the smallest falsifiable hardware checkpoint and its recovery path.

Do not guess register values, interrupt numbers, power sequences, DMA mappings, or
Windows interfaces when a primary source or live measurement can resolve them.
Treat evidence in this order: reproducible hardware observation, current source,
official specification/documentation, then an explicitly labelled hypothesis.

Asahi Linux and other external implementations may be used to understand observable
behavior. Do not copy code into this repository without an explicit license and
compatibility review.

## Change discipline

- Fix defects in the layer that owns the violated contract. Do not add a Windows
  driver to conceal an m1n1, Mu, ACPI, stage-2, timer, vGIC, or PSCI defect.
- Change one independently observable variable per hardware experiment.
- Add a regression test for every confirmed defect before or with its fix.
- Preserve a known-good recovery artifact before installing an experimental image.
- Keep production and diagnostic profiles behaviorally equivalent except for
  explicitly measured diagnostics.
- Do not add assistant attribution, session URLs, or `Co-Authored-By` trailers to
  commits.

## Anti-loop discipline

- Every next step must exclude a cause, confirm a cause, reduce the remaining
  causes, or produce the smallest experiment that distinguishes them.
- Stop a hypothesis after one complete offline pass if no new evidence changes
  its probability. Do not repeat the same diff, search, or log analysis in a
  different form.
- Do not spend more than two consecutive steps on one hypothesis without new
  evidence. Rank multiple differences by causal proximity to the current
  lifecycle boundary and test only the strongest one.
- Never run hardware without a new falsifiable reason. If offline analysis
  cannot distinguish two causes, design one minimal experiment that does.
- Restrict analysis to the current boundary. For `AddDevice` success followed
  by missing `StartDevice`, ignore RTKit, MMIO, UAT, IRQ, queue, render, and
  presentation behavior.
- Do not reread the complete ledger, refactor, clean up, or document incidental
  details unless that work changes the current causal decision.

After context compaction or reset, read `investigation/GPU_CURRENT_STATE.md`
first. Consult only the referenced experiment evidence in the full ledger.
Update that compact file only when a proven boundary, accepted package, stable
recovery, active hypothesis, verdict, or next causal target changes.

## Persistent experiment ledger

Every hardware build, launch, diagnostic run, and recovery attempt must be recorded
in `investigation/EXPERIMENTS.md`. This is mandatory even when the experiment fails,
hangs, reboots, or produces an inconclusive result. Update the entry twice: once
before the run with the intended experiment, and once after the run with the actual
result. Never rely on chat history as the only record.

Before a hardware run, record:

- a unique experiment ID and UTC timestamp;
- the falsifiable hypothesis and the single variable changed;
- repository, branch, root/m1n1/Mu commits, and dirty-state diff hashes;
- exact build command and launch/install command;
- artifact path, manifest profile, SHA-256, and recovery artifact;
- expected checkpoint, failure criterion, and evidence collection paths.

After the run, record:

- the observed boot phases and elapsed times;
- exact Windows stop code and parameters, relevant CPU, IRQ/timer state, and log or
  dump paths;
- whether display, USB input, SSH, RDP, storage, and all CPUs were alive;
- the verdict: confirmed, rejected, inconclusive, or superseded;
- the next experiment justified by the evidence.

Do not overwrite or silently reinterpret an old result. Append a correction that
references the original experiment ID. Do not call a build "working", "stable", or
"release" without linking the hardware experiment that demonstrated that claim.
Do not install or launch an artifact whose SHA-256 and manifest were not recorded.

## Machine-readable change ledger

Every feature, correction, or workflow change must have a row in
`investigation/CHANGES.csv`.  The CSV is the durable index for answering what
changed, why it changed, how the old behavior can be reproduced, how the change
was verified, and which exact artifact or hardware experiment supports it.

Required workflow:

1. Implement and verify one independently reviewable change.
2. Commit that implementation without assistant attribution trailers.
3. Append a CSV row containing the resulting 40-character commit hash.  A
   ledger-only bookkeeping commit does not require a row for itself.
4. Use `status=validated` only with a non-empty hardware result and related
   experiment ID.  Software-only verification uses `status=implemented`.
5. Never edit an old result to make it look successful.  Add a new row and mark
   the old row `rejected` or `superseded` when later evidence changes the verdict.

Keep every field single-line and valid RFC 4180 CSV.  Record artifact paths
relative to the repository where practical, and include SHA-256 whenever an
artifact exists.  The automated schema contract is
`tests/test_change_ledger.py`.
