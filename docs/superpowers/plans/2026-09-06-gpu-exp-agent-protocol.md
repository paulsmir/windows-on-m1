# GPU experiment-agent execution plan

**Goal:** execute the user's GPU project with a fresh executor for each EXP.
**Architecture:** sequential exclusive hardware ownership, controller evidence
review and next-boundary selection; no change to driver/platform architecture.
**Spec:** investigation/GPU_AGENT_PROTOCOL.md and the latest direct user request.

## Global constraints

One new executor per EXP. At most one executor controls Air/proxy/launcher.
No repeated failed candidate. Evidence before exact cleanup. Keep ordinary G2
GPU-visible and clean between experiments. Do not touch ANS worktrees/branches.
No hardware acceleration claim without physical execution and exact fence proof.

### Task 1: Complete delegated EXP500

Exact requirements: .local/experiments/EXP500-paging-patch/agent-brief.md.
Candidate is already built, signed, staged and preregistered. Parent performed
graceful guest restart but has not launched500. Executor must start from that
state, verify control planes and finish one run/evidence/cleanup/ordinary recovery.
Report: .local/experiments/EXP500-paging-patch/agent-report.md.

- [x] Dispatch fresh executor exp500_hardware with the exact brief.
- [ ] Review artifact identity, evidence/causality and cleanup after its report.
- [ ] Record the accepted boundary and select the smallest justified next EXP.
- [ ] Dispatch a new agent for that EXP; do not reuse500 as a new candidate.

Subsequent tasks are created only from observed evidence, not speculative GPU
layers. Continue through the original accelerated-driver goal; checkpoints do
not create human approval gates. A genuinely indispensable physical action or
new architecture choice outside authorization is reported precisely.
