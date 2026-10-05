# GPU engineering procedure

This is the complete operational loop, not a substitute for AGENTS.md or the
full acceptance mission. Main process owns implementation; no new agents.

## 1. Resume and choose scope

Read GPU_CURRENT_STATE.md first after context reset, then latest exact handoff,
AGENTS.md, applicable roadmap/spec and only relevant ledger/evidence entries.
Inspect HEAD, branch, staged/unstaged/untracked state and tool/process reality.
Do not assume a previous launcher/build is alive; verify its handle/process.
Do not restart on an observation timeout. Preserve previous artifacts.
Classify last work as progress, verified wait or no progress. Execute an action
that advances the final desktop contract, not merely another easy helper test.

## 2. Source-first contract and ownership

Write: REFERENCE/CONTRACT -> OUR BEHAVIOR -> DIFFERENCE -> WHY IT MATTERS ->
OFFLINE PROOF -> ACTION. Windows-facing contracts use pinned WDK26100 and
official Microsoft docs; hardware protocols use current Asahi/m1n1 and proven
traces. Inspect live state only when the question depends on it, then relevant
Asahi/device-tree, m1n1, Mu/ACPI and Windows expectations. Do not guess values.
Classify graphics references DISPLAY_ONLY, RENDER_ONLY or FULL GRAPHICS.
Document initialization/runtime/IRQ/DMA/power/recovery owner and lifetime.
License-review external source before reuse; keep immutable references clean.

## 3. Implement and test

One causal correction or documented indivisible contract; do not modify unrelated
firmware/UAT/DCP/caps. Preserve known lower-layer PASS absent contrary evidence.
Use production/shared functions for executable tests, not a duplicate renderer.
For deterministic defects: reproduce RED, minimal correction, GREEN/regressions.
For instrumentation/build repairs: meaningful existing tests suffice; no fake RED.
Check normal/error/retry/overflow/stale generation/busy teardown and ownership.
Keep render fence, completed output and display retirement separate.
No heavy I/O or pixel scans under spinlock/DIRQL; distinguish captured/exported/
durable and verify persistence result. Independent pixel oracle, never derive
expected values from output. Never fake successful DDIs or loosen validation.

## 4. Build gates

Use existing pinned FRYZZING paths in latest handoff. MSVC14.44.35207,
WDK26100. External compiler additions need pinned provenance/hash and isolation.
Freeze source commit plus exact selected overlay manifest/hashes; never archive
the entire dirty tree as a candidate. Keep immutable prior artifacts/recovery.
Run relevant executable host tests, ARM64 build, owned-code analysis, applicable
Universal validation, Inf2Cat/signing and exact SYS/DLL/CAT/INF/package hashes.
Static-library compilation != linked UMD != executed test != hardware PASS.
Signed experimental artifacts are not release artifacts. Keep warnings explicit.

## 5. Hardware hypothesis gate and ledger

Only run hardware for actual unknown hardware/dxgkrnl behavior or to validate a
complete offline-designed mapping. Record before run in EXPERIMENTS.md:
unique ID/UTC, WHY THIS HYPOTHESIS (1–3 concrete facts), single changed variable,
WINDOWS CONTRACT, AGX/ASAHI CONTRACT, TRANSLATION, WHAT IS STILL UNKNOWN,
source/root/m1n1/Mu commits, diff hashes, exact build/install/launch commands,
artifact manifest/SHA, recovery path, expected checkpoints/failure criteria and
evidence locations. ATOMIC CONTRACT required for coupled fields/DDIs.
No candidate without recorded hash. No blind repeat of rejected/inconclusive EXP.

## 6. Preflight / control planes / recovery

Check Windows SSH with bounded timeout and pinned known_hosts, plus proxy/vUART
USB endpoints and active launcher before requesting physical action.
Expected proxy `/dev/cu.usbmodemC02HDNCCQ6L41`, vUART suffix43; enumerate actual
ports rather than assuming unchanged names. Last launcher76592 is historical.
Ordinary GPU-visible recovery is377/392 with exactly one inert APPL0002 Code28,
broker disabled and no AppleAgx package/service/module/SYS/UMD.

Ordinary artifacts:
`.local/experiments/EXP-20260903-377-secondary-cpu-receipt/assisted-boot/m1n1.macho`
and `.local/experiments/EXP-20260903-392-current-gpu-mu-publication/assisted-boot/J313_EFI.fd`.
Current hardware full-owner reference:
`.local/experiments/EXP584-kmd-output/m1n1.macho` and
`.local/experiments/EXP-20260904-406-coherent-abi-admission/J313_EFI-exp406.fd`.
Validate hashes against original manifests before use. Reuse latest successful
launch/stage/cleanup scripts; this document does not invent launch arguments.

Collect previous evidence, remove only exact rejected package/state using
established hash-gated procedures; inspect exact OEM INF/devnode first.
Stage only next preregistered package. Use ordinary GPU-visible guest between
experiments; compatible validated GPU-hidden emergency only if ordinary cannot
recover. Never old incompatible recovery artifacts, broad delete or new
destructive recovery. Preserve crash evidence before recovery where possible.
If a control path fails, inspect and choose next previously validated safe path;
do not blind retry. Ask only for unavoidable physical action, required approval,
secret, exhausted safe paths or unapproved irreversible action; first finish
independent offline work and reduce request to one minimal action.

## 7. Execute, correlate, classify

One intended candidate/natural bind. Save exact producer requests/statuses,
device state, per-context/allocation/generation/call/fence correlation, Render/
Patch/adoption/Submit/worker/physical completion/Notify/DPC, raw immutable output
and independent decoding, Present purpose/surface/sequence/A408/D589 when relevant.
Flush producer stdout phase markers. Missing receipt alone != missing callback.
Query success != operation completion; STATUS_TIMEOUT is not completed success.
Physical visible confirmation is separate from machine latch proof.
Record CPU/input/storage/SSH health and Event41/1001/129 timestamps/counts.
Event129 is telemetry absent reproducible causal GPU evidence; do not touch ANS.
Classify CONFIRMED/REJECTED/INCONCLUSIVE/SUPERSEDED; launch failure is not a GPU
verdict. Save dumps/stop parameters/CPU progress without guessing fault owner.

## 8. Cleanup, ledger, next action

Preserve raw evidence first, exact cleanup and restore/verify ordinary baseline
unless working package retention is explicitly the variable. Never free active
or ownership-unknown display allocations on timeout; use proven retirement or
recovery. Update actual result in EXPERIMENTS.md and compact GPU_CURRENT_STATE.md.
Implementation commit first; append CHANGES.csv row with full40-char commit,
artifact SHA and verification. `implemented` for offline; `validated` only with
hardware evidence/EXP. Corrections append; never rewrite history as success.
Run `tests/test_change_ledger.py`. Commit no assistant attribution trailers.
Choose first new causal boundary and execute it. At most two equivalent offline
comparison passes; no archaeology loop or receipt-only run with known-bad transport.

## 9. Communication, platform limits and stopping

Short updates during work: phase, confirmed fact, mismatch/change, verification,
next executable action. Honest separation of software/hardware readiness.
No estimated completion promises unsupported by integration evidence.
If platform blocks an action, do not bypass/rephrase/retry it; isolate that action
and continue permitted independent work. A refusal is not technical evidence.
User can explicitly pause for handoff (as in this checkpoint). Otherwise individual
EXP/helper/build/triangle/Present is not mission completion.

## 10. Final acceptance and final installed state

Audit every FULL_GRAPHICS_MISSION requirement against fresh authoritative evidence.
Verify real runtime hardware device, dynamic draws, physical TA/3D/fences,
standard DXGI Present, DWM context correlation, visible interactive desktop,
1000 Presents/100 lifecycle cycles/30min stability/reset-reentry and platform health.
Leave final known-good KMD/UMD installed, enabled Code0; no Code28 rollback.
Report final package/version/hash, feature level, device/draw/TA3D/fence/Present/
DWM/desktop/stress/reset/stability results, limitations and current machine state.
Only then mark primary goal complete. OpenGL/CS1.6 is subsequent work.
