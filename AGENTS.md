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
