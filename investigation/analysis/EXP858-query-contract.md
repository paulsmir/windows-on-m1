# EXP858: one offline QUERY contract pass — 2026-09-28

Verdict: **no predicate is proved to fail on every real call**. Do not change
handle namespaces, drop ownership checks, force graph membership, or relax flags.
The requested change is diagnostic only; no package, install, Air access or run.
The saved EXP858 trace proves first QUERY followed by rollback, not its return
status. A successful QUERY followed by UMD generation/staging failure remains
possible. This concludes the one offline pass; no historical comparison loop.

## Source and contract references

Paths below are relative to the repository. Line references describe the current
receipt implementation, with the original R145 conditions unchanged.

- **U**: `drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c:231–276`:
  copy_escape/transfer_slot; `:157–208`: MakeResident producer. `umd_win32_screen.c:540–630`
  creates canonical GPU-local and separate CPU staging allocations and retains
  AllocateCb's allocation token. `drivers/apple-agx/mesa/winsys/agx_win32_gpuva.c:128–141`
  waits for the returned paging fence before Submit calls transfer_slot.
- **K**: `drivers/apple-agx/render-admission/src/gpuva_g3_windows.c:517–670`:
  CopyEscape; `:498–515`: CopyPte walks the resident table shadow; same file's
  CreateProcess/CreateContext integration establishes the process graph.
- **O**: `drivers/apple-agx/render-admission/src/allocation_windows.c:639–690`:
  OpenAllocation returns an ADMISSION_OPEN_ALLOCATION containing runtime token,
  KMD allocation-object pointer and owning device. `render_win32_transport.c:22–79`
  decodes the private description without normalizing GPU_LOCAL or CpuVisible.
- **P**: `drivers/apple-agx/render-admission/src/gpuva_g3_paging_windows.c:350–450`:
  UPDATEPAGETABLE records allocation pointer/offset/segment and expands 64K pages
  into logical 4K provenance. Shared graph initialization requires a nonzero
  process generation; successful mappings advance mapping generation.
- **C**: `drivers/apple-agx/render-admission/src/callbacks.c:43–94,246–257`:
  CreateDevice and escape dispatch; CreateContext returns the attached driver
  context. COPY dispatch first requires its magic; wrong magic does not enter K.
- **R**: `tests/g3_r145_copy_cases.c`, `tests/g3_vidmm_replay_shim.h`, and
  `tests/g3_vidmm_replay.py`: actual extracted K/P/C bodies, with external OS/broker
  services replaced. Original R145 token71 and valid object bridge were manually
  supplied; new regression uses token0x81234071, explicitly distinct from the
  KMD pointer. This strengthens namespace coverage but is not an OS execution.

Microsoft references, inspected for this pass:

- **D1** [D3DDDICB_ESCAPE](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/ns-d3dumddi-_d3dddicb_escape):
  runtime device handle, context from CreateContextCb, and HardwareAccess request.
- **D2** [DXGKARG_ESCAPE](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_escape):
  driver device/context handles and KMD process handle; the latter may be NULL.
- **D3** [GETHANDLEDATA arguments](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkargcb_gethandledata),
  [GetHandleData](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkcb_gethandledata),
  [AcquireHandleData](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkcb_acquirehandledata):
  hObject is a D3DKMT handle; allocation DeviceSpecific selects OpenAllocation's
  driver-private data. Acquire retains a reference until ReleaseHandleData.
- **D4** [UPDATEPAGETABLE](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_buildpagingbuffer_updatepagetable):
  hAllocation is the KMD CreateAllocation handle, not the UMD runtime token.
- **D5** [D3DDDI_ESCAPEFLAGS](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ns-d3dukmdt-_d3dddi_escapeflags):
  HardwareAccess is bit0; VirtualMachineData is OS-only. Running in a hypervisor
  does not by itself establish that Windows sets this bit on this escape.

The pinned WDK26100 d3dkmddi.h/d3dkmthk.h/d3dukmdt.h declarations were also checked.

## Every rejection predicate, in evaluation order

IDs are stable diagnostic IDs, not new admission rules. Unless stated otherwise,
status is STATUS_INVALID_PARAMETER (0xc000000d). Real values below distinguish
source-guaranteed producer values from **unknown runtime state**: EXP858 did not
capture the full DXGKARG_ESCAPE, referenced open object or graph snapshot.
R uses a started adapter, valid callbacks/state, device d/context c/process p,
64K allocation, VA0x10000, and an explicit paging publication before QUERY.

| ID | KMD rejects when | Real Windows path (source/docs) | R145 replay supplied |
|---|---|---|---|
| 1 | adapter NULL or not Started | C supplies adapter; EXP858 StartDevice succeeded; exact teardown state unknown | started adapter a |
| 2 | args NULL | OS callback supplies escape arguments (D2) | non-NULL stack args |
| 3 | IRQL not PASSIVE | HardwareAccess escape is expected at passive; exact observed IRQL absent | PASSIVE stub |
| 4 | Flags.Value != 1 | U zero-initializes flags then HardwareAccess=1 (D1/D5); OS-added flags unknown | exactly1 |
| 5 | private size != sizeof request | U sets sizeof=65600, shared ABI | same sizeof |
| 6 | private buffer NULL | U allocated 65600-byte buffer; OS buffers it (D2) | calloc buffer |
| 7 | Acquire or Release callback missing | StartDevice interface copied from OS; exact pointers not captured | both explicit stubs |
| 8 | G3 state NULL | K initialization should install it; runtime state unknown; INVALID_DEVICE_STATE | manually initialized state |
| 9 | request pool allocation fails | dynamic nonpaged allocation; INSUFFICIENT_RESOURCES possible | malloc-backed success |
| 10 | Magic wrong | U supplies COPY magic; C routes only that magic | correct magic |
| 11 | Version != 1 | U shared COPY_VERSION=1 | 1 |
| 12 | Bytes != sizeof | U sizeof=65600 | same |
| 13 | either reserved field nonzero | HeapAlloc ZERO_MEMORY | calloc zero |
| 14 | operation > DOWNLOAD | U QUERY=0 | QUERY=0; cannot fail for QUERY |
| 15 | Allocation == 0 | U canonical slot KernelAllocation, AllocateCb output; not staging token | token71 (now0x81234071) |
| 16 | GpuVa == 0 | U CanonicalGpuVa from successful bind/map | 0x10000 |
| 17 | GPUVA not 64K aligned | canonical bind reserves aligned mapping | 0x10000 |
| 18 | GPUVA >= 2^39 | advertised/map range is bounded; actual QUERY VA not saved | 0x10000 |
| 19 | TransferBytes > 65536 | QUERY buffer starts zero | zero |
| 20 | QUERY Offset, TransferBytes, MappingGeneration or ProcessGeneration nonzero | all zero at fresh U QUERY; generations are outputs | all zero |
| 21 | non-QUERY lacks transfer/generations | not applicable to QUERY | not applicable |
| 22 | acquired open object or release reference NULL | token is passed to Acquire with ALLOCATION + DeviceSpecific=1 (D3); runtime resolution not observed; INVALID_HANDLE (0xc0000008) | r145_acquire directly fabricates a valid resolution and reference |
| 23 | process not found by hKmdProcessHandle | D2 uses KMD CreateProcess handle, may be NULL; exact supplied handle unknown | p explicitly linked in state |
| 24 | process Poisoned | runtime failure state unknown | zero |
| 25 | Graph.Uncertain | runtime graph state unknown | zero |
| 26 | !Graph.Created | K graph created during process setup; actual state at QUERY unknown | sys_process explicitly creates graph |
| 27 | hContext not in process context list | U uses nonzero KernelContext from callback; D2 translates to driver context; exact linkage unknown | c explicitly linked to p |
| 28 | !context.Win32Transport | real private CreateContext data selects Win32 path; observed exact context unknown | TRUE |
| 29 | context closing | concurrent lifetime unknown | FALSE |
| 30 | context poisoned | runtime failure state unknown | FALSE |
| 31 | context device NULL | C attaches owning device; teardown state unknown | &d.Object |
| 32 | context's ADMISSION_DEVICE != hDevice | U passes nonzero runtime device; D2 translates to KMD device; actual equality unknown | &d |
| 33 | context device adapter != this adapter | C establishes association; actual equality unknown | &a.ObjectAdapter |
| 34 | open object's Magic wrong | O initializes correct magic if D3 resolution returns the expected object | manually correct |
| 35 | opened.Device != hDevice | O saves KMD device; D2 should supply same device for this open | manually &d |
| 36 | opened.Allocation NULL | O saves object from KMD allocation | manually &allocation.Object |
| 37 | opened.RuntimeAllocation != q.Allocation | O saves OpenAllocation info.hAllocation; U sends AllocateCb D3DKMT token; D3 bridge should resolve that open, actual equality unobserved | callback only resolves the same token, making this equality tautological in old shim |
| 38 | allocation object's Magic wrong | O/create initialize it; lifetime protected by reference | manually correct |
| 39 | allocation Win32ClassId == 0 | U private canonical allocation supplies typed intent, O decoder preserves it | class1 |
| 40 | allocation CpuVisible != 0 | U canonical GPU_LOCAL is CpuVisible0; CPU staging is a distinct handle | zero |
| 41 | Type != GPU_LOCAL (0x100) | U canonical private type0x100 survives O; not staging/system type | GPU_LOCAL |
| 42 | state.ActiveProcess != NULL | global active GPU job could belong to another process; not implied by completed paging wait; DEVICE_BUSY (0x80000011) | NULL |
| 43 | Graph.JobInFlight | GPU workload lifetime independent of paging; DEVICE_BUSY | zero |
| 44 | Graph.LeaseToken != 0 | live dispatch lease unknown; DEVICE_BUSY | zero |
| 45 | non-QUERY generations differ | QUERY does not compare input generation against graph; K supplies output generations | not applicable to QUERY |
| 46 | UPLOAD is readonly or lacks CpuWrite | not applicable to QUERY | not applicable |
| 47 | DOWNLOAD lacks CpuRead | not applicable to QUERY | not applicable |
| 48 | selected length == 0 | QUERY selects full canonical allocation Size; U creates positive size | 65536 |
| 49 | selected length > MAXULONG | U allocation caps are much smaller than 4GiB; exact selected size unobserved | 65536 |
| 50 | Offset > allocation Size | QUERY Offset=0; cannot fail after48 | zero |
| 51 | length > Size-Offset | QUERY length=Size and Offset=0; cannot fail after20 | equal Size |
| 52 | GPUVA+Offset/length outside 39-bit span | mapped canonical extent should fit; exact VA/size pair unobserved | 64K at64K |
| 53 | GraphContainsRangeAccess(full range, read) false | successful MapGpuVA/MakeResident+wait precedes U submit, but live G3 publication is not captured | sys_process plus successful BuildPagingBuffer publication before QUERY |
| 54 | LocalView fails | current KMD runtime reserved-local view, actual availability unknown; propagates its NTSTATUS | shim always supplies view |
| 55 | LocalView CPU address NULL | established local reserve mapping expected; actual pointer unknown | local_cpu |
| 56 | no resident logical PTE from graph/table shadow | P is expected to publish live shadow at each page; exact root/middle/leaf state unknown | test builds known root/leaf and shadow |
| 57 | PTE not VALID | P copies validity from VidMm page-table entries | all valid |
| 58 | PTE segment not LOCAL (2) | canonical allocation local-only placement should select2; saved QUERY PTEs absent | logical segment2 |
| 59 | PTE.Allocation != KMD allocation pointer | P stores UPDATEPAGETABLE.hAllocation (D4), compared to pointer resolved through open object (D3/O); namespaces intentionally bridged | x.UpdatePageTable.hAllocation=&allocation, not token71 |
| 60 | PTE allocation offset != page - q.GpuVa | P takes real allocation-relative offset and 4K subpage increment; requires canonical full-allocation base | update starts allocation offset0; whole64K mapping |
| 61 | backing page outside LocalView span | P placement must be within reserved-local range; exact real backing absent | known local pages0x100+i inside64MiB fixture |

There is no separate privileged-caller predicate in this handler. HardwareAccess
requests synchronization; it is not a user-supplied KMD handle conversion switch.
NULL hDevice/hContext is permitted in some generic escape uses, but this UMD call
explicitly requires and supplies both. Assuming they are always NULL here would
contradict the call site. D2's nullable process handle remains a runtime candidate.

Generation ownership: UMD sends zero on QUERY; K returns Graph.ProcessGeneration
and MappingGeneration. Process initialization and mapping publication own these,
not AllocateCb or the staging generation. UMD rejects zero output after K success;
that rejection and later lock/map failures are outside this KMD failure receipt.

## Ranked remaining candidates and replay limitations

1. **Device-specific bridge/process/context ownership (22–38).** Replay constructs
   this OS translation, especially22/37, while real OpenAllocation and handle
   resolution data were not captured. The documented shape is consistent, so a
   token-to-pointer rewrite is unjustified. New tests use a high-bit 32-bit token,
   a distinct pointer, and reject NULL process/context/device and wrong provenance.
2. **Live graph/PTE publication/full allocation extent (23–26,53,56–61).** The real
   path waits for residency, but that is not evidence of this driver's entire
   shadow graph. Replay starts from a known created graph and publishes a known
   full allocation, including both16/64 profiles. It does not reproduce every
   Windows paging split, allocation offset or interleaving.
3. **Busy state (42–44), then OS flags (4).** Replay is idle and sets exactly1;
   real concurrent state/OS-added flags are unknown. Neither follows necessarily
   from the observed trace. A zero output generation or UMD staging lock failure
   after successful QUERY also remains an alternative outside the handler.

Other shim limitations: source extraction exercises real function logic, not
Windows callback ABI dispatch. Shim structures/constants and callbacks are not
proof of OS behavior; pinned-header ARM64 compilation checks the actual types.
Replay local view is64MiB host memory, not the real firmware-backed1GiB reserve.
External registry IO is mocked, while the new receipt writer itself is extracted.
Interlocked operations in the replay are sequential stubs: first-writer arbitration
is reviewed against the real Windows primitive, not concurrency-tested here.
No speculative relaxation is licensed by a passing replay.

## Diagnostic contract and verification

`Wom1G3CopyQueryFailure` is a REG_BINARY of four little-endian ULONGs:
`{version=1, bytes=16, predicate_id, NTSTATUS}`. Adapter RAM retains the same
predicate/status, with atomic claim0→1→2. First claim wins for the adapter instance;
subsequent failures do not overwrite it. Registry IO occurs after G3 lock release
and handle-reference release, at PASSIVE_LEVEL, at most once. A registry failure
can leave only the RAM record; persistence is not guaranteed if registry IO fails.
A future collector must associate it with its exact boot/package and clear stale
registry evidence during ordinary experiment cleanup. No collector/run added here.

Only requests safely identifiable as QUERY are attributed: at least16 bytes of
buffer at PASSIVE_LEVEL must cover Operation. A missing/truncated tag, invalid
magic before COPY dispatch, or an OS rejection before KMD produces no QUERY receipt.
Thus IDs2/3/6 cannot normally generate this receipt and10 requires direct handler
entry. IDs14/21/45–47 concern other operations. These limits are explicit; absence
is not proof of QUERY success. No hardware-success claim is made.

RED: new real-replay assertion failed against the old handler at the first
missing Claim==2. GREEN: actual writer plus predicate mutation tests pass for16/64
profiles, verify first preservation, exact status, no GPU stores on rejection,
PASSIVE registry IO after unlock and reference release, fault-injected pool/local
view/handle-identity/missing-PTE failures, truncated-tag refusal, and no receipt for success
or failed UPLOAD. ARM64 compiles gpuva_g3_windows.c and receipts.c against WDK26100
with /W4 /WX /analyze. Full-suite comparison and hashes are recorded separately in
`investigation/evidence/EXP858-query-audit/summary.json`.

Ownership and source-first boundary: UMD owns canonical/staging allocation tokens;
VidMm owns escape translation and residency publication; KMD owns validation and
this receipt. No new MMIO, interrupts, DMA, power, firmware or recovery behavior.
The previously inspected Asahi/m1n1/Mu reserve/translation contract remains the
one recorded in EXP858-next-boundary.md; no register or hardware-state assumption
was introduced. Smallest future checkpoint is the first failed predicate/status,
with the existing exact-package cleanup and ordinary Code28 recovery. It was not
run, and this audit does not authorize a package or hardware run.
