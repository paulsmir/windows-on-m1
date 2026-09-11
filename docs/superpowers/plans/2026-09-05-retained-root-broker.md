# Retained-root broker implementation plan

**Goal:** Windows broker mappings in original retained root through management ACK, then stop and recover.
**Spec:** docs/superpowers/specs/2026-09-05-retained-root-broker-design.md
**Architecture:** EL2 owns tables and system storage; Windows supplies only validated guest IPA and range requests. Existing shared UAT algorithms and firmware lifecycle are reused.
**Tech Stack:** freestanding C, m1n1, ARM64 WDK28000, existing Python host tests.

## Global Constraints

No copied root, raw Windows private-table access, context63/HVC change, IRQ change, application endpoints or GPU workload. Existing workspace and dirty user changes preserved. Source/build/hash/recovery/evidence gates mandatory. User already approved implementation and hardware.

### Task 1: Portable retained-root owner

Create m1n1_windows/src/hv_agx_retained_root.h/.c and tests/test_agx_retained_root.py with C fixture tests/agx_retained_root_test.c. Do not edit shared UAT algorithms, Makefile or platform adapter; parent handles integration. Reuse shared UAT APIs by including shared header relative to repo root and compile existing .c files in host test command.

Expose `struct hv_agx_retained_root` and `struct hv_agx_retained_ops`. Types use existing APPLE_AGX_UAT_* types and unsigned long long for addresses. Ops contains Context, AllocatePage(Context,APPLE_AGX_UAT_PAGE*) -> unsigned char, ReleasePage(Context,const PAGE*), TranslateGuest(Context,ipa)->u64 (returns0 unless whole16KiB is validated guest normal RAM), Sync(Context)->void. Core keeps immutable retained physical address and Entries pointer; prefix internal only.

APIs return enum/int status0=OK, nonzero explicit invalid/state/range/ownership/allocation/tainted failures:
`hv_agx_retained_prepare(core, retained_pa, retained_entries, region_bytes, epoch, ops)`;
`hv_agx_retained_activate(core)`;
`hv_agx_retained_map(core,epoch,va,ipa,length,handle_out)`;
`hv_agx_retained_query(core,epoch,handle,va,ipa,length,pa_out)`;
`hv_agx_retained_unmap(core,epoch,handle,va,ipa,length)`;
`hv_agx_retained_close(core,epoch,cpu_stopped)`.
Expose core.Roots (APPLE_AGX_UAT_ROOTS), Epoch, Active, Prepared, SystemVa, SystemBytes, PrefixUnchanged boolean/query function and MappingCount for adapter receipts. No exported private contents to guest ABI. Keep own table/page inventory including one borrowed retained root; release callback must skip borrowed root, never private descendants. At prepare allocate owned root0; root1=retained_pa. At activate require valid live private entries (existing prefix validation helper allowed), kernel root slots2..2047 initially zero; allocate own16KiB system data and map at0xffffffa080000000 with existing FirmwarePrivateReadWrite (native Normal). Save prefix and verify unchanged around mutations. Map Windows only16KiB in [0xffffffa000000000,0xffffffa020000000), at most16 owned ranges, FirmwareSharedReadWrite. TranslateGuest for every leaf, reject zero/misaligned/40bit-invalid or reserved-region PA; no PA request accepted. Exact token/epoch/range matching for query/unmap, no overlaps or unowned unmaps. New handle monotonic within epoch. Map rollback leaves no mapping or corruption on allocation failure, reuse AppleAgxUatMap/Unmap. Close only after cpu_stopped; remove exact owned mappings and system mapping, detach only owned kernel slot2 after checking it belongs to owned table inventory, Sync, free owned inventory/system; retained identity and prefix preserved. Never free root or follow private subtrees. Retry close idempotent; reuse after close requires fresh nonzero epoch, no stale handles.

- [ ] Write/run failing tests for immutable root/prefix, valid owned map/query/unmap, bad epoch/length/alignment/private/system ranges, translated-page rejection, exact handle mismatch, table-allocation rollback, close while CPU running denied, double-close and second lifetime with fresh epoch.
- [ ] Implement core using existing shared algorithms; run ASan/UBSan tests GREEN and self-review. Commit only task files (m1n1 and root separately if needed), report exact hashes to parent. Do not touch hardware, other files or dispatch subagents.

### Task 2: Platform and Windows integration (parent)

- [ ] Add fixed versioned MMIO request adapter at broker+0x600; validate width/offset/version/sequence; serialize; use existing hv_ipa_to_pa/RAM bounds, m1n1 allocator and native barriers/TLBI. Protect context0 GPU-region writes and disable old private-prefix export. Publish only adopted root; preserve context63.
- [ ] Add management-only qualification build option reusing firmware provider callbacks and physical allocation GuestIpaBase. PREPARE before CPU, ACTIVATE + own probe map/query under existing handoff before management, broker system crashlog. Record immutable identity/ownership and real RTKit results; stop before app endpoints, CLOSE after ASC stop, then poweroff.
- [ ] Host-test MMIO validation and consumer sequence; review complete integration before build.

### Task 3: Exact hardware discriminator (parent)

- [ ] Record source commits/diff hashes, pinned current m1n1 build and FRYZZING WDK/UMD/signing/version gates; preserve normal recovery.
- [ ] Preregister one exact natural bind, require same-root/own-mapping receipts and real IOP/AP ACK. No copied-root or workload test. Save result, exact cleanup, ordinary G2 and health. If no ACK preserve evidence and identify next causal difference without repeating candidate.

Preflight review: Task1 produces core APIs consumed by Task2; Task2 alone owns Makefile/MMIO/Windows files so no write overlap. Task3 consumes frozen binaries, never changing code during run. Tests and scope agree: one bounded16KiB range is deliberate minimum, not full allocation submission architecture.
