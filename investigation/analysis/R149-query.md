# R149 QUERY v3 — EXP862 offline analysis

Scope: one current-source pass, no Air, package, production edit, or guard
relaxation. EXP862 source is `fd603f30` (pool change plus QUERY v3, without the
Flush correction). The relevant query, paging, and UMD transport files are
byte-identical at current integration HEAD `318e3fc956c12f8402bae645e4b63d57a537cc13`.

## Exact receipt and interpretation

Input: `.local/experiments/EXP862-r148-pools/hardware-evidence/Wom1G3CopyQueryFailure.bin`
in the main repository; SHA-256
`ede024938b38d4fdaeeca27ed4f56bde96fb4d786b33f29ff89c761edf99714a`.
The separately extracted `query-receipt.bin` has the same hash. Decoded afresh
using `tools/decode_g3_copy_query_failure.py`; all fields are retained in
`.local/experiments/R149-offline-query/query-v3-decoded.json`.

| Field | Value |
|---|---|
| Version / bytes | 3 / 168 |
| Predicate / status | 53 / `0xc000000d` |
| Flags / access | `0xdf` / read (`Write=0`) |
| Native missing level / index | 2 / 8 |
| Reason / component | leaf-absent / leaf-absent |
| Query VA / bytes / missing VA | `0x20000` / 65536 / `0x20000` |
| Graph / context / last SetRoot IPA | all `0x9d6aa0000` |
| Bootstrap IPA | `0x9d92f8000` |
| Process / context SetRoot counts | 1 / 1 |
| Process generation / mapping generation | 1 / 690 |
| KMD graph process ID | 6 (not Windows PID) |
| KMD context identity | `0xffff938861bfb380` |
| Request allocation token | `0x40000fc0` |
| Canonical KMD allocation identity / bytes | `0xffff9388623cf3a0` / 65536 |

Flags assert capture under the mutex, process/context/request presence, bounded
range, available resident-group pointer, and validated canonical allocation.
The bootstrap-root flag is clear. Guards 34–41 have established the device's
open-allocation record, runtime token match, allocation magic, nonzero Win32
class, `CpuVisible=false`, and `ADMISSION_WIN32_ALLOCATION_GPU_LOCAL`.
The receipt does **not** encode the exact Win32 class, resource/BO type, or name.

`ResidentGroupAvailable` is a pointer-availability flag, **not proof that the
allocation is resident or mapped**. `AdmissionG3CopyPte` follows the selected
root's parent edges and finds `TableShadow.ResidentPtes`. For this VA the
four-element group starts at logical 4 KiB index 32; native 16 KiB index is 8.
The capture loop would change both reason fields to `leaf-not-published` if
any of those four entries had `APPLE_AGX_GPUVA_G3_VALID`. Because the recorded
reason remains `leaf-absent`, **all four valid bits are clear**. The array exists,
but there is no valid provenance in the first 16 KiB of the requested 64 KiB.
The walk stops there and proves nothing about the remaining three native leaves.

This excludes, at the captured boundary, a bootstrap-root walk, missing parent
table, overlong tail, read/write rejection, and a partially valid or merely
noncontiguous native group. The 64 KiB path's deliberate `LogicalPtes`
invalidation alone also cannot explain it: QUERY reads `ResidentPtes` instead.
Canonical identity checks at guards 59–60 have not run because guard 53 failed;
the receipt must not be described as proving this VA's ownership by that object.

## Current source contract

Inspected:

- `drivers/apple-agx/render-admission/src/gpuva_g3_windows.c`:
  `AdmissionG3CopyPte`, `AdmissionG3CaptureCopyQueryFailure`, and the complete
  `AdmissionGpuvaG3CopyEscape` admission/copy path.
- `drivers/apple-agx/render-admission/src/gpuva_g3_paging_windows.c`:
  leaf candidate construction, native publication, rollback, table reset and
  subtree retirement, and UpdatePageTable dispatch/root selection.
- `drivers/apple-agx/shared/src/apple_agx_gpuva_g3_graph.c`:
  `AppleAgxGpuvaG3GraphInspectRangeAccess`.
- `drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c`:
  map, make-resident, wait, transfer, submit and evict paths.
- `drivers/apple-agx/mesa/winsys/agx_win32_gpuva.c`:
  bind/submit/retire ordering and failure cleanup.
- Receipt ABI/decoder and `tests/g3_copy_query_v2_cases.c`.

UMD owns reserve/map, the exact MakeResident list, paging-fence synchronization,
and upload requests. VidMm owns UpdatePageTable input and residency decisions;
KMD owns validated logical provenance, native publication and revocation.
No new m1n1, Mu, interrupt, DMA, power, or recovery behavior is proposed here.
Reopening those layers without evidence would not discriminate this boundary.

Microsoft specifies that MakeResident increments allocation residency references
and returns a paging fence for E_PENDING; MapGpuVirtualAddress can target an
allocation or an invalid/zero range and its E_PENDING completion must precede
access. The receipt's array-availability flag is neither contract.
Sources: [MakeResident](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ns-d3dukmdt-d3dddi_makeresident),
[MapGpuVirtualAddress](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_mapgpuvirtualaddresscb),
[nonresident access](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/access-to-non-resident-allocation).

`AdmissionG3UpdateLeaf` copies existing resident provenance, zeroes the updated
range, fills only valid input PTEs, publishes or removes each native group, then
commits the candidate after successful publication. A newly allocated array can
therefore contain untouched invalid groups. Invalid PTE updates clear groups;
level reuse resets the whole shadow; system-subtree retirement can clear system
entries. These distinct histories can produce this same receipt. The receipt
does not log which one occurred, nor a timestamp or paging operation sequence.

The UMD waits on Map/MakeResident paging fences before transfer. QUERY is issued
by `transfer_slot` using the slot's canonical GPUVA and allocation token and
fails before any copy. A failed submit callback causes eviction through
`AgxWin32GpuvaSubmit`; this is a possible lifecycle event to correlate, not a
proven explanation for the later QUERY.

## Validation and limits

`python3 -m unittest discover -s tests -p test_g3_copy_query_receipt.py -v`:
2 tests PASS, including real KMD bodies under both 16 and 64 page profiles.
The `absent-va` case produces the same available-group/leaf-absent distinction;
`absent-shadow` clears the flag and partial unmap yields leaf-not-published.
This confirms diagnostic interpretation; it does not reproduce EXP862's cause.

Generation 690 is separate from first-submit generation 474. Shared root alone
does not equate their allocation, access, order, or cause. No production fix
or new failing regression is justified until the allocation/mapping lifecycle
is identified. Guard 53 remains necessary and unchanged.

## ETL correlation and verdict

Decoded once on the offline builder by the parallel R149 stall analysis:
`.local/experiments/R149-offline-stall/events.jsonl`. The query-specific exact
joins are in `.local/experiments/R149-offline-query/etl-query-correlation.json`.
The decoded event window ends at `09:26:56.5446902Z`; the ETL header end time
`09:39:15.9195558Z` is not proof of event coverage to that point. The native
reader reports zero lost events/buffers, which does not fill missing coverage.

The runtime token is reused and cannot identify this allocation by itself:

| Time UTC | Event / owner | Object | Size / result |
|---|---|---|---|
| 09:25:49.9542819 | DeviceAllocation start, PID 5108 | `hVidMmAlloc=0xffff8287ee17fcf0`, global `0xffff9a010ab34700`, token `0x40000fc0` | preceding global-create size **12288** |
| 09:25:50.0271274 | DeviceAllocation stop | same lifetime | destroyed |
| 09:25:50.1001309 | Context start, PID 4704 | `ContextHandle=0x40000fc0` | different object type |
| 09:25:50.6880074 | DeviceAllocation start, PID 1408 | `hVidMmAlloc=0xffff8287ee8aa850`, global `0xffff9a010a660420`, token `0x40000fc0` | preceding global-create size **12288** |

Both allocation lifetimes are incompatible with QUERY's validated **65536**
bytes. Both belong to adapter `0xffff8287ea2b8000`, whose Adapter event records
type `261`, zero VidPn sources and nine asymmetric nodes. The other adapter
has type `6`. Under the documented
[D3DKMT_ADAPTERTYPE](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/ns-d3dkmthk-_d3dkmt_adaptertype)
bit layout, both have SoftwareDevice set. These events are not evidence for
this Apple allocation. Global allocation pointers are reused too; the joins
above use the immediately preceding creation within each lifetime.

Neither exact canonical KMD pointer nor exact KMD context pointer occurs in the
decoded event fields or as a raw little-endian 64-bit word in the ETL. The ETL
has no PagingOpUpdatePageTable (309), FlushTlb (310), or MakeResident entry/exit
(338/339) events. This is insufficient to reconstruct the last mapping change.
Pointer-domain differences alone do not prove a different boot: KMD objects
and VidMm objects are distinct identities. Boot/trace provenance remains an
independent evidence issue and does not rescue the incompatible token joins.

**Verdict: cause undetermined after the authorized single offline pass.** The
allocation can be named precisely only as the validated canonical object above;
its Mesa BO class, owner PID and VA lifecycle are not captured. No production
fix follows from available evidence.

The missing discriminator is one ordered, boot-identified record connecting:

1. OS PID/device/context, runtime token, canonical allocation, class/size and BO
   owner; map VA and MakeResident result/fence completion for that same identity.
2. The selected root and leaf-table identities plus the last committed
   UpdatePageTable operation covering VA `0x20000`: PTE validity, allocation,
   offset, input mode/page size, mapping generation and publication result;
   also any intervening table reset/reuse or unmap.
3. QUERY's sequence/time/generation and its four resident PTE values.

If that record shows no valid update, fix the demonstrated caller/residency
lifecycle. If a successful valid update was subsequently lost without matching
revocation, replay that exact operation ordering through the real KMD paging
and QUERY bodies and fix the owning graph transaction. The existing replay
provides that regression surface, but inventing the missing operation ordering
would not be a regression for a confirmed defect. No new hardware run is
authorized or preregistered here; any later experiment must preserve the
ordinary GPU-visible Code28 recovery contract and keep guard 53 unchanged.
