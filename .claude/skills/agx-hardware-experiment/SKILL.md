---
name: agx-hardware-experiment
description: Run, analyse or roll back an AppleAgx GPU driver hardware experiment on the J313 (MacBook Air M1) Windows guest in this repository, end to end - commit to package (builder), stage, probe, evidence analysis, recovery, and the EXPERIMENTS.md / CHANGES.csv ledger - with the lab tooling in tools/agx-lab. Use it whenever the work involves the Air, the guest, the builder, an EXPnnnn experiment, "running it on hardware", staging or installing a driver package, desktop fps/pacing/stutter measurements, the GL/ICD smoke test, receipts (Wom1*.bin), vsync traces, ETW/xperf profiles, recover.sh/rollback, or writing BEFORE/ACTUAL ledger entries - even when the user only says "test it", "measure it" or "check the desktop".
---

# AppleAgx hardware experiment

A hardware run is the most expensive and least reversible step in this
project: one J313, one guest, one builder, minutes per boot, and a wrong
package can leave Windows unbootable. Every rule below exists because a past
run lost time or evidence without it. The project rules in `AGENTS.md`
(hypothesis gate, one variable, ledger, change discipline) govern; this skill
is how to satisfy them with the tools in `tools/agx-lab/` (see its README).

## 0. Orient

1. Read `investigation/GPU_CURRENT_STATE.md` (top section): the best validated
   launch, the active hypothesis, the next causal target.
2. Find the last experiment and its directory: the tail of
   `investigation/EXPERIMENTS.md` and `ls $AGX_EXPERIMENTS | tail`. The new
   experiment is the next number; it is cloned from the last experiment that
   staged and recovered cleanly (normally the previous one).
3. Check the lab configuration loads: `. tools/agx-lab/lib.sh && echo ok`.
   If `.local/agx-lab.env` (in the main checkout) is missing, create it from
   `tools/agx-lab/lab.env.example` and ask the user only for values you cannot
   find. Shell snippets below assume `lib.sh` is sourced in the same command
   (it defines `$AGX_EXPERIMENTS`, `guest`, `guest_alive`, ...).

## 1. Gate: is a hardware run justified?

Do not run hardware to guess a documented contract. Before packaging, have:

- exactly one changed variable relative to the clone source (or an
  `ATOMIC CONTRACT:` when Microsoft/WDK defines several fields as one invariant);
- `WHY THIS HYPOTHESIS:` with 1-3 concrete evidence items, and for
  architectural changes the four sections `WINDOWS CONTRACT:`,
  `AGX/ASAHI CONTRACT:`, `TRANSLATION:`, `WHAT IS STILL UNKNOWN:` (empty last
  section means: finish offline instead);
- the relevant offline tests GREEN and the change committed (the package is
  built from a commit; the source manifest records it);
- an expected result and a failure criterion that will separate the causes.

Diagnostic-only runs (same package, different probe) are fine when the probe
itself is the variable; say so in the ledger. A preregistered series (R54 in
`AGENTS.md`, `investigation/SERIES_RUN_TEMPLATE.md`) has its own contract and
short per-run entries; this skill covers single experiments.

## 2. Make sure you own the hardware

The J313, its serial ports and the builder are single-owner. Another session
running a probe or recovery means a reboot from you destroys its evidence.

```bash
. tools/agx-lab/lib.sh
pgrep -fl 'run_uefi|run-probe|recover.sh|full-owner|launch-hidden|chainload'
guest_alive && echo guest-ssh-up; serial_ports_present && echo proxy-ports-present
```

Also check linked/peer sessions (ListAgents, the linked-sessions note) for one
that is mid-experiment. If someone else owns the lab, stop and coordinate; do
offline work instead. Never touch another session's builder directory.

Then check the control planes yourself before asking the operator for
anything (the last line above). Ask for physical action only when Windows is
unreachable and the machine is not at "Running proxy", and state that
evidence.

## 3. Package

Name the experiment `EXP<N>-<short-kebab-name>`; the package build number and
driver version are `<N>` and `30.0.<N>.0`.

- New driver code: `python3 tools/agx-lab/experiment/package_source.py EXP<N> <dir> <N> --repo <checkout at the commit>`,
  then `bash tools/agx-lab/experiment/builder.sh build <dir>` **in the
  background** (tens of minutes; the last line must be the build PASS line).
- Unchanged package (probe-only or ICD-only run): `builder.sh copy <prev dir> <dir>`.
- ICD (OpenGL) runs additionally need the ICD directory (opengl32.dll,
  libgallium_wgl.dll, agx_gl_smoke.exe, x86 VC runtime) built with
  `drivers/apple-agx/windows/icd/build-icd-x86.ps1`, recorded with hashes.

## 4. Stage

```bash
bash tools/agx-lab/experiment/prepare.sh <prev N> <prev dir> <N> <dir> "<purpose>"
```

Read the whole output. Continue only when it shows: the receipt hash lines (no
`MISMATCH`), `leftovers: none`, `HASHES_MATCH <n>`, a clean stage tail, and
`EXP<N>_ARTIFACTS_AND_STAGE_PASS`. The stage gate also refuses a guest disk
below the floor; free space by archiving/compressing old traces, not by
deleting evidence (see references/pitfalls.md). `recovery references:` lists
the fixed recovery images the derived `recover.sh` will boot; they must be the
known-good ones.

## 5. Ledger BEFORE (commit it before the run)

Append the `EXP<N> BEFORE` entry and its `WHY THIS HYPOTHESIS:` paragraph to
`investigation/EXPERIMENTS.md` exactly as in references/ledger.md: variable,
commit, package hashes, m1n1, manifest/hardware-manifest/launcher short
hashes, staged count, recovery script, probe, expected result, failure
criterion. Take timestamps from `date -u +%Y-%m-%dT%H:%MZ` at the moment you
write; never estimate them. Commit (no assistant attribution trailers).

## 6. Run

Choose or compose the probe (references/probes.md). The canonical desktop
control is `probes/desktop-loads.sh`, and it goes first, before any other GPU
client.

```bash
bash tools/agx-lab/experiment/run-probe.sh <dir> <N> EXP<N> tools/agx-lab/probes/<probe>.sh [args]
```

Run it with `run_in_background` and wait for the completion notification; it
takes 10-30 minutes. Do not poll with sleep loops and do not start anything
else that touches the guest meanwhile. Exit 1 = refused before launch (nothing
booted); exit 2 = Windows never came up (go to recovery). `RUN_DONE` ends a
normal run.

## 7. Analyse, then write ACTUAL

Evidence is in `$AGX_EXPERIMENTS/<dir>/evidence/EXP<N>/`. Start with
`probe.txt`, `state-start.txt`/`state-end.txt` (device problem code,
DWM/explorer pids, uptime: a changed DWM pid or a short uptime means a
restart or reboot happened), the `reject-` counts and receipts. Use the
recipes and baselines in references/analysis.md; compare against the
baseline experiments named there, not against memory.

Append `EXP<N> HARDWARE ACTUAL <UTC>`: what ran, numbers with units and the
comparison baseline, any stop code/dump/TDR, which control planes were alive,
and `Verdict: CONFIRMED|REJECTED|INCONCLUSIVE -- <one sentence>`, then
`Next: <the single next experiment the evidence justifies>`.

## 8. Roll back, always

```bash
bash $AGX_EXPERIMENTS/<dir>/evidence/rec<N>h/recover.sh   # background, ~5 min
```

It boots the GPU-hidden recovery image, removes the experiment package,
refuses to continue unless cleanup is verified (the normal profile must never
boot with a package bound), boots the normal GPU-visible profile and runs the
durability preflight. Success is `RECOVERY_DONE` plus `Preflight PASS`. Record
`REC-EXP<N> ACTUAL <UTC>: rec<N>h/recover.sh RECOVERY_DONE, Preflight PASS {BeforeBoot, AfterBoot, manifest}`.
If it stops (`HIDDEN_SSH_NEVER_UP_STOP_BEFORE_NORMAL`,
`HIDDEN_CLEANUP_UNVERIFIED_STOP_BEFORE_NORMAL`), do not improvise another boot
path: record it, report it, and ask.

Roll back after failures too: the next experiment must start from the clean
GPU-visible profile with no AppleAgx package carried over.

`AGENTS.md` prefers replacing packages inside the stable GPU-visible guest and
keeping the GPU-hidden image for emergencies; the derived `recover.sh` still
takes the hidden path because the normal profile must not boot with a package
bound (EXP1049). Follow `recover.sh` as derived unless the user changes that
policy, and do not mix the two paths within one rollback.

## 9. Bookkeeping

- `investigation/CHANGES.csv`: a `validated` row (hardware result + EXP id)
  for each change the run validated; `implemented` rows stay for software-only
  verification. Never edit an old result; add a row and mark the old one
  `rejected`/`superseded`. Run `python3 -m pytest tests/test_change_ledger.py -q`.
- `investigation/GPU_CURRENT_STATE.md`: add a new top section only when a
  proven boundary, best validated launch, active hypothesis or next causal
  target changed.
- Corrections to earlier ledger lines are appended, referencing the original
  ID; never rewrite them.
- Commit the ledger; push only if the user approved pushing.

## References

- `references/ledger.md` - exact BEFORE / ACTUAL / REC / CHANGES.csv formats with real examples.
- `references/probes.md` - what each probe does and collects, durations, writing a new probe.
- `references/analysis.md` - baselines, analysis recipes, receipts, ETW profiling, dumps.
- `references/pitfalls.md` - traps that already cost runs, and their fixes.
