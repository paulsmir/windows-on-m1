# EXP1938 final dump verdict and heap draw-lifetime remedy

## Final dump verdict

The saved-dump route is closed. No fifth CDB script is proposed. Pass 4
completed but did not authenticate an ADMISSION_CONTEXT or read a runtime
instance, so it does not select firmware restart, protocol handling or a
transport remedy.

Evidence paths below use H =
`/Volumes/pwdev/public_windows/.local/handoff/codex/dump1938`.

- `H/cdb-output-4.txt:64` resolves the private PDB; `:78-80` records the
  same 0x116 and arguments `ffffd684d64e3010`, `fffff803c291a9f0`,
  `ffffffffc0000483`, `3`.
- `:151-155` shows the reset wrapper loading its callback from
  DXGADAPTER+0x240 and miniport argument from DXGADAPTER+0x120.
  `:354` gives DXGADAPTER `ffffd684d3ce0000`; `:358` gives its render
  object `ffffd684d39bf010`. `:364` corroborates the reverse parent link.
  The script does not read the miniport pointer at `ffffd684d3ce0120`.
  This is a missing observation, not an absent/zero miniport or RTKit session.
- `:1347-1389` displays type layouts only: PlatformRuntime+0x130,
  runtime Rtkit+0x5c0, session CrashlogCrashed+0x71. No actual values or
  crash-buffer bytes are displayed. The END marker at `:1391` certifies
  script completion, not session health.
- `drivers/apple-agx/shared/src/apple_agx_rtkit_session.c:261-298`
  distinguishes invalid lifecycle, transport, protocol and sticky crash
  outcomes. None may safely be ignored merely because the aggregate reset
  outcome was poll failure.

The earlier hardware evidence remains separate. The exp1937 handoff
`REPORT.md:53-76` records an unmapped read, NativePresent=0, four zero logical
PTEs and invalidation/initialization history. The exp1938 handoff
`REPORT.md:49-57` records a DWM fault and a **busy** mapping capture; its
zero-filled mapping fields are not proof of zero PTEs. No BO identity links
these events to the source defect below.

## Source-proven behavioral defect

The final-round task permits a fault-avoiding change when the dump cannot
settle RTKit. The selected change fixes a reproducible missing-reference
path in the UMD, rather than making an unproven firmware reset succeed.

Files below are relative to the repository. M denotes the pinned reference
`.local/reference/mesa/src/gallium/drivers/asahi`.

1. Before this change, `mesa/winsys/agx_d3d12_windows.cpp` under
   `drivers/apple-agx`, at HEAD `9f05bada:3692`, called `bind_heaps` before
   `pipe->draw_vbo`. The binder publishes CB14 sampler indices and registers
   descriptor heap reads/writes (`agx_d3d12_windows.cpp:3085-3133`).
2. `agx_win32_asahi_scene.c:486-518` allocates sampler handles in the
   selected **batch's** sampler heap. Resource reads/writes at `:526-556`
   select and initialize that batch before invoking native hazard tracking.
3. Native `M/agx_state.c:4914-4923` explicitly legalizes feedback and
   stream-output hazards before obtaining a batch. In particular,
   `:4830-4858` explains the read-to-XFB write hazard and flushes readers.
   `M/agx_batch.c:517-535` registers the BO reference on one batch, and
   `:538-575` manages writer ownership. Flushing does not transfer that
   batch's references to the next batch.
4. The Windows wrapper already implemented this ordering, but it ran
   **after** the D3D12 binder. Thus its hazard flush could submit/reset the
   batch carrying the heap references and relative sampler indices before
   the actual draw obtained a new batch. Merely fixing interstage sampler
   rollover (EXP1936) does not close this later flush boundary.
5. `agx_win32_gpuva_batch.c:827-847` collects the selected batch's BO
   bitset/pools. `render-admission/umd/src/umd_gpuva_windows.c:600-618`
   holds precisely the submitted residency set. `:1613-1659,1736` proves
   native backing for that held set; a missing reference cannot be repaired
   by validating the references that are present.

The regression executes the original owner call site, actual heap binder,
actual native sampler allocator, final projected draw wrapper and its
pre-batch body region with a simulated Gallium hazard flush. It observes the
new draw batch without the required references. This is a source/test proof
of the defect, not identification of the original faulting allocation.

## Fix and ownership contract

- A context-scoped frontend callback binds the D3D12 heaps after native
  legalization and before selecting/entering the draw batch
  (`native-asahi-batch-lifecycle.py:295-336`). Each recursive native draw
  uses the same boundary.
- Predicate evaluation, internal legalization draws and the binding callback
  itself run with the callback temporarily disabled. Internal meta draws do
  not consume application heaps. The callback is restored for native
  lowering recursion.
- The body no longer repeats legalization after the binder has introduced
  new readers (`native-asahi-batch-lifecycle.py:364-366`). In particular,
  repeating XFB legalization could immediately flush those new references.
- `agx_d3d12_windows.cpp:3136-3144,3701-3711` scopes the callback to the
  owner draw, retains the first failing HRESULT and clears callback/data
  before stack storage expires. No-op/predicated-away draws need no heap
  publication. Failure prevents encoding and reaches the existing error path.
- `agx_win32_asahi_scene.h:69-72` declares the C bridge used by the owner;
  the scene translation unit includes this header. The native context fields
  follow `pipe_context base`, preserving offset-zero downcasts
  (`native-asahi-batch-lifecycle.py:56-60`; `M/agx_state.h:755-757`).
  The full-projection layout regression catches placing fields before base.

The owner holds its existing exclusive context lock. UMD/Mesa owns resource
references, descriptor publication and the draw batch; VidMm owns paging;
the existing KMD owns native validation, DMA submission, completion and
recovery. The local WDK 26100 `d3dukmdt.h:1475-1489` defines allocation-list
residency and the paging fence that must precede GPU use;
`d3dumddi.h:3999-4012,4371-4372,4418-4419` defines the unchanged callback
interfaces. No Windows ABI/capability, GPU power, interrupt, DMA mapping,
firmware launch, m1n1 or Mu behavior is changed. This UMD defect needs no
new register/IRQ/power assumption or live measurement. Reference Mesa code
is inspected in place; existing projected MIT source notices remain intact.

The saved launch contract remains EXP1938's recorded m1n1 0d3a75b3 and Mu
artifact, as identified in the exp1938 handoff `REPORT.md:18-26`. This work
does not claim to revalidate either launch profile or change firmware restart
ownership. A firmware restart would need a separate proven DMA-stop, queue,
lease/mapping, bootstrap and failure-recovery design.

## Verification and review

- Original production code: the final two-test regression runs and fails
  on missing actual-batch references, internal-draw contamination and eager
  no-op bindings (`H/round5-final-red.log`).
- Mutation restoring duplicate body legalization: fails on the missing
  actual-batch references (`H/round5-body-mutation.log`). Both mutations
  were restored before final tests.
- Independent review found an initial base-offset mistake in the patch.
  A new regression compiled the actual generated header and failed with
  `offsetof(base) == 16`, then passed after moving the fields after base
  (`H/round5-review-red.log`).
- Eight runtime cases cover hazard flush, meta draw, binding allocation
  failure, predicate rejection, zero-count draw, ordinary non-heap caller,
  legalization failure and recursive draw with another batch switch.
  Fixtures compile with warnings-as-errors and ASan/UBSan. The actual
  source projection is executed, not merely searched for expected strings.
- Existing empty-draw, predicate and legalization fixtures gain the two
  new context fields without relaxing their assertions. Compute and
  interstage sampler rollover retain their existing coverage.

Exact final focused and baseline results, commit and tree are in
`H/REPORT.md`. Native Windows compilation and hardware validation remain
outstanding. Rebuild every header-dependent native UMD object coherently
for each packaged ABI; do not mix old agx_context-layout objects with new
ones. Heap-binding time now falls under TimeDrawVbo, not TimeBind; compare
the combined times when using older performance receipts.

## WHAT IS STILL UNKNOWN and hardware checkpoint

Unknown: the EXP1938 RTKit drain result, authenticated runtime/crash flag,
crash-buffer contents and faulting BO; whether either EXP1937 or EXP1938
actually exercised this heap-lifetime gap; whether other stale-address or
mapping defects remain. This change does not implement firmware restart or
prove GravityMark completion. In particular EXP1938's initial fault was in
DWM, so a D3D12-specific candidate cannot be asserted to explain it.

WHY THIS HYPOTHESIS:

1. EXP1937 proves an unmapped GPU access in a graph with no native leaf;
   losing a BO from the submission's residency set is causally relevant.
2. The current UMD has a reproducible later-than-binding flush which loses
   heap references and relative sampler handles; the final-source regression
   distinguishes it without guessing fault addresses or disabling eviction.
3. Existing native validation covers only held references, so it cannot
   reject a missing heap reference before the GPU dereferences it.

Claude's smallest behavioral checkpoint: after coherent native compilation
and the existing heap/graphics smoke checks, run the same 10K ARM64 D3D12
windowed 1280x720 GravityMark workload on the original full-owner profile,
with Wom1GpuFaultInfoRead=1 and Wom1PoolHalfSize=1. Change only this UMD
implementation; preregister exact source/package hashes and preserve the
known recovery artifact. No diagnostic-only package or new CDB round.

Success: natural benchmark completion with timing/score/image, original
boot/device identity preserved, no unmapped fault, RTKit polling failure,
device removal or 0x116. Failure: any of those events or failure to complete;
collect evidence and do not launch a second workload on an unexplained
replacement boot. Repetition rejects attribution of this gap to that run,
not the host-proven lifetime correction. Record receipts, application output,
boot/PID identity and dump first, then use the established exact-package
GPU-visible recovery to Code28/package absent/eight CPUs. Claude owns this
future checkpoint; none of these actions was performed here.
