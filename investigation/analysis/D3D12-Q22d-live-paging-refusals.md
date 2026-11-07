# Q22d — identify the live-paging refusal, offline only

Continuation base: 2d482fe27af75c90b7fb5697e4a186fc754a648d.
No hardware, guest, builder, package, launch or recovery access. m1n1-q22
is an unchanged read-only reference at 7cce5a0e8427d30b5ad9337a2098f27f1ab86ea4.

## Evidence and ranking

EXP2116's saved receipts prove live capability acceptance and per-process
negotiation (q22c). Wom1G3PagingWait has only two waits, zero timeouts; its
maximum is UPDATE_PAGE_TABLE for process21 while process21/fence2242 is
active and last complete is2241. QPC1461130783 +457864 ticks at24MHz is
19.0777ms; the end is34.875us after head completion1461587810. Q3PC's
next submit1461596481 misses overlap. Outputs and IA1048576/524288 pass.

The UMD trace adds a useful narrower fact: line535 has
measure-gpuva-paging-wait {19291us, site1, fence7092}, immediately followed
by measure-bo-create {0MiB, class1, create84us, bind20040us, associate2us}.
This is a new sub-MiB General BO's bind, not a cache hit. Source:
umd_gpuva_windows.c:926 wait_paging uses site1 when PagingWaitToken is set;
map_va sets it after MapGpuVirtualAddressCb; agx_win32_asahi_bo.c:508,542
binds and reports the three creation intervals; agx_d3d12_windows.cpp:560
names their fields; apple_agx_win32_device_info.h defines General=1.
The trace does not identify this BO's VA, PTE array, table level or placement.
A returned paging fence may include preceding work on the same paging queue.
It does not prove which individual update generated the wait.

Ranked hypotheses (inferences, not a hardware verdict):

1. An occupied logical/resident/native entry, invalidating update, or reused
   table/VA in that bind's paging dependency. Existing entries cannot be
   replaced live; a new BO is not proof of a never-used VA or table. Two warm
   passes do not eliminate fresh internal BO allocation (the timed create
   proves one happened). No trace proves VA recycling specifically.
2. A partial native group or system/aperture/native-staging mapping. The
   preflight permits only full aligned native groups in local/pool segments;
   a sub-MiB General BO does not identify its segment or PTE shape. Neither
   case is excluded by the trace. Mixed segments or permissions within a
   native group also reject; uniform but separate complete groups are legal.
3. A table-level reuse, active-root change, or pending shadow. Fresh empty
   non-root initialization and fresh parent/leaf chain growth already pass;
   generic table growth is not itself an explanation. Root replacement and
   reused nonempty links are destructive and remain joined to retirement.
4. Unsupported flags, VA/physical alignment/range, arithmetic or backing
   failure. Reachable guards exist, but the successful Q3PC result and no
   additional broker failure provide less support for malformed input.

Excluded as explanations of this recorded maximum: fill/transfer (operation
is UPDATE_PAGE_TABLE), FLUSH_TLB, timeout (count0), process poison/uncertain
at entry (those break the loop before waiting), and broker refusal causing a
retry (Build waits before broker mutation, and never loops on its result).
The only broker failure in the saved ring is CREATE/STALE epoch discovery.
Per-process mode disabled is possible in the abstract, but25 successful
negotiations and node1 discovery make it a weak explanation here.

No specific avoidable rejection of the observed input is proven. q22d adds
no speculative behavior switch or relaxed guard. An identical remap would
still touch a nonzero native descriptor and the broker refuses it live.
Partial/system/native-staging cases require an ownership/group proof; a
shorter wait or safe fresh-map fixture cannot supply that proof for EXP2116.

## Complete guard inventory and owners

Source order in gpuva_g3_paging_windows.c:

- Build prevalidation (before any wait): PASSIVE_LEVEL and state, supported
  operation, level/index/count bounds, input pointers, no dual PTE array,
  reserved/native-fence flags, supported64K contract, update mode, table
  resolution. Rejected inputs use existing internal-status/failure receipts.
- Preflight: process exists; negotiated LiveAdditive; no poison, uncertain
  graph or CpuOnlyMappings; no NotifyEviction; input shape; shadow has no
  pending candidate and native table level matches; runtime local view;
  native-staging binding exclusion; mapping-generation headroom; original
  table span; leaf virtual address alignment and39-bit range.
- Fresh non-root InitialUpdate containing only invalid PTEs is an allowed
  no-op when no broker shadow exists.
- Parent: root must be the active root; valid ordinary local table PTE
  (only allowed page-size bit ignored; root cannot use that page-size mode),
  lossless address conversion, resolved owned table, no self-reference,
  no pending/wrong-level child, no existing parent edge at the slot.
- Leaf: full four-logical-entry group; index/count and allocation-offset
  bounds; neither logical nor resident old flags; valid PTE with no zero,
  cache-coherent, no-execute, large-page, physical-adapter, page-table-size
  or reserved fields; lossless page address; local or pool segment; resolved
  backing range; physical arithmetic headroom;64K alignment where required;
 16K alignment; empty native slot; complete native span; contiguous pages
  with uniform segment and ReadOnly inside each16K group.
- Wait control: no job/lease, eligible update, missing/poisoned/uncertain
  process, or3-second deadline ends the wait. Timeout is an outcome, not an
  eligibility reason. UpdateLeaf's independent non-negotiated live-job guard
  remains. After preflight, allocation/registration, logical graph, backing
  and broker failures follow the existing failure/poison path, not a retry.
- FLUSH_TLB: waits only for a live/leased non-negotiated process. Root/ownership
  checks and broker synchronization follow, unchanged.
- CPU paging worker: on node1, a packet with any non-fence record waits for
  every process's live job/lease to end, then keeps the G3 mutex over writes.
  Fence-only packets bypass this gate. No firmware publication is implied.

m1n1 hv_agx_gpuva_v5.c checks epoch/generation, taint and ownership before
mutation. Table registration checks level/alignment/translated range,
zeroed table content, collision and capacity. Parent/leaf updates require
owned registered tables/children/backings, valid index, permissions and
native descriptor encoding; self/foreign references fail. publish_entry
(:339) requires negotiated live mode, actual old descriptor==0 and new!=0
while any owner slot has jobs, plus map-generation headroom. It synchronizes
and invalidates every owner slot, tainting live rollback on failure. Root
relocation/lease/revoke/destroy retain their own busy/ownership checks.
Flush validates owner/root/range and allows negotiated live sync/TLBI.
Broker failures cannot be identified more finely by a KMD preflight reason;
use Wom1G3BrokerFailures if publication itself fails.

Primary sources inspected: current KMD paths above, shared graph and client,
actual m1n1 retained broker; Asahi pgtable.rs:388,486 (zeroed parent creation
and native16K mapping), prior BROKER-Q22B.md memory ordering contract; Mu
Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleAgxSsdt.asl (resource-only
exposure, no hardware initialization); pinned WDK26100 d3dkmddi.h:2081,4693.
[Microsoft UPDATEPAGETABLE documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_buildpagingbuffer_updatepagetable)
defines the first input entry relative to StartIndex and FirstPteVirtualAddress
and explains the4K logical input model for larger native pages.
No external implementation was copied. Windows classification/diagnostics
remain KMD-owned; m1n1 owns UAT/TLBI, Asahi owns its own direct UAT updates,
Mu's ACPI/reservation and all initialization/IRQ/DMA/power/recovery contracts
are unchanged. No live machine state was inferred from these source reads.

## Receipt contract

Wom1G3LivePagingWait is REG_BINARY, written only by the existing paging
receipt worker, without a registry flush. Paging callers only stage memory.
The receipt is3912 bytes: four QWORD header fields,37 QWORD histogram bins,
and16 entries of28 QWORDs each. All integers are little-endian. Version=1,
Bytes=3912, Total counts events, Overwritten counts replaced ring entries.
Slot=(Sequence-1)%16; sort retained entries by Sequence. The histogram is
cumulative even when the ring wraps. Storage and snapshots use the same
receipt spinlock. The serial worker keeps its snapshot in the same nonpaged
receipt allocation (a separate worker-only buffer), adding no3912-byte local
frame or allocation per wait. One update/flush contributes one event if it actually
waited; a CPU mutation packet contributes one event per destructive encoded
record held by its shared gate (so its records share duration). No event for
an admitted/no-job update or fence-only worker packet. Records appear after
wait exit; a system stopping before the bounded wait exits can lose its event.

Entry QWORD order:

```
Sequence Qpc QpcFrequency EndQpc Iterations Exit
Reason LastReason Operation ProcessId ActiveProcessId
ActiveFence CompletedFence JobState
Va RangeBytes SourceVa TableIpa Level StartIndex
EntryCount UpdateFlags Index PteFlags PteAddress
Allocation AllocationOffset AddressKind
```

Qpc is the first blocked observation; duration=(EndQpc-Qpc)/QpcFrequency.
Reason and PTE detail preserve the FIRST refusal; LastReason is the final
classification. Exit:1 idle,2 eligible,3 process invalid,4 timeout. Exit follows
the existing loop's break-condition priority, so delayed rescheduling after
retirement/invalidation is not mislabeled timeout. The older receipt's
wall-clock timed_out counter and the actual wait behavior are unchanged. JobState
bits0/1/2 mean job/lease/nested. ProcessId is the process whose job/lease
blocked this gate, not an OS PID. ActiveProcessId/fences are start snapshots.
Operation is the WDK operation number (11 update,12 flush,9 virtual fill,
8 virtual transfer); physical worker operations are0x100+physical-plan kind.
AddressKind1 means VA,2 means physical-plan offsets. Virtual records preserve
their input VAs in a union with the unused physical plan, keeping the existing
128-byte record size and DMA capacities. No diagnostic field is used to
validate, map, authorize or execute memory.

Update RangeBytes uses level0 stride4K/64K, level1 stride32MiB, level2
stride64GiB. Va is FirstPteVirtualAddress as supplied, including initial
updates; it is not a recovered BO identity. Flush records the original input
range (0 bytes for full/anomalous ranges). PteAddress is the raw page number;
Index is the logical position within the update, ~0ULL if no PTE was examined.
UpdateFlags bits0..3 are Repeat/InitialUpdate/NotifyEviction/Use64KBPages.
For a repeated input, Index identifies the logical failing position, whose
actual input pointer is entry0. Worker physical offsets retain plan semantics;
SourceVa is SecondLocalOffset there, not necessarily the source of a download.

| Reason | Name |
|---|---|
| 0 | Eligible |
| 1 | NoProcess |
| 2 | Disabled |
| 3 | Poisoned |
| 4 | Uncertain |
| 5 | CpuOnly |
| 6 | NotifyEviction |
| 7 | InputShape |
| 8 | PendingShadow |
| 9 | TableLevel |
| 10 | LocalView |
| 11 | NativeStaging |
| 12 | Generation |
| 13 | TableRange |
| 14 | LeafVa |
| 15 | RootChange |
| 16 | ParentPte |
| 17 | ChildResolve |
| 18 | ChildSelf |
| 19 | PartialGroup |
| 20 | LeafRange |
| 21 | LogicalOccupied |
| 22 | ResidentOccupied |
| 23 | InvalidPte |
| 24 | PteFlags |
| 25 | PteAddress |
| 26 | Segment |
| 27 | BackingRange |
| 28 | BackingOverflow |
| 29 | Page64Alignment |
| 30 | NativeAlignment |
| 31 | NativeOccupied |
| 32 | NativeSpan |
| 33 | Noncontiguous |
| 34 | MixedSegment |
| 35 | MixedPermission |
| 36 | CpuMutation |

Codes1,3,4,7,10,13 have direct classifier coverage; for ordinary valid input
some are excluded by earlier validation or the wait-loop break conditions.
They must not be manufactured into actual-wait events. Code0 is eligible;
code36 is the CPU mutation gate. Code31 identifies a native occupied leaf
or parent slot; Level distinguishes them. Code8/9 may concern the target or
a child; Index/PTE detail identifies a child when present.

## Verification and integration checkpoint

New tests execute every classifier result,29 reachable refusal waits through
real BuildPagingBuffer, exact range/fence snapshots, first refusal retention,
timeout, late-wakeup retirement/invalidation, flush, fence-only and idle
bypasses, physical/fill/transfer records, and real virtual record encoding. Existing
live-publication tests execute the real graph/client/retained broker including
both depth-two jobs and partial-publication quarantine. The writer test runs
real staging/worker code with host OS services and unrelated receipt payloads
modeled; it checks two concurrent writers, ring wrap, histogram, a new event
during registry output, stopping, and no caller-side registry I/O.

The pre-q22d classifier was also executed against the same36 controlled
inputs; outcomes match.35 refusal-to-eligible negative mutations each fail
the corresponding runtime reason assertion. An original-tree run first failed
because the destructive update emitted no receipt; worker absence and lost
histogram updates have their own retained RED logs. These are host tests,
not proof of Windows concurrency, WDK stack/codegen or hardware pagewalks.

Integration owner only: build with native WDK /W4 /WX; retain exact7cce5a0e
firmware and Wom1LiveAdditivePaging=1. Preregister Q3PC with this receipt,
retain before/after snapshots plus q22c activation/node receipts, UMD trace,
Q3PC stdout, broker failures and ETL. A waited update inside the pair window
should have Operation11, matching process/active fence, nonzero Reason and
range. Read Reason/Level/Index/PTE flags to select ONE next causal change.
A ring entry absence is not proof of no waits: compare Total/Overwritten and
histogram deltas and allow the deferred worker to drain before collection.
Run GravityMark HD100k temporal0 and collect the same receipt before/after;
histogram deltas identify dominant refusal classes despite ring wrapping.
Retain exact outputs, IA1048576/524288, path3/reason0, no reset/removal, and
head_submit < next_submit < head_complete as overlap criterion. Receipt-only
q22d does not promise overlap or a performance gain. Compare HD to EXP2110
5949 only after correctness. Use the existing exact-package cleanup and
ordinary GPU-visible Code28 recovery; no new recovery procedure.

Independent review identified two diagnostic defects before commit: a3912-byte
worker stack addition and timeout labels overriding an already-retired or
invalid process after a delayed wake. Both were reproduced RED and corrected
GREEN. The final source uses a nonpaged serial-worker snapshot and preserves
the original wait-loop precedence for the new Exit field. Raw review and
RED/GREEN evidence are in the q22 task handoff directory. Native WDK stack
measurement remains an integration gate; host extraction is not a WDK build.

Final offline result (Q22D-baseline-final.log):

```
ran 859 tests in 343 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

Final focused run:23 tests PASS (Q22D-targeted-final.log). Implementation
commit: ecf4e0e3d21c479c6201044a1fe9df12102e9e5b. No baseline exceptions
were added; the six preexisting failures are named in REPORT.md. Hardware,
WDK/package builds and the guest were not accessed. Integration owns the
CHANGES/EXPERIMENTS entries per the task handoff README.
