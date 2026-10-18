# Production context0 broker implementation plan

**Goal:** move current full inventory to retained broker, then advance hardware beyond application endpoints without replaying management qualification.
**Architecture:** existing allocations/layout plus one broker-owned retained root; portable inventory/lease journal, no second UAT implementation.
**Tech Stack:** C/WDK, current m1n1, ASan/UBSan, pinned FRYZZING.
**Spec:** docs/superpowers/specs/2026-09-05-production-context0-broker-design.md

## Global Constraints

No copied/replacement production context0 root. No context63/HVC/IRQ/scheduler/scanout changes. Preserve firmware/private/system ownership and existing backing policy. First hardware candidate is full current Windows inventory plus application endpoint starts, no initdata/workload. All experiments preregistered and cleaned. Keep existing user workspace and unrelated dirty changes.

### Task 1: Saturate broker for production inventory

Only edit m1n1_windows/src/hv_agx_retained_root.h/.c and tests/agx_retained_root_test.c (plus test runner if required). Parent owns wire ABI/MMIO adapter and Windows code. Current native9cf6c9d/root6b021e1.

Requirements: raise max Windows records16->256 and page inventory20->24. High allowed window unchanged. Add ONLY exact16KiB alias at J313_AGX_G2_REGIONB_BUFFER_MGR_GPU_VA (currently0x420000000) in broker-owned TTBR0; arbitrary low VA/private/system remain denied. Reuse shared UAT mapping implementation. Generalize check_tables/check_leaf ownership validation to owned TTBR0 slot0 as well as retained TTBR1 slot2, never private slots. Root1 identity/private prefix preserved. CLOSE validates/removes owned low/high mappings and detaches only owned descendants; no foreign free or root replacement.

Add `int hv_agx_retained_verify_absent(core,epoch,handle,va,ipa,length)`. Only exact tuple from successful prior UNMAP may be verified (one last-unmap receipt is enough for immediate verify); mismatched/stale/private tuples reject. It must verify real PTE absence with owned intermediate descriptor validation, not treat every query error as absence. Newly remapped VA must not verify absent. Malformed intermediate taints, no guessed traversals or foreign mutation. Parent will expose operation7 in a coherently versioned wire ABI. Do not edit ABI/platform files.

- [ ] RED/GREEN tests200 high leaves plus exact low alias, capacity boundary, arbitrary low denied, high/low malformed intermediate denial, alias backed by same owned data PA allowed, exact unmap/absence, stale/mismatched absence, remap-not-absent, rollback failures, second lifetime, unchanged root/prefix/firmware system.
- [ ] Run focused sanitizer suite, self-review and commit only owned files in correct nested/root repos. No hardware or subagents. Report API/header changes early; report exact commands/results/commits to parent.

### Task 2: Inventory-only graph and portable mapping journal (parent)

- [ ] Add PrepareBroker path to existing graph:89 backing allocations, zero UAT table allocation/root publication, no legacy crashlog; preserve VAs and90 range metadata including buffer alias.
- [ ] Add enumeration/ownership adapter and bounded journal using existing AGX_RR_IO client. Query each mapped leaf against expected backing; unmap reverse+verify absent. Test actual graph against actual broker core, Nth failures and restart; hold allocations on retirement failure.
- [ ] Remove copied-root/direct context0 callbacks from full render-admission, leaving only explicitly diagnostic shared legacy helpers. Generalize one-probe retained client to inventory. Correct retirement after ASC stop, without changing handoff lock owner. Add endpoint-stop qualifier before initdata, timestamps and full-inventory receipts.

### Task 3: Reviewed build and hardware ladder (parent)

Progress:EXP475 all200Windows leaves+application endpoints PASS;EXP477 full
native-profile initdata and real DC_Init/Idle receipts PASS. Both exact packages
cleaned. Next bounded BackendQualification runs existing production
BackendRuntimeStart including prepared-image/context/queue ownership, records
its existing exact result and readiness flags, then uses common teardown before
WorkItem/StartDevice completion. This avoids automatic OS graphics workloads
before queue-owner setup is hardware-qualified. No backend algorithm change.
After PASS proceed to normal Windows-driven submission boundary, not another
firmware probe. Native/Mu/profile remain exact477 artifacts.

- [ ] Review task1 plus full integration and resolve all load-bearing findings. Confirm WDK pin choice, build clean native and pinned KMD/UMD with Universal/sign/version/hash gates.
- [ ] First new candidate all200 Windows leaves+EL2 system, management proven prerequisite, application20/21 admitted, no initdata/workload. Save before/after Event129 timestamps and exact cleanup.
- [ ] After verdict continue first unknown only: offline native I/O/Hwdata/initdata gaps -> next exact candidate, then runtime/queues -> real completion. Do not stop merely for checkpoint report; no blind repeat.

Preflight review: Task1 core APIs consumed by Task2; no same-file writes. Task2 owns ABI/platform/Windows. Task3 consumes frozen artifacts and has no source edits during run. Firmware I/O gap explicit before initdata, not conflated with current90-range inventory.
