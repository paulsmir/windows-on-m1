# T7: EXP1911 G4 Parse/Unmapped — offline finding and discriminator

Scope: `codex/t7-g4-unmapped-root`, starting at `612b847e`. No guest,
hardware, package, firmware, or other worktree changes. The task explicitly
allows receipt-only diagnostics when the supplied evidence cannot select a fix.
This change uses that fallback; it does **not** claim to cure GravityMark.

## What EXP1911 actually proves

Decode `kmd-receipts-EXP1911.txt:2` using the native 64-bit alignment of
`render-admission/include/render_admission.h:691` (version 2, 192 bytes):

- Branch 9, status `c000000d`, downstream 3 (`ParseUnmapped`), total 3.
- DMA `0x110c410000`, bytes `0x118`; UMD-private bytes `0x1e0`.
- Subsite 1, **Kind 4 (render)**, ordinal 11, read of one byte at
  **`0x110bc90000`**, GraphPresent 0.
- Graph process ID `0x38`, root IPA `0x9df3b0000`, process generation 1,
  mapping generation `0x6e39`. Receipt PID 4 is the scheduler thread's PID;
  it is not the application PID.
- Logical IPAs: `0xec76000`, `0x855875000`, `0xec74000`, `0x16173000`.
  All segments are 0 and all flags are 3 (valid/writable).

This is a VDM stream access, **not EXP1908's compute helper**:
`shared/include/apple_agx_g4_submit.h:37` defines nine process ranges;
`shared/src/apple_agx_g4_submit.c:262` checks them first (ordinals 0–8),
then the CPU envelope at 9 (`:284`), one attachment at 10 (`:324`),
then `validate_render` checks VdmCtrlStreamBase first (`:86`).
The sizes independently fix that shape: `0x1e0 - 0x118 = 200`, the v3 header;
`0x118 = (8+24) + (8+240)`, one attachment and one render, no compute prelude.

The four physical pages cannot form one AGX leaf: the first IPA is not
16-KiB aligned and the other three are not consecutive 4-KiB pages.
`render-admission/src/gpuva_g3_paging_windows.c:644` requires both properties;
`:660` publishes a zero leaf when incomplete, while `:738` retains the logical
PTEs for CPU/paging use. `gpuva_g3_windows.c:3312` is only an access validator;
it does not remove mappings on Evict. There is no evidence for weakening it.

## Allocation and residency ownership

Paths below are under `drivers/apple-agx/` unless explicitly external.

- Native Asahi `agx_pipe.c:1234` puts the encoder pointer in
  `vdm_ctrl_stream_base`; `agx_pipe.c:1597` adds the VDM BO to the batch.
  The reference root is `.local/reference/mesa/src/gallium/drivers/asahi`.
- `mesa/scripts/build-native-asahi-state.py:5803` projects initial VDM/CDM
  allocation through `AgxWin32AsahiEncoderCreate`.
  `mesa/winsys/agx_win32_asahi_bo.c:431` sets encoder allocation intent;
  `:347` selects Shader for executable BOs, Encoder for low-VA/encoder-intent
  BOs, General otherwise. `shared/include/apple_agx_win32_device_info.h:16`
  defines General=1, Shader=2, Encoder=3. Thus Encoder is the expected class
  of this VDM BO; the old receipt cannot prove its actual allocation handle.
- `mesa/winsys/agx_win32_gpuva_batch.c:782` explicitly includes `batch->vdm.bo`
  in the render residency list. Compute includes its CDM stream and per-job
  preemption buffer at `:931`; batch BOs and both batch pools follow.
- Scratch **data** is a batch reference: native `agx_pipe.c:1515` (compute),
  `:1554`/`:1556` (VS/FS). Helper **code** comes from `ctx->bg_eot`:
  `agx_pipe.c:1536`, `:1416`, `:1421`; external `src/asahi/lib/agx_bg_eot.c:255`
  uploads it to the context cache's executable/low-VA pool (`:213`). It is
  not inherently in the batch pipeline pool. The current compute test's
  helper-in-pipeline-pool fixture is therefore insufficient evidence for
  helper-code residency. This is a separate follow-up, not an explanation
  for EXP1911's ordinal-11 VDM access.
- `render-admission/umd/src/umd_win32_screen.c:570` makes unborrowed BOs
  SystemDirect through the class maximum. `umd_gpuva_windows.c:262` trims
  eligible idle slots; its T1 reservation at `:293` excludes submission
  holds until EvictCb finishes, then clears Resident on success (`:311`).
  `umd/include/umd_residency_trim.h:31` has no shader/encoder exemption.
- `umd_gpuva_windows.c:560` takes submission holds, calls MakeResident for
  nonresident tokens, and fails the request on HRESULT/count failure at
  `:646`. `mesa/winsys/agx_win32_gpuva.c:192` refuses a failed residency
  request and at `:209` waits its paging fence before submitting. It does
  not ignore E_OUTOFMEMORY or E_PENDING on the compute path.
- Creation has a different, best-effort contract: `resident_before_map`
  (`umd_gpuva_windows.c:377`) ignores a failed residency attempt or wait;
  `map_va` at `:406` still maps. Later submit residency is supposed to
  repair it. The supplied logs do not establish that this happened to the
  failing VDM allocation.
- KMD leaf updates, not MakeResident/Evict callbacks directly, own the
  graph. `gpuva_g3_paging_windows.c:560` constructs logical pages from
  VidMm PTEs, `:594` records their allocation identity/offset, `:644`–`:740`
  publishes/removes native leaves and updates the shadow. The same path
  handles map, eviction, restoration, and unmap.
- Primary ABI inspected locally: WDK 26100 `d3dukmdt.h:1475`
  (`D3DDDI_MAKERESIDENT`, output paging fence and trim budget),
  `d3dumddi.h:3830`/`:4371` (Evict/MakeResident callbacks),
  `d3dkmddi.h:2080` and `:4693` (UpdatePageTable flags, native-fence handle
  exception, allocation handle and offset). No Windows interfaces invented.

Windows owns residency/placement and paging scheduling; the UMD owns its
residency references and submission holds; the KMD translates VidMm PTEs and
refuses GPU access without a native graph leaf. Asahi's BO reachability is not
itself a WDDM residency reference. m1n1, Mu, initialization, IRQ, power and
recovery are outside this CPU diagnostic change. No new launch contract is
proposed. The pinned m1n1 broker source/tests were extracted from shared Git
objects at `21c3648d32423def46086e258e0995c737dc9284` only for host replay.

## Why the supplied evidence cannot select a behavioral fix

The receipt proves unsupported system-page placement at parse time, but carries
no allocation identity, allocation class, owner OS PID, or preceding paging
operation. It is the first failure; TotalFailures=3 does not describe all three.
The filtered logs do not include VA history or a per-submit residency roster.
`gm1911a-umd-filtered.log:3955` and `:7766` show successful trims;
`:11944` shows a later rejected MakeResident (`88760870`), which is handled
as failure. `gm1911b-umd-filtered.log:3777` and `:12111` also show trims.
Neither log can be joined to graph process `0x38` using the v2 receipt.
A VDM allocation omitted from the residency list is contradicted by source.
An initial mapping left in system memory versus a later eviction/placement
change remains undecided. Exact-size/VA admission or pinning all shaders
would conceal that distinction.

## Implemented diagnostic correction

`Wom1G4SubmitFailure` version 3 retains the complete 192-byte v2 prefix and
appends the four logical allocation handles/offsets, owner OS PID, and the
matched allocation tracker. Total size is 392 bytes; Track begins at 264.
The tracker gains class, flags, size, allocation type, CPU visibility,
MetadataValid, and last leaf-operation status. Metadata is copied in
`AdmissionG3RecordLeaf` while the callback owns the handle, excluding native
fences. At refusal, `AdmissionG4SnapshotFailure` selects the actual failing
4-KiB page and copies its matching process/handle tracker without dereferencing
the historical pointer. Unknown/collided tracks have TrackFound=0. Existing
unlocked fill/transfer counters remain best-effort diagnostics.
The older `Wom1G3LeafHistory` snapshot embeds this tracker, so its version is
also incremented from 3 to 4. All v3-prefix offsets there remain unchanged.
No changes to residency selection, return status, graph admission, scheduling,
capabilities or firmware. The fixed track table grows by 32 KiB per adapter
G3 state; no new dynamic allocations or paging-path I/O.

The regressions execute the actual RecordLeaf, SnapshotFailure, and registry
writer bodies. `render-admission/src/receipts.c:2518` accepts version 3;
the writer replay verifies the extended payload, failure count, accompanying
bind receipt, and rejection of stale versions or sizes.
RED: the old bodies omit metadata/identity. GREEN: class/size/flags/status,
OS PID, per-page allocation offsets, copy lifetime, collision/foreign-process
rejection, nonzero subpage selection and native-fence exclusion work; the
existing virtual-submit replay still refuses unmapped work before dispatch.

## Smallest hardware discriminator (for the integrating owner, not run here)

WHY THIS HYPOTHESIS: (1) EXP1911's VDM is already in the residency set;
(2) its four logical PTEs are valid system pages but physically incompatible
with one AGX leaf; (3) the old receipt cannot identify the owning allocation
or its last paging operation. The strongest next target is placement and
paging provenance for this stream, not helper code or GPU execution.

Run the same GravityMark D3D12 workload once with only this diagnostic change.
Collect Wom1G4SubmitFailure v3, Wom1G3LeafHistory if present, Wom1G3SystemShapes,
Wom1AllocationLife, pool receipts, and the full UMD trace before cleanup.
Join OwnerOsProcessId to the trace; inspect the selected LogicalAllocation,
Track.ClassId/MetadataValid, AllocationBytes, LastAnyVa/Qpc, LastValidVa/Qpc,
LastFlags (4=NotifyEviction, 8=InitialUpdate, 1=64K), LastStatus and LastSegment.
GraphPresent plus LogicalFlags/LogicalSegment describe the observed mapping,
not the UMD's Resident boolean. A matching eviction identifies that boundary;
an initial mapping identifies creation; a later successful local placement
with a system-only shadow focuses on a subsequent paging update. TrackFound=0
or unmatched LastAnyVa means insufficient attribution, not a guessed verdict.
The owner should preregister the exact package and use the existing known-good
GPU-visible recovery/package-cleanup contract from the active hardware session.
