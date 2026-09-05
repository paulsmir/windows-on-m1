# Firmware-private prefix implementation plan

**Goal:** close only Windows context0 private-prefix preservation through RTKit management ACK, then clean and stop.

**Architecture/spec:** User-approved read-only live prefix transport. Add a versioned 64-byte view at broker+0x280 (existing assigned resource; config ends0x248, scanout starts0x400). Source is ADT gfx-shared-region, never measured entry constants. Firmware owns every borrowed private subtree. Windows owns its root and kernel mappings. No Mu/ACPI allocation or private-memory mapping needed. Host accepts only aligned bounded reads, no writes; validates live source geometry and descriptors before reporting Ready. Power receipt epoch and double-read equality guard collection; read afresh after each successful CPU/handoff startup, never retain across graph destruction.

## Sources and observed contract

- EXP470 result: root zero before CPU; two live firmware descriptors before ioalloc; native ACK0x20/0x20 with retained root.
- Current m1n1 hw/uat.py UAT.init preserves first16 bytes; fw/agx/handoff.py owns synchronization.
- Asahi mmu.rs Vm::new, pgtable.rs new_with_ttb: borrowed root/restricted kernel range; no external source copied.
- Current hv_agx_config_snapshot.c, hv_agx_power_mmio.c and broker: existing read-only MMIO pattern and power lifetime.
- Current shared initdata_memory.c: currently creates owned roots then mappings before firmware; split only mapping phase.
- Current uat_table.c: destroy frees recorded inventory, child walk refuses missing inventory pages. Borrowed descriptors must never be inventoried or traversed.
- Microsoft READ_REGISTER_ULONG64 contract: mapped register read with memory barrier, https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-read_register_ulong64 . Existing DXGK-assigned broker mapping reused.
- Native EXP208 source reference and EXP470 current control agree on retained private-prefix ownership; no old live artifact.

## Tasks (inline execution; existing user workspace, no worktree/delegation)

- [ ] Add portable wire validation + owner read-only adapter with tests. Reject bad version/size/prefix length/alignment/range, unknown flags and unavailable epoch; reject writes before invoking source reader. Every read obtains current entries, not a boot-time cache. Validate two identical full windows on Windows.
- [ ] Extract current mapping block unchanged into common graph-map function. Existing Build remains prepare+map for existing users; production Prepare defers maps. Finish imports validated exact entries into empty context0 TTBR1 prefix before calling same mapping function. Reject repeat/foreign root, assert prefix byte-exact after mappings; destruction only frees owned inventory and clears readiness. Exercise map/cleanup/restart with nonconstant test prefixes.
- [ ] Wire production provider publish after CPU/handoff, read/validate epoch/window, import/map, resolve crashlog, then existing UAT publish. Persist transport/import entries and result receipts. Candidate-only management qualification returns a deliberate failure after real successful management, forcing existing reverse cleanup before application endpoints; normal build retains full behavior.
- [ ] Run focused sanitizer suites, m1n1 host tests and source review; commit only owned changes. Append CHANGES.csv with exact source commit(s), implemented status.
- [ ] Freeze source; build m1n1 with existing IOMFB_FULL_OWNER=1 toolchain and KMD/UMD with pinned FRYZZING. Universal/analysis/catalog/signing/version/hash gates. Preserve EXP377/392 recovery.
- [ ] Preregister one EXP471 integrated prefix candidate, natural bind, exact receipts and host output. PASS requires live/imported entries identical, mappings valid, grant and IOP/AP0x20. No further GPU phases. Save verdict, remove exact package/service, restore ordinary G2, health, state/ledger and short final handoff.

**Self-review:** transport does not own or mutate firmware pages; mapping ordering follows explicit user requirement; context63 and HVC unchanged; full-window readiness is not hardware proof. A failed primitive gets at most two focused causal fixes, no adjacent subsystem work.
