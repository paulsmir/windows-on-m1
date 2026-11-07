# T11: DCMP fault evidence and pre-map paging failure repair

This is an **offline partial resolution**, not a demonstrated GravityMark fix.
The deterministic pre-map paging failure below is repaired. The supplied
EXP1922 receipts cannot identify the faulting allocation or establish whether
it was trimmed. No behavioral change to placement, compression, C84/C86,
firmware, or recovery is justified by an invented allocation identity.

Paths prefixed `umd/`, `winsys/`, and `shared/` below mean respectively
`drivers/apple-agx/render-admission/umd/`, `drivers/apple-agx/mesa/winsys/`,
and `drivers/apple-agx/shared/`. Evidence files are in
`/Volumes/pwdev/public_windows/.local/handoff/codex/t11-dcmp-unmapped/`.
Line references describe this branch's source, except explicitly labeled
pre-fix lines. Primary references were read locally; no internet, guest,
hardware, package build, or other worktree was accessed.

## What the hardware receipts establish

`exp1922-kmd-receipts.txt:185`, decoded by
`tools/decode_wom1_gpu_fault.py`, records two Timeout events with valid faults:

| PID | Firmware fence | Fault | Decoded access |
| --- | --- | --- | --- |
| 8528 | 0x181c | 0x119e720d5f82e011 | DCMP(7), unmapped read, level 0, VM slot 1, VA 0x119e720d40 |
| 8716 | 0x24c8 | 0x11ac13509f822011 | DCMP(1), unmapped read, level 0, VM slot 1, VA 0x11ac135080 |

The decoder output is preserved as `fault-decoded.txt` in the handoff.
There is no 2k UMD log. A unit name does not identify a resource format,
allocation handle, or whether the resource uses AGX lossless compression.

The 10k log contains 89 heap records. None of their recorded ranges contains
0x119e720d40. `exp1922-gm10k-umd.log:3937` records the 128 MiB heap
[0x11854b0000,0x118d4b0000); `:7908` records the next higher heap at
0x11a85d0000. This is consistent with a committed resource or internal BO,
but is not a unique allocation identification.

Between those records, `:4134`/`:4136` show a 129 MiB BO and DXGI format 39
(R32_TYPELESS), 4096x4096, bind 0xe (depth, sampler, GPU-only intent).
`:4173`/`:4175` show another 129 MiB BO and format 98 (BC7_UNORM),
1024x1024, bind 0xa. Neither receipt contains its VA. Many other textures and
internal BOs occur in this interval. **Do not choose one by apparent allocation
order, by MiB size, or by treating DCMP as proof of lossless compression.**
In particular the C84 target is format 95 (BC6H_UF16), not BC7.

### Addendum assessment (received during this offline pass)

`T11-fault-va-allocation.md` and the appended TASK identify BC7 1024x1024,
96 layers, 11 mips by a sequential VA model. That is a useful candidate,
but the categorical identity is not measured: BO creation can reuse a cached
BO (`winsys/agx_win32_asahi_bo.c:374`); VA reservation is delegated to the
runtime (`agx_win32_gpuva.c:36`); slow-create receipts omit fast/internal
allocations. No source guarantees a monotonic bump allocator. D3D byte
estimates and truncated native MiB values do not establish bounded cumulative
drift in that model. I retain BC7 as a ranked hypothesis, not an identified
allocation.

Two stronger conclusions do follow **conditional on that candidate**:

1. It was not in either recorded trim. Its own `measure-bo-create` at
   `exp1922-gm10k-umd.log:4173` reports 0x81 MiB. The hook uses `bytes >> 20`
   (`agx_win32_asahi_bo.c:453`), so this is **at least 129 MiB**, not a rounded
   130.12 MiB D3D estimate. Both trims are smaller than a whole such slot:
   first `:4184` is 0x8010000 = 128.0625 MiB; second `:12007` is 51.5 MiB.
   `trim_residency` evicts whole allocation handles and sums slot Bytes;
   it does not trim a 51 MiB portion of a 129 MiB allocation. The second
   trim's size and fault offset therefore cannot establish partial eviction.
2. The addendum's private-staging premise is stale. The successful create
   already requires Bytes <= classInfo->MaximumBytes
   (`umd_win32_screen.c:623`); C75c then selects SystemDirect through that
   maximum (`:642`). An ordinary nonborrowed BC BO is CPU-visible/direct,
   even above 16 MiB. No large-BO private-staging upload should be presumed.
   A texture transfer may still allocate CPU scratch for tiling; that is
   different from the UMD canonical/private-staging allocation pair.

The six live CPU-access receipts do not demonstrate CPU upload of this
candidate either. The live path records >64 MiB access events (up to 512,
`umd_win32_screen.c:88`), while all six surviving records name upload heaps.
Do not join 1620 footprint-copy command receipts to one BC7 resource by pitch
alone: those receipts have no resource identity and recording a command is
not proof of executing a CPU map of that resource.

### Why the requested identity/trim join is unavailable

- `winsys/agx_d3d12_windows.cpp:258` defines `measure-bo-create` as rounded
  MiB, class, and three timings, emitted only for slow creations. It contains
  neither token, allocation handle, nor VA.
- `drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp:2499` defines
  `measure-d3d12-committed-texture` as format, width, height, D3D bytes/64K,
  and bind/presentation/placed bits. It does not carry native layout size,
  metadata offset, native BO identity, or VA.
- `umd/src/umd_gpuva_windows.c:319` defines `measure-trim` as allocation
  count and aggregate bytes; `:362` defines make-resident-trimmed as requested
  count, rounds, total trimmed slots and bytes. Neither identifies a member.
- The full identity is in the in-memory `va_ring` (`umd_gpuva_windows.c:125`)
  and per-allocation trim op 8 (`:317`), but `va_dump` emits it only on a
  copy failure (`:142`). Neither supplied log has `reject-va-history`.
- `measure-bo-cpu-access` does contain identity and range
  (`umd_win32_screen.c:54`), but only for >16 MiB slots and selected events.
  The six EXP1922 rows describe three mapped 128 MiB upload heaps
  (`exp1922-gm10k-umd.log:2817`, `:2918`, `:3102`, plus subsequent rows).
  None contains the fault VA. Absence here does **not** prove the unknown
  faulting allocation was never CPU mapped.

The 19-allocation trim at `exp1922-gm10k-umd.log:12007` covers 0x3380000
bytes (51.5 MiB); its retry at `:12008` returns E_PENDING for one allocation.
This is not evidence that the GPU later referenced one of those 19 slots.

### Correct the execution boundary

Counting actual `measure-d3d12-execute` rows, the 96-reinterpret-copy window
is EXP1921 execute **47**, ending at `exp1921-gm10k-umd.log:15893`, and
EXP1922 execute **48**, ending at `exp1922-gm10k-umd.log:15902` (0x7a6 commands).
EXP1922 execute 47 ends at `:11980` (0x1029 commands).
Both final execute records return S_OK. Their queue-wait records say ordered=1
and owner fences 0x133/0x135 respectively. `winsys/agx_d3d12_windows.cpp:3608`
sets `retired=1` when an outgoing GPU queue wait is enqueued; it need not CPU
retire the work. Thus neither the execute receipt nor retired=1 proves GPU
completion. Firmware fence 0x181c is a different fence domain; the logs do not
provide a direct join from it to the owner fence.

## Candidate audit

### (a) Paging-fence ordering

Pinned Microsoft ABI: `.local/reference/winsdk26100/shared/d3dukmdt.h:1475`
defines D3DDDI_MAKERESIDENT; `:1484` requires synchronizing PagingFenceValue
before GPU use. The callback typedef is `winsdk26100/um/d3dumddi.h:4371`.
The offline Microsoft-document analysis already in
`investigation/analysis/T7-g4-unmapped-root.md` and
`umd/include/umd_residency_trim.h:4` describes budget trim/retry.

The normal submit path does **not** ignore E_PENDING:
`umd_gpuva_windows.c:592` reserves submission holds before trim/retry;
`:635` calls residency_request, and `:660` rejects pending without a fence.
`winsys/agx_win32_gpuva.c:192` calls MakeResident and `:209` waits the returned
paging fence before Submit. `restore_residency` at
`umd_gpuva_windows.c:1285` likewise checks pending-without-fence and waits.
These source paths contradict the simple claim “E_PENDING is submitted
immediately.” No supplied receipt shows a failed paging wait.

A real **different path** was defective: pre-fix `resident_before_map:393`
set Resident and discarded the result of wait_paging_at. It skipped the wait
on S_OK with a nonzero fence and accepted E_PENDING with no fence. Map could
then succeed; later make_resident would see Resident and skip the callback.
This is a deterministic contract defect even though its occurrence in EXP1922
is **unknown**.

The repair keeps the accepted residency reference, waits every nonzero fence,
and sets DrawTerminal on a failed wait or pending-without-fence. Canonical
`map_va:416` and alias `map_va_at:491` return uncertainty (3) before MapCb.
Their callers retain terminal binding state (`agx_win32_gpuva.c:45`, `:109`),
and later make_resident rejects DrawTerminal (`umd_gpuva_windows.c:592`).
The reference is retained for runtime/device teardown, not immediately Evicted:
ordinary buffer destruction refuses DrawTerminal (`umd_win32_screen.c:1229`).
Ordinary MakeResident rejection without an accepted reference retains the
existing best-effort map path. This is not a change to the trim policy.

### (b) Missing residency references

Mesa `agx_batch.c:519` adds sampled resource BOs (and separate stencil) to the
batch. `agx_state.c:2712` calls that helper before packing sampler descriptors.
Windows render (`winsys/agx_win32_gpuva_batch.c:823`) and compute (`:937`)
walk batch BOs and both pools. `add_bo:439` deduplicates allocation tokens and
also includes sparse backing/table BOs. Heap SRV/UAV resources are registered
by `agx_d3d12_windows.cpp:2997` and `agx_win32_asahi_scene.c:474`/`:490`.
C86 stagings use ordinary resource_copy_region, which owns batch hazards.
There is no source proof here of an omitted BO in the observed submission,
and no submitted-token roster in the supplied log to establish membership.

### (c) Layout span, native mapping and restoration

Mesa `src/asahi/layout/layout.c:284` appends metadata after image storage;
`:312` includes it in size_B. `agx_pipe.c:583` allocates the BO using size_B;
`winsys/agx_win32_asahi_bo.c:350` rounds to 64 KiB, and `:423` binds those
bytes. `agx_win32_gpuva.c:34` maps that rounded span. The DDI texture byte
estimate is therefore not the allocation's mapping length. No generic
metadata-tail truncation is established in this path.

KMD `gpuva_g3_paging_windows.c:644` requires a native group to consist of
four consecutive, identically attributed 4 KiB pages with a 16 KiB aligned
base. At `:660` an incomplete group publishes no native leaf, while `:738`
retains the logical PTEs. Map/eviction/restoration use this same update path.
The G4 parser validates enumerated command/attachment ranges
(`shared/src/apple_agx_g4_submit.c:50`, `:129`), not every address a shader
may load from a descriptor. Passing G4 does not prove all sampled texture
pages have native leaves.

**Remaining placement hypothesis:** `agx_d3d12_windows.cpp:976` excludes BC
formats from UNMAPPABLE/GPU-only creation. BC stays CPU-visible, unlike the
T7h single-plane depth/color path. Mesa `layout.h:444` also distinguishes BC
block formats from AGX lossless compression. A sampled BC resource with
scattered aperture pages could therefore survive command-range admission
and fault at sampling. This is source-supported feasibility, not attribution
of 0x119e720d40. Switching BC to GPU-only without implementing its CPU copy
fallback/staging would introduce a new defect: `agx_blit.c:655` falls back to
CPU texture maps; the existing staging blitter requires renderable formats.
No such placement or staging change is made.

### (d) C84 lifetime

`agx_d3d12_windows.cpp:1912` maps source, then destination, copies rows while
both transfers live, then unmaps both. It does not free a temporary GPU BO
before recording a copy. Native `agx_pipe.c:947` retains the resource in the
transfer and its staging path synchronizes the writer before reading.
Existing buffer-cache disposal also checks in-flight use. No specific C84
use-after-free is established. The 96 copies are a temporal correlation,
not proof that the fault belongs to their BC6H destination.

## Ownership and next falsifiable checkpoint

VidMm owns residency, backing placement and paging scheduling. UMD owns its
persistent references, waits and submission holds. KMD owns native UAT
publication and must refuse unrepresentable placement. Mesa/winsys own BO
reachability, layout, hazard and transfer lifetimes. The fix changes only UMD
failure propagation; initialization, interrupts, DMA backing, power, Mu,
m1n1 and recovery remain under their existing owners and launch contracts.
No new live ADT or firmware evidence was obtained, as hardware is prohibited.

WHY THIS HYPOTHESIS: (1) T7h leaves BC CPU-visible; (2) native-leaf publication
explicitly omits scattered groups; (3) command validation cannot establish
all shader descriptor targets. These justify collecting the allocation/native
leaf discriminator, not enabling a speculative BC placement fix.

Before another causal hardware run, capture a durable, opt-in join keyed by
PID, device/owner and allocation lifetime: token, kernel allocation, VA/size,
BO flags/class; resource format/dimensions/layers/mips and layout metadata
span; per-member trim, MakeResident status/fence and completed wait; submitted
token roster; faulting quartet and native leaf provenance. Current generic
trace settings alone do not emit the in-memory VA ring. This instrumentation
is a follow-up requirement, not something this patch claims to provide.
It distinguishes missing references, incomplete paging, unsupported placement,
and out-of-range descriptor access in one run.

Integration owner's requested checkpoint: GravityMark D3D12 10k and 2k short
profiles with Wom1GpuFaultInfoRead=1. Require GPU completion through the first
full benchmark window (EXP1921 #47 / EXP1922 #48), no DCMP unmapped fault,
and no subsequent device removal/TDR. If pre-map waits fail, this branch must
stop allocation/admission before GPU use; that fail-closed result is not a
benchmark success. Preserve the existing exact-package GPU-visible recovery
contract. No checkpoint or recovery action was performed here.

## Separate quiesce failure: design note only

`exp1922-kmd-receipts.txt:188` records ResetEngine twice and ResetFromTimeout
all failing in quiesce with c0000483; `:184` records one deadline expiry and
longest job 3061 ms, fence 0x24c9. This follows the faulted 0x24c8 job and is
not evidence that fixing the original allocation would repair recovery.

Asahi `drivers/gpu/drm/asahi/gpu.rs:1006` walks pending stamps, decodes the
version-specific event slot, marks culprit work with the fault and other work
Killed, then clears pending info/wait values before resume. Our
`shared/src/apple_agx_g13_queue_runtime.c:578` passes fence/kind to recovery
without that per-slot pending-work protocol; the prior T10 note documents
the counter/slot mismatch. A follow-up must capture counter, slot, pending
stamp values and queue progress, reject stale events, mark failed/collateral
work without reporting successful rendering, and prove firmware quiescence
before releasing allocations. Do not lengthen the timeout or fabricate
success stamps. No recovery implementation is included.

## Offline verification

Regression: actual extracted C++ canonical/alias mapping and residency helper,
with controlled WDK callbacks under ASan/UBSan. Original source failed the
accepted-residency uncertainty assertion; repaired source passes pending wait
failure, pending without fence, S_OK wait failure, S_OK with/without fence,
retry refusal, and the original successful pending/ordinary-refusal cases.
Removing either caller's uncertainty guard independently fails the replay.
Eight affected tests pass. Independent review found no blocking issue.

Required unmodified baseline:

```text
ran 415 tests in 175 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

No known-failure entries or experiment/change ledgers were changed. Hardware
validation and a native Windows build were not performed. The exact faulting
allocation, trim membership, native quartet and root cause remain unknown;
this branch must not be described as the GravityMark DCMP fix.
