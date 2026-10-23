# T6b / C75c: failed CPU maps and evictable direct buffers

2026-10-23, offline in `codex/t6b-evictable-direct`, starting at
`7d5c28379d9bd1e7a3b454bb200ea7a28d311d47`.

Both requested changes are implemented and exercised offline. C75 is reapplied
as a hardware candidate; there is no claim of hardware validation or successful
zero-copy performance. This follows `C75b-large-direct-buffers.md` and the T6b
handoff, which explicitly authorizes reconsidering that note's deferred policy.

## EXP1910 and the CPU-read boundary

The supplied UmdArm64-1910.dll has SHA-256
`99216127b52e523f7b5761c77cd4c71cf16970de21f9ce69c8ca24a464f32916`.
Offline disassembly at RVA `0xaed10` calls buffer_map, at `0xaed18` tests only
the returned transfer, at `0xaed1c` loads through the returned map pointer,
and at `0xaed28`/`0xaed2c` clamps the result before buffer_unmap.

That sequence matches pinned Mesa
`src/gallium/auxiliary/util/u_draw.c:212-224`, `util_draw_indirect`, reached
from `src/gallium/drivers/asahi/agx_state.c:4891`. It is a stronger match than
the owner's explicit ExecuteIndirect expansion, which already checked the map
pointer. Without a PDB or original registers this is a source/instruction match,
not proof of the original callback's reason for failure.

The producer defect is deterministic: pinned Mesa `agx_pipe.c:965-971`
allocates and publishes a transfer before obtaining a BO map, then line 1026
adds the pixel offset to a possibly NULL BO pointer. Offset zero leaves a NULL
pointer with a valid transfer; nonzero offsets manufacture an invalid pointer.
The buffer case passes directly through `u_transfer_helper.c:375-376`.

CPU buffer-read audit (Mesa paths are relative to `.local/reference/mesa/`):

| Consumer | Evidence and handling |
|---|---|
| D3D12 explicit count/argument read | `agx_d3d12_windows.cpp:556`, called by `agx_d3d12_ddi.cpp:6292/6306`; now checks pointer and transfer, releases a returned transfer, returns DEVICE_HUNG on failure. |
| D3D12 compressed-texture upload | `agx_d3d12_windows.cpp:1849-1856`; buffer READ and texture WRITE maps checked together, no memcpy on failure, DEVICE_HUNG and transfer cleanup. |
| D3D12 ordinary texture copy | `agx_d3d12_windows.cpp:1914-1932`; buffer READ for upload / WRITE for download, checks both map pairs before copying and returns DEVICE_HUNG on failure. |
| D3D10/11 ResourceMap | Original `Resource.cpp:530` is replaced by projector `build-native-asahi-state.py:5238-5242`; checks map and transfer before exposing data, now SetError(DEVICE_HUNG). READ and READWRITE share this path. |
| D3D10/11 initial data and ResourceUpdate | Original `Resource.cpp:333/906` and projected update at `build-native-asahi-state.py:5362` are WRITE-only. They do not read through a BO map; map failure also reaches the corrected producer. |
| Gallium indirect count and argument expansion | `u_draw.c:145/165/214/233`, both util_draw_indirect and util_draw_indirect_read; they check transfer, so the corrected producer prevents both dereferences. |
| DrawAuto stream-output count | `agx_streamout.c:133` calls `u_inlines.h:628` pipe_buffer_read; its NULL check precedes memcpy, offset_B starts at zero, and the producer now marks the context fault. |

The Windows projection at `build-native-asahi-state.py:6305` fixes the producer:
clear the output transfer at entry, check allocations and each BO map, do no
NULL-plus-offset arithmetic or read-side detiling, release staging/temporary/
resource ownership on failure and call AgxWin32AsahiMarkContextFault. The same
fix covers linear and texture staging/read paths. It preserves upstream source
and notices: only the derived Windows overlay changes, with unique anchors
and output hashes. No external implementation is copied into the repository.

The existing D3D12 completion path converts the context fault to
DXGI_ERROR_DEVICE_HUNG (`agx_d3d12_windows.cpp:3238-3239`). The explicit owner
read/copy functions return that error immediately. The executable regression
combines the projected map body with the actual unchanged util_draw_indirect
body: before the fix it faults at the count read under ASan; after the fix it
returns with no draw, one context fault and all transfer references released.

## WINDOWS CONTRACT

Local WDK 26100 primary definitions inspected:

- `d3dukmdt.h:1475-1489`: MakeResident input/output allocation count, paging
  queue/fence and NumBytesToTrim. A returned paging fence provides the
  synchronization before use; success alone is not permission to ignore it.
- `d3dumddi.h:3830-3836` and `d3dukmdt.h:1491`: Evict allocation list and
  EvictOnlyIfNecessary, which drops residency requirements without demanding
  immediate paging out. The existing driver maintains one reference per slot.
- `d3dumddi.h:3480-3492` and `d3dukmdt.h:544`: Lock allocation, entire-allocation
  flag and output CPU pointer. No changes to the callback ABI or lock flags.
- Previously inspected Microsoft segment/paging semantics remain recorded in
  `M2-f3-demand-pool.md:12-26`. No online documents were fetched for this
  explicitly offline task.

Conservative UMD policy: a CPU-visible SystemDirect allocation can release its
persistent residency requirement only between CPU locks and ownership holds.
No active map, transition, copy, submission, source, presentation, borrowed
owner or KMT residency hold may exist. This is lifetime eligibility, not a
promise about VidMm's next placement.

`umd_residency_trim.h:31` keeps every exclusion except the unconditional
SystemDirect veto. `trim_residency` at `umd_gpuva_windows.c:289-315` already
reserves selected slots with Transition through EvictCb, retaining residency
on failure. That earlier T1 fix remains intact.

The CPU map path now takes Transition under the slot lock
(`umd_win32_screen.c:953-979`), calls
AdmissionUmdGpuvaRestoreCpuMapResidency, and retains the reservation across
MakeResident, paging completion and LockCb. `Mapped` takes over atomically at
`:1029`. No trim can select the slot in between. Unmap leaves Mapped set on an
UnlockCb failure and clears it only after success (`:1068-1089`). A persistent
D3D12 Map therefore remains non-evictable until its real final unlock.

Restoration (`umd_gpuva_windows.c:1264-1304`) requires one accepted allocation,
records its residency reference, and waits for every nonzero returned paging
fence, including S_OK with a fence. Pending with no fence or a failed wait
makes the direct map fail and the device terminal. Retaining accepted residency
accounting allows teardown to release it, while terminality prevents a later
map from seeing Resident and skipping an unfinished wait. Already-resident
slots take no second reference.

GPU use continues through the existing `make_resident` path at
`umd_gpuva_windows.c:560`: submission/copy holds precede the residency request;
only nonresident slots acquire a new reference. No new GPU submission protocol
is introduced.

## AGX/ASAHI CONTRACT AND TRANSLATION

Asahi may retain its BO CPU map between Gallium transfers. Consequently, an
idle logical resource is not necessarily an evictable allocation: a retained
native CPU lock deliberately remains pinned. Removing that protection would
violate persistent-map pointer lifetime. C75 (`umd_win32_screen.c:574`) now
selects SystemDirect up to the advertised class maximum, excluding borrowed
staging, with these residency guards in place.

Ownership is unchanged: UMD serializes CPU maps, submission and residency;
VidMm selects placement and owns CPU views; KMD reserves/commits segment backing
and validates UAT mappings; m1n1 owns the IPA aliases; Mu owns the distinct
firmware reserve. Initialization, interrupts, DMA authorization, power and
recovery are untouched. The m1n1/Mu source contract inspected for T6 is recorded
in C75b; this CPU/UMD change does not need new machine-state measurements.

Unlike Linux BO mapping, Windows CPU visibility admits an aperture fallback.
`allocation_windows.c:500-535` documents prior unsupported 4 KiB system-page
placements at pressure and preserves that fallback in the supported segment
set. This task does not remove it or weaken KMD validation. Eviction can now
make progress, but that alone cannot establish that the next placement is
GPU-usable. Treat zero-copy performance and aperture behavior as unvalidated.

## Tests and checkpoint

Executable RED -> GREEN:

- `test_umd_buffer_map_failure`: count and argument reads, nonzero offsets,
  read-side texture maps, reference cleanup, successful draw and owner errors.
- `test_umd_direct_map_residency`: actual map/restore/trim/submission/unmap
  bodies; callbacks try trim and submission during the transition; pending,
  S_OK with/without fence, bad accepted count, no pending fence, wait failure,
  persistent map, lock/unlock failures, holds, eviction and subsequent remap.
- `test_umd_residency_trim`: idle SystemDirect eligibility and every exclusion.
- `test_umd_system_direct_limit`: execute the actual policy expression at small,
  >16 MiB and class-limit boundaries, including borrowed staging.

Existing volume-copy fixture also exercises failed map/transfer pairs and
unchanged destination bytes. The focused set passed 56 tests. The full command
`cd tests && python3 run_against_baseline.py` reported:

```
ran 361 tests in 153 modules; 8 failing, 8 known, 0 NEW, 1 FIXED
FIXED test_umd_system_direct_limit.SystemDirectLimit.test_threshold_within_kmd_class_limit # pre-existing on 2026-10-22
BASELINE_OK
```

No hardware, builder, package build/install or guest access occurred. The
missing exact m1n1 source for host replay was materialized in this worktree from
local shared Git objects; no other checkout was used or modified.

WHAT IS STILL UNKNOWN: native Windows compilation; the original callback's
failure reason and crash registers; actual VidMm placement after direct-slot
eviction; how much of the workload remains pinned by retained/persistent CPU
maps; observed performance and memory pressure behavior.

Smallest hardware checkpoint, integration owner only: use one direct buffer
larger than 16 MiB, write/read its full span, unmap, induce a confirmed residency
trim, then map/read again and submit GPU work. Record MakeResident result,
paging fence/wait, Lock result, placement, content and UAT admission. An
outstanding Map must prevent trim. Then run GravityMark D3D12 10k/2k and D3D11,
check XFER near zero and no AV, confirm nonzero trim progress under budget
pressure, and run PRESSURE plus regressions. Retain the known-good recovery
artifact and normal GPU-visible exact-package rollback. This note initiates no
hardware run and makes no hardware verdict.
