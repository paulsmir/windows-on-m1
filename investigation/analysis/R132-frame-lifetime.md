# R132 — bounded system-frame lifetime (offline)

Authorization: NEXT_TASK_R132.md, steps 3 and broker tests of step 4 from
EXP850-sysmem-translation.md. No Air, package, firmware change or cap change.
Base: 40bbf027; existing dirty m1n1/Mu trees are preserved.

## Contract and implementation sequence

Inspected current paging, graph, translation and BeginJob sources; Asahi
mmu.rs map_node and the source/spec references in EXP850-sysmem-translation.md;
m1n1 hv_agx_gpuva_v5.c, retained_platform translate_guest, retained_backing.c;
Mu MemoryInitPeiLib reservation and the generated ACPI contract in that design;
pinned WDK DXGK_PTE/UpdatePageTable and official Microsoft structure docs:
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ns-d3dukmdt-_dxgk_pte
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_buildpagingbuffer_updatepagetable

VidMm owns pinning/residency and supplies ordered PTE updates. KMD owns logical
mapping references, per-frame generations, grants and queued-job validation.
m1n1 owns full-span IPA-to-PA validation, protected RAM exclusions, table/grant
collision checks, UAT stores and TLB completion. Mu owns the immutable R64
reservation. Existing scheduler/firmware own execution, interrupts and completion;
no new DMA, power or recovery initialization is introduced. Windows 4-KiB PTEs
need four compatible contiguous subpages for one Asahi 16-KiB leaf. System
backing receives no new CPU mapping. Local R64 generation policy stays intact.
Reference remains EXP850R on R110 and ordinary EXP377/392 recovery; no live
machine state is needed or claimed for this offline task.

1. RED→GREEN production paging→graph→wire→broker replay; typed system provider,
   global frame registry, merged logical PTE provenance, ordered revoke and
   partial/Repeat/64-KiB cases. Zero allocation handle remains valid.
2. RED→GREEN queued generation check through production BeginJob; idle mutation
   and destruction refuse jobs in flight. Verify aliases and reverse teardown.
3. Broker/real translator negative tests and fault/capacity gates, without
   changing ownership enforcement. Full host suite once, review, implementation
   commit(s), then CHANGES.csv bookkeeping with implemented/EXP850.

Registry operations are serialized by the existing adapter fast mutex. Mapping
references survive incomplete groups; grant references survive uncertain UAT
state. Unsupported flags/scattered subpages remain CPU-only or rejected and
never gain an unsafe GPU leaf. Any uncertain publication blocks jobs and paging
success and retains affected grant ownership for the existing fatal recovery.

Smallest future hardware checkpoint: a joined v2 failure receipt, then separately
one resident representable system frame with GPU read/write and completion,
invalidation and stale-access rejection. No hardware permission here. Recovery
must preserve the hash-bound R110 and ordinary artifacts and follow EXP850R
ordered restart/dump-first exact cleanup/ordinary Code28 verification.

## Progress

Baseline: 6 targeted tests PASS. Existing VidMm replay was stale after R130;
updated its receipt sink only. RED: complete system group publishes no leaf
(assert broker.commands > calls), real paging/graph/client/broker chain.

## Tandem dispositions

- REVIEW R113: ACCEPT — preserve direct cold full-owner transition; no launch here.
- REVIEW R111: ACCEPT — preserve the measured R64 CPU memory-type fix; no new uncached OS-RAM alias.
- REVIEW R110: ACCEPT — preserve the completed 64-bit dynamic ACPI range fix.
- REVIEW R109: REJECT — header already records the firmware hypothesis rejected; no sysmem implication.
- REVIEW R108: DEFER — historical firmware issues are outside the proven Submit boundary.
- REVIEW R107: DEFER — historical 0x101 attribution is outside this offline mapping design.
- REVIEW R106: ACCEPT — keep ordered transition and package gates for any later run.
- REVIEW R105: ACCEPT — use the recorded EXP836 matrix result; do not rerun it here.
- REVIEW R104: REJECT — already rejected by EXP821, not evidence for this mapping failure.
- REVIEW R103: DEFER — AllocateCb boundary is crossed; no live harness in this task.
- REVIEW R102: DEFER — AllocateCb failure is not the current boundary.
- REVIEW R100: REJECT — preserve its recorded rejection; no re-investigation.
- REVIEW R99: DEFER — old CreateDevice failure is superseded by the current boundary.
- REVIEW R98: ACCEPT — retain full-size output cases in future scatter-output tests.
- REVIEW R97: ACCEPT — one current-source pass, no EXP208 archaeology.
- REVIEW R96: ACCEPT — preserve the native TA/3D builder and change only its backing contract.
- REVIEW R95: ACCEPT — include ownership, generation, paging and failure-attribution gates.
- REVIEW R94: ACCEPT — keep per-process GPU objects distinct from firmware objects.
- REVIEW R91: DEFER — prior screen observation is unchanged; no presentation claim here.
- REVIEW R90: DEFER — panel/color observations are outside this address-translation boundary.
- REVIEW R88: ACCEPT — submission, completion and actual presentation remain separate checkpoints.
- REVIEW R86: ACCEPT — bounded direct grants first; versioned range grants before capacity claims.
- REVIEW R85: ACCEPT — future package must bind source, ABI, profile and artifact hashes.
- REVIEW R74: ACCEPT — read the context contract; retain aperture DMA placement.
- REVIEW R71: REJECT — the assertion that a cap guarantees all mappings contiguous is not established; retain the supported 64-KiB path as a separately tested candidate.
- REVIEW R69: ACCEPT — no live bind or Air access here.
- REVIEW R65: ACCEPT — preserve AGX4 v2, root/rights/lease/fence ordering; compute remains separate.
- REVIEW R64: ACCEPT — both OPEN entries: preserve the implemented R64 carveout and current G4 ABI; neither grants new hardware permission in this task.
- REVIEW R63: ACCEPT — keep firmware-owned reserve; no new contiguous allocation at StartDevice.
- REVIEW R57: ACCEPT — preserve full-span bounds and meaningful paging failure status in new tests.
- REVIEW R55: DEFER — repeat StartDevice is a separate recovery defect, not a mapping experiment.
- REVIEW R54: DEFER — no series is started; later hardware requires its own authorization and ledger.
- REVIEW R49: ACCEPT — preserve pinned WDK caps/table ABI; no opportunistic cap change.
- REVIEW R48: ACCEPT — no ports opened; future control-plane/launcher exclusion remains required.
- REVIEW R47: ACCEPT — preserve build-profile/ABI checks; no firmware build here.
- REVIEW R45: ACCEPT — test real broker dispatch and leaf/grant/table/root teardown order.
- REVIEW R40: DEFER — timer/watchdog attribution is outside the synchronous Parse rejection.
- REVIEW R37: DEFER — historical disarmed-start failure does not justify changing sysmem or launching Air.
- REVIEW R54: DEFER — No hardware series in this task.
- REVIEW R49: DEFER — Admission caps remain unchanged; Code0 already proven.
- REVIEW R48: DEFER — No ports opened.
- REVIEW R47: DEFER — No firmware or package build.
- REVIEW R45: ACCEPT — Reverse teardown is exercised with the real broker.
- REVIEW R40: DEFER — EL2 timing is outside this synchronous paging boundary.
- REVIEW R37: DEFER — No live install or bind.

## Implemented result and verification

The bounded system provider is now part of the production G3 pager and graph.
The adapter registry records frame generations, logical subpage mapping counts
and per-owner grant references. Per-leaf rights and optional allocation/offset
provenance survive merged partial updates. Missing or scattered/mixed-rights
groups remain unpublished. Shared system grants join the same live physical
lifetime across processes, while local R64 retains its existing grant policy.
System payload receives no CPU mapping and no firmware/ACPI/capability changes.

A leaf transaction acquires all incoming mapping references before publication;
failed batches restore acknowledged leaves while old mappings remain resident.
Successful invalidation/replacement drops old references only after broker
synchronization. Ambiguous stores/TLB failures retain old and pending ownership,
poison the graph and cannot complete paging or begin jobs. Parent updates apply
all replacement links before retirement; retired descendant links do not count
as live aliases. Parent rollback preserves retirement metadata. Table level
reuse retires removed descendants and resets the shadow. Replacement metadata
is allocated before destructive retirement; a broker failure after retirement
fails closed instead of permitting a retry with stale shadow provenance.

Submit captures the graph mapping generation under the adapter lock. BeginJob
revalidates it and the actual CPU/GPU ranges, so invalidate/remap and root/parent
ABA cannot execute an old queued snapshot. In-flight jobs prohibit mutation and
process destruction. R131 CPU-envelope checks and deliberate 64-KiB CPU-shadow
invalidation remain intact. No physical-path/output capability was added.

Permanent real-C coverage includes all 24 four-PTE arrival orders, partial
invalidation, Repeat, 4K/64K replacement, RO/mixed rights, scattered PFNs,
null allocation, same-owner aliases, two process roots plus paging alias,
PFN lifetime reuse, invalid PTE attributes and bounds, generation exhaustion,
reverse teardown and in-flight refusal. The production BeginJob is replayed
with the real parser/graph/client/broker; output-locality is an explicit test
boundary because output views are step 5. Queue admission itself is exercised
by the separate unmodified SubmitCommandVirtual body replay.

The production retained-platform translator is extracted into the host replay;
only stage-2 translation, CPU memory and synchronization I/O are modeled.
Tests assert the actual nonidentity PA in the broker descriptor, bad subpages,
MMIO/protected RAM/root/ramdisk/table conflicts, wrong epoch/process/backing
generations, shared/exclusive collisions, revoke BUSY and actual grant-capacity
exhaustion after an earlier leaf published. Single/double sync failures and
TLB rollback failures assert descriptor restoration, retained references and
rejected BeginJob. No broker ownership check was weakened or firmware edited.

Review found and reproduced additional RED cases before fixes: child-link move
within one parent update; diamond table aliases; unrooted middle-table reuse;
rollback of a retired parent edge; allocation failure after table retirement
followed by a retry at the new level. Each now passes. The final independent
review reran the sanitizer replay and found no remaining Critical/Important
issue in the final correction.

Final affected command (33 tests PASS):

```text
CC=/tmp/agx-clang-wrapper python3 -m unittest tests.test_gpuva_g3_contract tests.test_g4_submit_virtual_replay tests.test_g3_vidmm_replay tests.test_gpuva_broker_v5_contract tests.test_agx_retained_backing tests.test_gpuva_g3_paging_bootstrap tests.test_change_ledger
```

Full host command: `CC=/tmp/agx-clang-wrapper PATH=/tmp/agx-cc:/opt/homebrew/bin:$PATH python3 -m unittest discover -s tests`.
It ran 1121 tests: 15 failures, 41 errors, 2 skipped. The suite is **not green**;
missing serial/construct/proxyenv/Mesa fixtures and existing source-contract
expectations are among the failures. Every failing test name is preserved in
main-repository `.local/experiments/R132-offline/full-suite-failures.md`, with
the complete log beside it. That full run preceded the final allocation-failure
fix; all affected suites were rerun after the fix. An earlier full run also
caught the newly added level-reuse RED during review and is retained as such,
not reinterpreted as successful verification.

Final source file manifest SHA256:
`d5ad8ee8f13fd06d06466d87dc1afc4571a852725d481f983f295e8062f7396f`.
The main-repository `.local/experiments/R132-offline/verification.json` indexes
source and RED/GREEN/final-test logs by SHA256. These are host-test artifacts,
not a driver package or a hardware result. No Air access, package build,
installation or launch was performed in this task. Concurrent EXP851 ledger
and state updates from another task were left intact.

## Remaining step 5 and hardware checkpoint

Step 5 must replace the reserve-only output proof with a generation-bound GPU
output view that admits VA-contiguous outputs backed by noncontiguous 16-KiB
frames. Optional CPU consumers need an explicitly pinned compatible scatter
view or must decline the operation; never fabricate contiguous DestinationPhysical
or a local CPU token. Keep the final scanout destination separately owned and
prove backend bind, BeginJob, completion and copy/fence ordering on real code.

The current broker remains bounded to its existing 8192 grant slots. Range
grants are separate scalability work, and arbitrary scattered 4-KiB GPU data
is still not representable. Steps 3–4 do not claim system-backed render targets
or accelerated DWM.

A separately authorized hardware checkpoint must identify a resident,
representable segment-0 frame and its generation, show the broker-translated
GPU read/write and matching completion, then invalidate it and demonstrate
stale-access rejection. Retain Code0/SSH health and perform evidence-first exact
cleanup to ordinary Code28. Parse success alone is insufficient. The concurrently
recorded EXP851 v2 receipt confirms a GPU-consumed segment-0 rejection after
CPU-envelope admission; it does not validate this new mapper on hardware.

Verification manifest SHA256: `ae176215dd0ed0512251eefd012e14208492fb5a35bd05a826d442ed0da72acd`.
