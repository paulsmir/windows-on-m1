# Effective AD04 compiler workflow — user policy 2026-09-12

This supersedes the routine multi-cloud-agent/reviewer/worktree-per-task parts
of earlier Phase1–3 procedures for the current compiler-portability phase.

Roles: one Tier-A architect, one GPU-resident local Devstral worker, deterministic
runner. No additional subagents or separate review for ordinary mechanical fixes.
All earlier cloud workers have returned. Do not redispatch them routinely.

Use the continuous integration/ad04-windows-compiler worktree for this phase.
Record/checkpoint the exact clean input HEAD per task; do not create another
agent or worktree for each include, guard, constant or dependency correction.
Architect checkout and native-ANS remain untouched. No push/hardware authority.

Routine flow:
compact bounded contract -> Devstral structured exact replacement -> runner
schema/scope/git apply validation -> fixed approved x64/ARM64 checks -> compact
first-error result -> local checkpoint -> next mechanical boundary.
Avoid a separate architect conversational gate after a successful mechanical
step. Keep per-task changes causal; do not opportunistically fix later errors.

Reviewer required only for ABI/layout, AGX/NIR semantic change, >2 production
files, behavior not fully validated deterministically, low worker confidence,
or an explicitly architectural task. This condition takes precedence over
token-saving pressure; ordinary compiler blockers do not trigger review alone.

Raw evidence remains on disk. Read it only for failed tests, conflicting results,
scope expansion or ambiguous architecture. Compact results must distinguish
translation-unit compile, linked compiler executable, and hardware proof.

Mechanical compilation, hashing, manifest generation and source checks execute
in fixed scripts without LLM inference. Devstral proposes/clarifies code only
when useful. Maximum one local inference at a time, context4096, validated GPU
residency before and after, keepalive10m, no CPU fallback or model substitution.
Prefer exact old/new replacements plus deterministic diff generation. One format
repair attempt then bounded escalation; never spend a long loop repairing diffs.

Cost record per task: local input/output tokens when returned, raw bytes,
compressed bytes, raw-read reason, rework count, result. Frontier savings are
not yet quantified; judge throughput and rework over subsequent comparable
tasks instead of asserting a percentage from local token counts.

Current checkpoint: math constant configuration fixed; all normalized focused
tests and x64/ARM64 agx_compile.c checks pass. Full compiler dependency build/
link and executable shader tests remain the next boundary. Hardware is untouched.
