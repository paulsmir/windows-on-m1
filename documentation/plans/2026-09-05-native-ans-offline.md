# Native ANS offline implementation plan

**Goal:** implement the supplied native physical StorPort/EL2 design up to the
first read-only WinPE candidate, without any access to the MacBook Air.
**Spec:** J313_NATIVE_ANS_REQUEST.md. **Audit:** ../NATIVE_ANS_STORAGE.md.
**Execution:** inline, isolated root/m1n1/Mu worktrees; deterministic tests only.
No reordering of runtime prerequisites; host-only helpers do not qualify hardware.

- [x] Inspect current source and record architecture gaps and ownership.
- [x] Isolate all three repositories from parallel GPU work.
- [x] Verify and separately preserve EXP377/392 recovery pair and EXP474 receipt.
- [ ] Canonical ANS config, resource collision validation and generated ABI/ASL.
- [ ] Exclusive ownership and broker generation/tag state machine, host tested.
- [ ] Common backend refactor preserving synchronous recovery wrappers.
- [ ] Async shared-tag engine; full-range stage-2/PRP authorization.
- [ ] Dedicated physical IRQ path and Mu ANS0 enumeration.
- [ ] Truthful bounded controller stop and pre-EBS handoff.
- [ ] Physical StorPort project with pinned WDK, admin and Identify engine.
- [ ] SCSI translation, SG→PRP, async SRB completion, read-only enforcement.
- [ ] Offline suite, ARM64 builds, code analysis, INF and signing verification.
- [ ] Record exact ANS001 candidate and recovery manifest; request permission.

V2 direct hardware queues, writable tests, native C: boot, hibernation, crash
dump and performance increases retain the hardware prerequisites in the spec.
Do not describe them as complete offline, or bypass those prerequisites.

Test admission question: WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Tests enforce duplicate ownership, stale generation, double submit/completion,
wrong queue/tag, DMA range/permission violations, PRP corruption and exact ABI
layout. A profile cannot become hardware-launchable on source tests alone.
