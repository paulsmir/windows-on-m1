# Retained-root broker management qualification

User-approved architectural stage, following EXP472/473. No copied-root runs.

## Lifetime and ownership

1. PREPARE after power ON adopts the ADT original root address, allocates an EL2-owned empty TTBR0; does not modify/publish retained root or private entries before firmware boot.
2. Firmware CPU/handoff boot populates private entries. ACTIVATE verifies live private prefix and untouched kernel slots, freezes identity/prefix internally, creates an EL2-owned system crashlog page and mapping in the SAME retained root. EL2 publishes context0 pair and performs barriers/TLB maintenance.
3. Windows MAP/QUERY/UNMAP operates only on caller-owned guest-RAM-backed 16KiB ranges in an explicit kernel-VA window. Each successful map receives an epoch-scoped handle. No caller-supplied host PA, table pointer, raw PTE, or private prefix.
4. Existing RTKit management uses broker-owned crashlog VA/capacity, then real IOP/AP ACK is required. Qualification stops before application endpoints.
5. Stop ASC through existing production stop path. CLOSE requires CPU stopped, removes only broker-created Windows/system mappings, syncs, releases only broker-owned table/data pages. The adopted root address and private entries are never replaced, cleared or freed. Power OFF follows. Reset requires new lease and fresh adoption; failed cleanup retains ownership and fails closed.

FIRMWARE OWNED: retained identity/private tables and system/crashlog semantics.
WINDOWS OWNED: DXGK physical allocation backing and its exact registered ranges.
BROKER OWNED: page-table mutation, validation, publication, maintenance and rollback; system backing is allocated by EL2 for firmware, never supplied/freed by Windows.

## Narrow API

Version1, fixed register structure in existing broker window, request sequence and epoch. PREPARE, ACTIVATE, MAP, exact UNMAP, QUERY, CLOSE. One MAP range is exactly16KiB; maximum16 Windows mappings for this management discriminator. Larger transactions reject rather than partially apply. Allowed Windows VA [0xffffffa000000000,0xffffffa020000000). System crashlog VA0xffffffa080000000 is denied to Windows. Private VA is outside allowed window. Guest IPA translates through existing stage2 and must lie wholly in guest normal RAM; firmware/reserved/EL2 pages are never accepted. Exact PA identity is verified by the existing physical-owner/HVC evidence, not used as unvalidated request input.

No raw private prefix export; previous prefix diagnostic window is unavailable on this platform. Context0 GPU-region TTBR writes are broker-only; context63 semantics remain unchanged. Root address and unchanged-prefix receipt may be queried, private contents only appear in host evidence.

## Implementation reuse and checkpoint

Reuse shared apple_agx_uat.c / apple_agx_uat_table.c with adopted-root inventory and owned-only release. Do not duplicate table algorithms. Reuse existing render-admission physical objects, RTKit session, firmware provider, handoff, power and exact recovery paths. Management qualification adds one Windows-owned probe mapping through broker; no full initdata/application queue construction is needed to prove this boundary.

Sources: EXP470/472/473 live evidence; current m1n1 UAT.init/flush_dirty and handoff; Asahi mmu.rs/pgtable.rs borrowed root and restricted ranges; current hv_guest_ipa_pa.c and RAM guard; current DXGK physical_memory_windows.c ADL GuestIpaBase ownership. No foreign code copied.

Self-review: identity is immutable per lease; private entries never guest-accessible; map rollback uses existing deterministic UAT code; close cannot free a firmware page; hardware proof limited to actual management ACK.
