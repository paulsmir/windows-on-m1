# DWM shared/GDI contract — EXP746, 2026-09-23

Classification: FULL GRAPHICS. Scope: the measured DWM resource descriptors,
not a new feature inventory. Source baseline e88455194bd7798e52b1ab0091ffdb1618f8eff1.

## WINDOWS CONTRACT:

EXP737 resource events from DWM PIDs 5292/8580/8644 contain API format87
(B8G8R8A8_UNORM), BindFlags40 (RT|SRV), MiscFlags512 (GDI_COMPATIBLE)
and2562 (0xA02 = SHARED|GDI_COMPATIBLE|SHARED_NTHANDLE). These are measured
requests, although the successful resource events belong to the WARP fallback.
The request is proven; successful Apple UMD sharing is not.

API and DDI MiscFlags are different namespaces. Pinned WDK26100
`d3d10umddi.h:372` defines SHARED=2, reserves API GDI_COMPATIBLE=0x200,
and uses DDI0x800 for RESTRICTED_CONTENT, NOT SHARED_NTHANDLE.
`D3D10DDIARG_OPENRESOURCE` carries allocation handles and resource/allocation
private data; it does not carry the application's NT handle.

Static inspection of the same runtime image used in EXP737 proves the mapping:
D3D11.dll10.0.26100.9457, image checksum0x4c638f, image identity0x780ae53e,
size0x4c2000. Both APIMiscFlagsToDDIMiscFlags bodies (RVAs0x2fb10/0x11d970)
drop API0x200/0x800 and preserve SHARED: 0xA02 -> 0x2; 0x200 -> 0.
This is static code evidence only. The old dump supplies module/symbol context;
its old exception/registers are NOT evidence about the current run.

Microsoft's NT sharing contract uses NtSecuritySharing, D3DKMTShareObjects,
and D3DKMTOpenResourceFromNtHandle. CreateSharedHandleInternal in this runtime
calls NDXGI::CDevice::ShareObjects, not a new resource-creation UMD DDI.
NT handle/security/lifetime operations belong to runtime/KMT; UMD must implement
the existing shared allocation/open/private-data/resource lifetime contract.
Do not pass API0x800 into UMD or interpret it as protected content.

GetDC/ReleaseDC are DXGI API operations, not UMD callbacks of those names.
Microsoft documents ResolveSharedResourceDXGI for GetDC/ownership transitions:
the UMD must flush pending writes and make resource contents available to the
new owner. ReleaseDC ends GDI access; dirty-region updates and synchronization
must reach the same backing resource before subsequent D3D work.
The inspected runtime has device-bitmap and staging-buffer GDI paths; the latter
contains Map/Unmap/copy dispatches and CreateDCFromMemory/GdiFlush. Which branch
is selected for Apple is not established by WARP resource descriptors.

Pinned WDK IS_DXGI1_1_BASE_FUNCTIONS is based on D3D major and the runtime Version
low word, not a requirement for D3D11. For D3D10 major and Version low0x177a,
0x177a >= (6000|9)=0x1779: the DXGI1_1 table is legal with D3D10_0_x minor6.
Never write its trailing pfnResolveSharedResource into a base-only table.
The current Mesa projection fills only the seven base fields; resolve is an
open implementation gate. D3D10_0_x alone does NOT solve sharing/GDI.

## TRANSLATION:

- API0xA02 -> existing UMD CreateResource MiscFlags SHARED=2; API0x200 ->0.
  Runtime retains NT/GDI metadata and controls KMT handle/security operations.
- Allocate once through the existing Windows allocator; describe dimensions,
  format, layout and ownership in private data. OpenResource reconstructs an
  alias on another device/process; never allocates unrelated replacement pixels.
- DXGI GetDC/shared-owner transition -> negotiated ResolveSharedResource,
  existing flush/fence retirement, plus whichever standard allocation/copy/map
  path runtime selects. ReleaseDC dirty updates must preserve backing identity.
- Existing native producer, batch adapter, physical patch-list KMD and AGX
  submission stay unchanged. No GPUVA migration or second composer.
- Keep D3D10_0_x as the candidate D3D interface: measured API flags do not by
  themselves force D3D11 DDI. Require negotiated DXGI1_1 table coverage and
  executable shared/GDI tests before the first DWM run; do not label them optional
  or move them to POST-HARDWARE. Do not advertise the new DDI yet.

## WHAT IS STILL UNKNOWN:

Exact runtime admission/branch behavior for the selected Apple0_x combination
has not been executed. Before choosing a hardware package, prove table size and
Version negotiation, shared Create/Open across devices, and GetDC/ReleaseDC
content/lifetime behavior through standard runtime or an exact executable gate.
No source inspected here proves an additional D3D11-only UMD requirement for
these two flags. If a standard-runtime gate rejects0_x before those existing
DDIs, that is evidence to revisit the interface version, not to guess new caps.
WARP creation records do not prove Apple callbacks or the actual GetDC branch.

## Explicit cap/recovery decisions (no cap changes in EXP746)

- SupportDirectFlip: retain current KMD bit for now because Microsoft marks
  DirectFlip mandatory for FULL GRAPHICS WDDM1.2+; clearing a required KMD bit is
  not a justified fix for an older UMD table. Current0_x has no CheckDirectFlip
  entry and is NOT evidence of end-to-end DirectFlip support. The candidate gate
  must reconcile this combination; do not silently certify it from the KMD bit.
- SupportKernelModeCommandBuffer: decision is to clear this optional cap in a
  separate tested caps change unless actual aperture coherency is proved and
  correctly described. Missing CacheCoherent plus one successful GDI fence is
  insufficient. Do not set CacheCoherent merely to satisfy a bit check.
- TDR: retain required reset callbacks/ABI, but no claim of recoverable GPU reset.
  Software bookkeeping is not AGX quiescence. Treat timeout as fatal for this
  experimental profile: collect evidence and reboot through documented recovery;
  failed ResetFromTimeout can bugcheck. A software-only success must not be used
  as proof that DMA stopped. Real quiescence/restart needs its own causal change;
  no TDR stress or recovery acceptance is added to this experiment.

## Refusal receipts

`reject-* hr=... pid=... tid=...` records contain hexadecimal UINT fields:

| Record | Field order |
|---|---|
| CreateResource (MiscFlags nonzero only) | format, dimension, usage, bind, map, misc, mips, array, samples, quality, primary, initial, width, height, depth, mip-info-present |
| OpenResource | allocation count, resource private bytes, resource private present, allocation array present, first allocation private bytes, first allocation handle, runtime resource low/high |
| BltDXGI | dst/src subresource, dst left/top/right/bottom, flags, rotation, dst/src/device present |
| ResourceMap | subresource, mode, flags, resource low/high, format, usage, bind |
| ResourceCopyRegion | dst subresource,x,y,z,src subresource,box present,left,top,front,right,bottom,back,dst low/high,src low/high |

A stack-scoped thread-local observer retains the original DDI arguments through
delegated errors (e.g. CopyRegion -> ResourceCopy). One refusal record per call;
no callback replacement or graphics state mutation. Refusals are not suppressed
by the existing128-record success-trace budget. Trace remains opt-in using the
existing experiment-local APPLE_AGX_UMD_TRACE_FILE, with
APPLE_AGX_UMD_REFUSALS_ONLY=1 to suppress success chatter and optional residency
queries. Set both before DWM logon and
verify file accessibility and process inheritance during preregistration.
A fatal runtime error can prevent further calls: this collects all refusals
actually reached, not a promise to enumerate every future unsupported operation.

## Sources and evidence

- Pinned `.local/reference/wdk26100/{d3d10umddi.h,dxgiddi.h,d3dkmthk.h}`.
- `.local/experiments/EXP745-texture-map/etw-research-check.json`.
- `.local/experiments/EXP746-dwm-sharing-contract/runtime-{translation,gdi,gdi-path}.log`.
- Current Mesa projection, UMD OpenResource, KMD lifecycle/memory/scheduler sources.
- [NT share objects](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/nf-d3dkmthk-d3dkmtshareobjects).
- [Open from NT handle](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/nf-d3dkmthk-d3dkmtopenresourcefromnthandle).
- [DXGI1.1 DDI table and resolve](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dxgiddi/ns-dxgiddi-dxgi1_1_ddi_base_functions).
- [GetDC](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgisurface1-getdc).
- [DirectFlip](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/direct-flip-of-video-memory).
- [GDI acceleration](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gdi-hardware-acceleration).

## Diagnostic gate regression

Enabling the old unrestricted trace in the full suite exposed an existing null
optional pfnQueryResidencyCb call in make_resident (cdb stack, execute address0).
That old block also supplied one output status for NumAllocations>1, contrary
to D3DDDICB_QUERYRESIDENCY. Reuse the existing guarded per-allocation diagnostic
helper; no residency or submission policy change. The composer regression
explicitly enables unrestricted tracing with a missing optional callback.
Refusals-only mode avoids intrusive optional residency queries during the DWM
discriminator; HRESULTs and normal error callbacks remain unchanged.

## NEXT acceptance gates (operator refinement, 2026-09-23)

Order stays nonblocking Map -> selected0_x coverage -> package -> Air.
Machine-readable gate definitions: `DWM_NEXT_GATES.json`; none are PASS merely
because their acceptance criteria are recorded.

1. Shared Create/Open: create on device A, serialize allocation private data,
   open on independent device B using only those bytes and runtime allocation
   handles. B cannot see the creator Resource object, addresses, or a creator
   lookup registry. Render on A, complete the same backing's fence, sample on B
   through the actual native producer and retire; verify backing identity and
   both lifetimes. Use EXP737 dimensions1024x1024,64x320,1024x1088,192x192,256x256,
   512x512; format87 and bindRT|SRV. Foreign or truncated private data must fail
   before import/submission, without crash, leaks, or aliasing unrelated memory.
2. BltDXGI: use those source/destination sizes, including unequal pairs to force
   stretch, with BGRA/BGRX UNORM/sRGB combinations. Verify at least bilinear
   stretch and preservation of encoded sRGB values: no sRGB-to-linear decode
   for the DXGI blit. One native pass; no resolve-then-convert in place. Exercise
   the real Blt entry, producer, capture, materializer and retirement.
3. ResolveSharedResource: before writing the extended slot, prove the selected
   runtime and our Interface/Version choose DXGI1_1 and allocate its full table.
   Macro evaluation alone is necessary but not sufficient for that proof. Check
   exact structure sizes/offsets and surrounding sentinel integrity in executable
   ABI tests, plus runtime table-selection evidence. If runtime selects only the
   base table, reconsider interface negotiation before any callback write or DWM
   hardware run; do not force a cast or infer size from the header's availability.

Required standalone preregistration line:
**DWM with DirectFlip=TRUE and no CheckDirectFlipSupport: unknown; observe reject-* / ETW.**
The absence of a DirectFlip callback in the selected UMD must be an explicit
unknown in the experiment, never a surprise or a claimed capability proof.

## Updated first-run scope after implementation-notes source check

Microsoft Supporting the DXGI DDI explicitly requires NO_REDIRECTION for a
driver without a shared D3D9 implementation. Pinned Mesa Device.cpp returns that
status and our projection preserves it. Its documented consequence is bypassing
the shared-resource presentation redirection in favor of PresentDXGI. Therefore
general Blt stretch/conversion is deferred past the first DWM experiment, with
preregistered expectation **reject-blt = 0**. This supersedes the earlier blanket
pre-DWM Blt requirement above; it is an experiment expectation, not proof that
Windows11/DWM can never call Blt. Any rejection reopens the gate. Do not change
NO_REDIRECTION. Explicit DWM shared/GDI resources remain mandatory and distinct
from DXGI's redirected presentation path. Source:
https://learn.microsoft.com/en-us/windows-hardware/drivers/display/supporting-the-dxgi-ddi

The software-probe suggestion is a hypothesis to test on the builder. A stub
that deliberately fails CreateDevice can measure negotiation but cannot prove
which format checks would follow successful device creation. Record that limit
and the builder/Air runtime image versions rather than infer absence of checks.

Map limitation: existing WRITE_DISCARD currently synchronizes and reuses storage;
no asynchronous storage-renaming claim is made. DONOTWAIT is not accepted with
DISCARD/NOOVERWRITE. Nonblocking read/write staging access and resource busy
queries use the existing batch/fence lifetime; no new allocator is introduced.
