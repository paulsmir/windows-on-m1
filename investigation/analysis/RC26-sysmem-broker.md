# RC26 sysmem broker implementation plan and contract

Spec: `/Volumes/pwdev/public_windows/.local/handoff/codex/rc26/TASK.md`.
Offline only, driver base 846cca147389db7d5f98432c5e02670a0d5ecf6c,
m1n1 base 0d3a75b3beb78fbfdfda3b1bf8ca8dfbcfaa49a7.

## Source finding and scope

The supplied premise that v5 only permits pool-window pages is contradicted by
this exact m1n1: src/hv_agx_retained_platform.c:50-68 translates any eligible
16-KiB guest page; src/hv_agx_gpuva_v5.c:447-530 registers it by IPA and PA.
There is no pool-window bounds test there. src/hv_agx_power_mmio.c:47 explicitly
budgets broker records for both window and other system pages. RC23/24 lacked
this matching source; their unknown must not become a fictitious restriction.

Use the task's explicitly permitted broker + ABI + KMD probe deliverable.
A complete pageable shadow bridge remains a separate content/residency owner:
RC24-shadow-contract.md identifies ordered copy-in/out, aliases/generations,
fallible admission and eviction/cancellation. A successful broker grant alone
cannot safely enable Wom1SysmemShadow or lower allocation-life pool promises.

## Architecture and ABI

Keep wire version 6 and 128/64-byte request/response layouts. Append commands
17 QUERY_CAPS and 18 REGISTER_SYSMEM_BACKING. Old brokers reject both; old
clients retain all existing behavior. Query uses zero payload and returns
Reserved[0] bit 0 (strict exclusive sysmem registration), Reserved[1] extension
version 1. Flags remain the taint channel, never capability bits.

New registration uses ProcessId/ProcessGeneration, AllocationGeneration and
AuxIpa; every other payload field is zero. Exactly one complete aligned 16-KiB
page per call. Runs are registered page by page with caller-owned rollback.
This version is exclusive; shared shadow generation ownership is not invented.
Existing UPDATE_LEAF, REVOKE_BACKING and DESTROY perform publication/revocation.

A distinct platform translator requires normal read/write hardware stage-2
mappings at both ends of every 4-KiB quarter, contiguous aligned PA < 2^40,
IPA and PA within published guest RAM, and existing live-reserve exclusions.
The ordinary guest-RAM IPA requirement intentionally excludes the synthetic
pool window and low boot aliases. Nonidentity translations within guest RAM
are valid. Exclude local reserve/private/backend and retained firmware root
by PA as well as ordinary launch reservations. Legacy registration stays intact.
New records retain their strict type and revalidate it when publishing a leaf.
Tables/mailbox/duplicate PA/process/generation checks remain in the common broker.

## Owners and security limits

VidMm owns original pageable storage/residency; KMD must hold the MDL and all
four PFNs through GPU retirement, acknowledged leaf clear/TLBI and revoke.
m1n1 owns stage-2, backing grants, UAT descriptors, slots and TLB completion.
It cannot observe MmFreePagesFromMdl; freeing before revoke is a KMD violation.
The trusted guest kernel already controls its RAM and writable UAT-table pages;
this does not claim isolation from a malicious guest kernel rewriting tables.
It does preserve broker-mediated guest-process ownership and reserved-PA checks.
Mu owns boot RAM/reserves/ACPI; its gitlink is uninitialized here, so generated
ACPI/live layout cannot be reverified. No Mu, power, IRQ or launch changes.

Cache: retain GPU shared attributes (UAT attribute 2, RO/RW), not firmware cached
attribute 0. New CPU backing must have one consistent cached or WC MDL mapping;
no cached alias of existing WC pool slabs. Barriers order publication, not repair
incompatible CPU aliases. Asahi gem.rs:160-195 selects cached/WC separately;
mmu.rs:1200-1229 uses DMA SG addresses with native alignment. Its 16-KiB Linux
pages do not make Windows PFNs contiguous. UAT here encodes translated PA directly;
there is no new DART mapping, and this grant cannot be used for DCP/other devices.

## Implementation steps (native execution)

1. Add failing real MMIO/core tests for capability negotiation and strict sysmem
   registration; translation policy tests cover denied reserves, nonidentity RAM,
   partial/noncontiguous/unaligned/overflow, missing permissions and aliases.
2. Implement the broker extension and strict translator; reuse real leaf/slot/
   revoke code. Test running-job BUSY, live-leaf revoke BUSY, failed TLBI rollback,
   successful unmap/revoke and stale generation. Keep default ABI behavior.
3. Add ABI constants and client capability query. Read Wom1SysmemGpuMap only as
   DWORD 1 at PASSIVE startup. When requested, require the capability before
   mailbox attachment; otherwise STATUS_NOT_SUPPORTED. This is a probe, not a
   shadow enablement. Test old/new/tainted/stale replies and default-off behavior.
4. Run targeted driver/m1n1 host regressions, full driver baseline to BASELINE_OK,
   m1n1 native cross-build with pinned flags, independent review, small commits
   on both branches, and final handoff REPORT.md. Do not alter experiment/change
   ledgers, protected invariants or known_failures.txt.

Review focus: old broker rejection; forged/stale replies; PA aliases into reserve;
job/TLBI failure retaining ownership; scope claims that confuse grants with paging.

## Smallest future checkpoint (integration owner only)

Build a new J313_GPUVA_FULL_OWNER macho with AGX_LOCAL_RESERVE_V2=1 and
IOMFB_FULL_OWNER=1; pass AGX_M1N1=<new macho> to launch-exp/prepare and hash it.
First test opt-in capability success, old-macho clean refusal and default-off
compatibility. A subsequent bounded test-owned MDL probe must prove one 16-KiB
raw page outside the pool: register, publish, GPU read/write, verify contents,
retire, clear, TLBI, revoke, then free; include rejected partial/protected pages.
Do not use these commits alone for a GravityMark shadow/performance run.
Recovery is integration's preserved known-good GPU-visible image/package cleanup;
no hardware/control-plane inspection or execution occurs in this task.

## Delivered source evidence (final line coordinates)

D = drivers/apple-agx; H = the authorized m1n1-sysmem tree.

| File:line | Contract / change |
| --- | --- |
| D/shared/src/apple_agx_gpuva_g3_graph.c:534 | Existing graph registers backing through v5; it is not converted to strict sysmem by this patch. |
| D/shared/src/apple_agx_gpuva_broker_v5_client.c:65, :108, :123 | Client stamps ABI/sequence/epoch, rings the synthetic MMIO doorbell, verifies receipt/epoch. No new HVC number. |
| H/src/hv_agx_gpuva_v5_mmio.c:15, :27 | Snapshot either MMIO-window or attached-mailbox request; exact version 6, size, monotonic sequence and bounded command dispatch. |
| H/src/hv_agx_retained_platform.c:50 | Existing generic translation already accepts eligible RAM outside pool. |
| H/src/hv_vm.c:589, :602 | IPA-to-PA stage-2 walk; separate normal-RW attribute predicate rejects software subpage descriptors. |
| H/src/hv_agx_retained_backing.c:17, :44 | Existing launch-region exclusion plus new strict IPA/PA range, live root/local, permission and full-leaf tests. |
| H/src/hv_agx_retained_platform.c:101, :199, :389 | Wire actual stage-2 callbacks, route new commands, enable optional capability only after valid local-reserve setup. |
| H/src/hv_agx_power_mmio.c:590, :624 | Window changes and v5 calls hold the same broker_lock. |
| H/src/hv_agx_gpuva_v5.c:459 | Preserve process/generation, PA/IPA uniqueness, table/mailbox exclusion and capacity checks; mark strict records. |
| H/src/hv_agx_gpuva_v5.c:625, :697 | Full four-quarter mapping, uniform permissions, ownership/generation and strict revalidation before descriptor encoding. |
| H/src/hv_agx_gpuva_v5.c:302 | Publish is refused with in-flight jobs; stores/sync/TLBI precede acknowledgement; failed publication rolls back, failed rollback taints. |
| H/src/hv_agx_retained_platform.c:141 | DSB oshst / ASIDE1OS / DSB osh / ISB for leased nonzero ASID. |
| H/src/hv_agx_gpuva_v5.c:543, :792, :843 | Revoke refuses live leaf references; release requires zero jobs and TLBI; destroy refuses slots/grants/nonzero tables. |
| H/src/hv_agx_pool_window.c:119 | Pool window unmap refuses while broker holds PA. Raw strict grants do not create window aliases. |
| D/shared/src/apple_agx_uat.c:83 | GPU shared RO/RW protection uses attribute 2, host PA in leaf descriptor. |
| D/shared/include/apple_agx_gpuva_broker_v5.h:19 | Extension 1, command numbers 17/18, unchanged wire layout and version. |
| D/shared/src/apple_agx_gpuva_broker_v5_client.c:158 | Capability requires successful, untainted, matching-extension reply; old broker rejection means unsupported. |
| D/render-admission/src/gpuva_g3_windows.c:328, :380 | Strict registry reader directly in PASSIVE G3 startup, before mailbox/state publication. |
| D/render-admission/src/lifecycle.c:477, :492 | G3 starts before pool: review caught and fixed a first-start policy-read placement error. |
| D/render-admission/src/allocation_windows.c:453 | RC23 shadow refusal stays intact. No new MDL allocation/publication caller exists. |

The early plan's pool-start wording is superseded by the final G3-start read:
putting the reader in pool startup silently skipped the first capability query.
The new regression first failed, then passed after relocation. No lifecycle
ordering was changed. `tests/test_g4_pool_residency_limit.py` only gained an
optional source argument to reuse its executable registry fixture.

### Windows allocation/lifetime boundary for follow-on work

Pinned `.local/reference/winsdk26100/km/wdm.h:29772` declares
MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS; :29832 declares MmAllocatePagesForMdlEx
with SkipBytes and CacheType (not a generic alignment/boundary parameter).
Existing pool_windows.c:278 requests WC 64-KiB chunks and verifies PFNs.
A future private owner can request native-sized chunks, but must inspect actual
MDL length, page alignment and every PFN; no request flag replaces this proof.
Do not allocate memory in a paging update and invent a retry status.
`.local/reference/wdk26100/d3dkmddi.h:3856` and :10117 expose backing-store
sharing and contiguous ADL facilities; they do not supply the missing shadow
residency, copy ordering or canonical generation implementation.

The inspected Asahi device tree is t8103.dtsi:477 (GPU/UAT reserved regions,
no new DART domain for this mapping) with t8103-j313.dts:12/:17 selecting J313.
Its SG DMA addresses must not be confused with Windows IPAs. Existing m1n1
proxyclient/m1n1/hw/uat.py:20 distinguishes firmware cached and shared attributes.
No external implementation was copied. The uninitialized Mu gitlink and absent
live measurements limit verification of the actual boot map; the extension
fails closed when its captured launch contract/reserve is unavailable.

## Verification result and limits

Driver targeted: 24 tests pass. Full baseline: 536 tests / 230 modules,
6 known failures, 0 NEW, BASELINE_OK. No baseline exceptions changed.
m1n1: four real C host fixtures pass with ASan/UBSan and warnings as errors.
All four changed C translation units compile using LLVM 22.1.8, flags
AGX_LOCAL_RESERVE_V2=1 IOMFB_FULL_OWNER=1. Generated build_cfg.h SHA-256
matches the supplied full-owner build info:
a01c4ca96c32fa5aa024e3790a50aefd5627f26da388f1a6d25f6e5fb33047df.

Full macho link was attempted offline, but rust/vendor/rust-fatfs is an empty
submodule (pin 4eccb50d011146fbed20e133d33b22f3c27292e7). No macho is delivered.
The broad existing m1n1 host script also has current Apple-clang -Werror failures:
asc_tx_gate_test's uninitialized objects passed by address, then
hv_autonomous_manifest.c's exact-length magic string. These unchanged files
were not patched; targeted broker tests run without warning suppression.
No WDK build, hardware, guest, installation, launch or other worktree access.
Independent review found the startup-order error above; re-review closed it
and reported no remaining blockers within the broker-plus-probe scope.
