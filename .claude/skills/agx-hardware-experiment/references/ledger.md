# Ledger formats

`investigation/EXPERIMENTS.md` is append-only and is the only durable record
of a run; chat history is not. Each experiment gets exactly these lines, in
this order, each a single paragraph. Timestamps are UTC from
`date -u +%Y-%m-%dT%H:%MZ` taken when you write the line.

## BEFORE (written and committed before the run)

```
EXP<N> BEFORE <UTC> (<single variable vs EXP<prev>: what changed and in which commit>; package from <commit8>, PACKAGE<N>, AppleAgxRenderAdmission.sys <sha256>, AppleAgxRenderAdmissionUmd.dll <sha256>; m1n1 <commit8>; manifest/hardware-manifest/full-owner<N>.sh <16 hex> <16 hex> <16 hex> ; <k>/<k> staged; recovery rec<N>h/recover.sh). <ICD/tool artifacts with hashes if any>. Probe <probe>: <what it runs, in which order>.
WHY THIS HYPOTHESIS: (1) <evidence> (2) <evidence> (3) <evidence>. Expected: <checkpoint>. Failure: <criterion -> what it implicates -> rollback>.
```

The three short hashes are the first 16 hex digits printed by the last line of
`prepare.sh` (`shasum -a 256 manifest.json hardware-manifest.json full-owner<N>.sh`).
For architectural runs add the `WINDOWS CONTRACT:` / `AGX/ASAHI CONTRACT:` /
`TRANSLATION:` / `WHAT IS STILL UNKNOWN:` lines (and `ATOMIC CONTRACT:` when
several fields change together) between BEFORE and WHY.

Real example (diagnostic run, unchanged package):

```
EXP1152 BEFORE 2026-10-10T10:20Z (diagnostic; PACKAGE1151 artifacts copied unchanged, AppleAgxRenderAdmission.sys 407bffbc...069c; m1n1 98cdabae; manifest/hardware-manifest/full-owner1152.sh ca96c559b35c3f3e a1a16c9c965bef6a 6ea25250108b1cce ; 16/16 staged; recovery rec1152h/recover.sh). No ICD. Probe probe1152.sh = probe1143.sh exactly (same loads, order, Settings cycle and receipts).
WHY THIS HYPOTHESIS: EXP1150 and EXP1151 both measured typing ~87-89 % and charmap drag ~92-94 % one-period, below EXP1143/EXP1144 (94-95 % / 98.5-99.3 %), but under a different probe order (...). Running the EXP1143 probe unchanged on the new package separates the two causes: equal to EXP1143 -> the order explains it and the package is clean for the desktop; still low -> one of the package changes is not dormant.
```

(The real entry spells out full SHA-256 values; they are shortened here.)

## HARDWARE ACTUAL (after the run, before or after rollback)

```
EXP<N> HARDWARE ACTUAL <UTC>: <what ran>; <device state: CM_PROB_*, DWM pid alive/restarted, rejects, TDR/stop code + parameters, dumps>; <numbers with units, each next to its baseline in parentheses>; <which of display, input, SSH, storage, CPUs were alive if anything failed>. Verdict: CONFIRMED|REJECTED|INCONCLUSIVE -- <one sentence>. Next: <one experiment justified by this evidence>.
```

Real example:

```
EXP1152 HARDWARE ACTUAL 2026-10-10T10:29Z: probe1143 order on the EXP1151 package (no ICD): CM_PROB_NONE, DWM pid1200 alive, 0 rejects/device-terminal, Settings cycles clean. Notepad drag 59.1-60.0 / 98.4 % (EXP1143 57.3-59.1 / 97.1 %), typing 57.1-57.5 / 94.9 % (57.1-58.2 / 95.6 %), Explorer 54.6-60.0, charmap drag 59.9-60.0 / 99.3 % (59.6-60.0 / 99.2 %), same-size drag 57.3-58.1 / 95.2 % (55.9-57.6 / 93.8 %). Verdict: CONFIRMED -- the new package leaves the desktop at the EXP1143/EXP1144 level; the EXP1150/EXP1151 deviation came from the probe order. Next: rollback; GL depth-test correctness and a first hl.exe run.
```

`a-b / p %` means: flips/s range over the steady 10 s windows
(`analysis/vsync_fps.py`) / share of one-vblank latch intervals
(`analysis/latch_pacing.py`).

## Recovery

```
REC-EXP<N> ACTUAL <UTC>: rec<N>h/recover.sh RECOVERY_DONE, Preflight PASS {BeforeBoot <hh:mm:ss>Z, AfterBoot <hh:mm:ss>Z, manifest <8 hex>}.
```

`BeforeBoot`/`AfterBoot`/`manifest` come from the preflight JSON that
`recover.sh` prints last. A failed recovery is recorded the same way with the
stop marker it printed, and nothing else is booted until the user decides.

## Corrections

Never edit an earlier line. Append:

```
EXP<N> CORRECTION <UTC>: <what was wrong in which line> -> <correct value/source>.
```

## CHANGES.csv

Header (schema enforced by `tests/test_change_ledger.py`):

```
record_id,date_utc,repository,branch,commit,change_type,title,description,reason,problem_fixed,reproduction,implementation,verification,hardware_result,artifact,artifact_sha256,status,related_experiment,notes
```

- `change_type`: `feature`, `correction` or `process`.
- `status`: `implemented` (software-verified), `validated` (only with a
  non-empty `hardware_result` and `related_experiment`), `rejected`, `superseded`.
- `commit`: the 40-hex implementation commit (not the ledger commit).
- `reproduction`: how to get the old behaviour back, usually `git revert <commit>`.
- One line per row, RFC 4180 quoting for fields with commas.

Real validated row:

```
CHG-20261010-G4-ZLS-HW,2026-10-10T10:29:33Z,public_windows,integration/ad04-windows-compiler,7429bb182e39fd944f463fb3313b6e329d1eeaff,feature,G4 ZLS attachments and 1..3 attachments (hardware validation),"G4 ZLS attachments and 1..3 attachments (hardware validation)",EXP1151: GL depth smoke 300 frames 60 fps correct; EXP1152: desktop unchanged,No depth on the G4 path,git revert 7429bb182e39fd944f463fb3313b6e329d1eeaff,see commit,"EXP1151 + EXP1152","EXP1151: GL depth smoke 300 frames 60 fps correct; EXP1152: desktop unchanged",.local/experiments/EXP1151-g4-zls-attachments/package/AppleAgxRenderAdmission.sys,407bffbc47b014f94d1f71486b123726f43f963e5fe8634b0aee10b82a18069c,validated,EXP1151,validates CHG-20261010-G4-ZLS and CHG-20261010-G4-ATTACHMENTS
```

## GPU_CURRENT_STATE.md

Add a new section at the top (`## Current as of <UTC> (read this first)`) and
rename the previous one to `## Earlier: <UTC>` only when one of these changed:
a proven boundary, the best validated launch, the stable recovery, the active
hypothesis, a verdict, or the next causal target. Keep it short; details stay
in EXPERIMENTS.md.
