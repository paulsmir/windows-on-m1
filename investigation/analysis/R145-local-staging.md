# R145 — GPU-local canonical storage and CPU staging (offline)

Authority: main-repository `.local/tandem/NEXT_TASK_R145.md`. Base79e5b983;
implementation plan: `docs/superpowers/plans/2026-09-28-r145-local-staging.md`.
Implementation is offline; final verification evidence is recorded below.
No Air connection, package, installation, firmware build or hardware experiment.
The Windows builder is used for saved-dump analysis and offline object/tests only.

## Evidence gate

EXP857 state, UMD log, DWM full dump and decoded receipt match the retained
`hardware-evidence/host-evidence-hashes.json`. Independently decoded state binary
matches receipt SHA25631553ecc22b5552d5632c9725b03b72408f5e92b7995b4d1a100e4fbd6d28165.
First G4 failure remains branch9/ParseUnmapped, Render ordinal11/read1,
VA0x2f0000, graph absent. The logical IPAs are0x9b3995000,0x9b65db000,
0x9b65da000,0x9b65d9000: all segment0, valid/write; neither aligned nor
contiguous as a native16KiB group. R142's producer/parser attribution remains
VDM control stream. The log's only SubmitCommandCb record is line653,
PID5188/TID5360, E_INVALIDARG, DMA280/private480. It does not identify that PID
as DWM or join the failed GPUVA to a specific runtime allocation handle.

G3's status0/branch8/level1 high-table receipt proves initialization at
local0x3e764000, IPA0x91e764000. This boundary did not regress to R144's failure.

The saved DWM1220 dump SHA256
73557d99f65cf549fde6c0b2047b94f3212e4ee6d3fdbd3dd225b46f15e6cfc6 was decoded
with official DXGI symbols on the builder. PC0x7ffbb9b8b314 is
`dxgi!CDXGIFactory::MakeWindowAssociation+0x1f4`, loading from x21+0x10 with
x21=0xafca5e60. Stack is factory FinalRelease during exception unwind from
ddisplay device construction. The low pointer is suspicious but its provenance
is not established. No AGX execution/frame or causal link from the system-backed
VDM failure to this separate CPU crash is proven. No speculative DXGI fix.

Raw evidence, full dump analysis, commands and hash receipts are in main
`.local/experiments/R145-offline/`. Nested m1n1/Mu commits and binary dirty-diff
hashes exactly match R144's tracked source_baseline; those trees are untouched.

## Source-first ownership and differences

Inspected Asahi `mmu.rs::map_node` requires16KiB-aligned IOVA, scatter address,
offset and length. Current m1n1 `hv_agx_gpuva_v5.c` validates native page
translation and retains grant/lease/owner exclusions. Mu MemoryInitPeiLib
reserves the identity1GiB RAM as EfiReservedMemoryType; reconstructed64-bit
ACPI `_CRS` preserves it into Windows. Existing EXP857 R143/R144 full-owner
contract therefore supplies local1000MiB without another firmware experiment.

VidMm owns allocation, residency, paging and VA; UMD owns CPU synchronization,
staging and import ownership; KMD validates current allocation provenance and
copies through its existing full-local CPU view. m1n1 owns grant validation and
retained hardware state; firmware reservation, IRQ, power and recovery remain
unchanged. Private scenes16MiB/backend8MiB are not enlarged or used as BO storage.
External Mesa sources are transformed only in the existing build-local projection
with pinned MIT/BSD notices; reference sources remain unchanged.

## Contracts and translation choices

**WINDOWS CONTRACT:** WDDM2+ uses SupportedWriteSegmentSet for placement;
bit1 means local segment2. CpuVisible identifies direct CPU accessibility.
PreferredSegment is a preference, and EvictionSegmentSet0 permits paged-locked
system eviction storage; it does not prohibit eviction. AccessedPhysically
requires contiguous GPU-memory placement and permits explicit physical access.
See [allocation info](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_allocationinfo)
and [allocation flags](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_allocationinfoflags_wddm2_0).
The WDDM3.0 CPU-visible system-fallback constraint is described in Microsoft's
[GPU upload heap specification](https://microsoft.github.io/DirectX-Specs/d3d/D3D12GPUUploadHeaps.html#support-for-vram-only-lockable-surfaces).

**AGX CONTRACT:** every GPU execution reference must resolve to native16KiB
representable backing with current owner/rights. Scattered4KiB pages are valid
Windows storage but cannot become AGX execution leaves by rounding addresses.

**TRANSLATION:** private allocation version2/type0x100 denotes canonical GPU
storage with CpuVisible0 and the existing local-only placement path. The type
is private metadata, not an invented WDK GDI surface enum. Class/CPU access flags
describe logical BO access via staging. Version1 is retained for the earlier
transport. CPU staging is ordinary unclassed STAGING_CPUVISIBLE/A8/CpuVisible1,
so the existing class0 aperture policy is preserved. AccessedPhysically1 remains
truthful because KMD transfers physically through its local view; normal priority,
alignment64KiB, eviction policy, MMU caps, segment descriptors and page profiles
are unchanged. No unsupported physical CPU lock of a canonical BO is attempted.

**WINDOWS CONTRACT:** UpdatePageTable supplies the KMD allocation handle and
relative allocation offset; NULL handles can describe paging structures. See
[UpdatePageTable](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_buildpagingbuffer_updatepagetable).
DeviceSpecific handle lookup identifies the open allocation, and acquired handle
references prevent destruction of associated KMD objects until release. See
[GetHandleData](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkcb_gethandledata)
and [AcquireHandleData](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkcb_acquirehandledata).

**TRANSLATION:** the buffered copy request carries a runtime allocation handle,
base GPUVA, byte range and process/mapping generations. It contains no user
pointer, PFN or physical address. QUERY validates the whole current allocation;
UPLOAD/DOWNLOAD validate every page in the bounded64KiB chunk before the first
store. KMD matches current ResidentPtes to the acquired allocation and offset,
requires local segment2 and published native reachability, excludes private and
table backing through provenance, checks idle context/process ownership and
copies under the process graph lock. Both16/64KiB Windows PTE profiles use
ResidentPtes, not the deliberately narrower CPU-envelope LogicalPtes. Stale
generation, missing identity, system backing or uncertain lifetime refuses copy.

**WINDOWS CONTRACT:** HardwareAccess escapes receive level-two serialization;
UMD owns CPU-access synchronization in the GpuMmu model. See
[escape flags](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ns-d3dukmdt-_d3dddi_escapeflags)
and [allocation usage tracking](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/allocation-usage-tracking).
[WrittenPrimaries](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/ns-d3dumddi-_d3dddicb_submitcommand)
describes display allocations written by submitted commands.
[LockCb](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_lockcb)
provides a valid CPU range and synchronization; every lock must be paired with
[UnlockCb](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_unlockcb).

**TRANSLATION:** rendering targets nondisplayable canonical allocations; the
borrowed display allocation is updated by a separate synchronous CPU copy after
GPU completion, under Lock/Unlock, before publishing completion or returning to
Present. Merely listing the old handle in WrittenPrimaries and delaying the UMD
fence would expose a scheduler/readback race. Original CPU storage is therefore
not falsely listed as written by those GPU commands. This CPU-publication route
is an implementation inference from the cited contracts, not hardware proof.

**UNKNOWN:** Windows acceptance of the complete local-only canonical allocation
shape on this build; real budget/eviction behavior and performance under DWM;
imported surface CPU-publication behavior; AGX submission/completion and final
display; cause of the saved DXGI crash. Offline replay proves deterministic
ownership/copy/error handling, not VidMm scheduling or hardware acceptance.

## Offline gates and later checkpoint

Allocation intent and real KMD copy replays are RED→GREEN. The copy test covers
both page profiles, partial writes/readback, tail identity failure before stores,
wrong handle/device/rights, stale generations, busy GPU, eviction and re-residency
to different local backing. Shader replay reproduces the saved NULL BO failure,
then verifies allocation/map cleanup and frontend failure propagation. Independent
review found a newly nullable compute caller; its real-function replay was RED,
and creation/info failure now returns safely. Whole native TUs must also compile.

Baseline full suite on unchanged79e5b983:1147 tests,15 failures,38 errors,2 skips,
matching R144. The user explicitly corrected the gate to no NEW failure/error
names against this base, withdrawing the task's two-failure wording. Exact command
and log hash are retained; this comparison does not call the full suite green.

Proposed EXP858 single variable: complete R145 canonical/staging backing contract
relative to EXP857, identical R143 firmware. First prove canonical allocation,
staging content, local PTE provenance and admission of the full submitted set;
then observe the first subsequent parser/execution/completion boundary. Exhaustion
or stale-copy refusal must remain bounded. No frame claim from first VDM admission.
No package or launch is authorized here. Recovery remains immutable EXP377/385
hidden exact cleanup after Code0, then ordinary EXP377/392 Code28.

## Final verification

Stable-tree full suite: 1,154 tests in 143.653 seconds, 15 failures, 38 errors,
2 skips. All 53 failure/error identities and classifications exactly match the
unchanged-base run (1,147 tests); no new or removed failures/errors. The corrected
user acceptance gate passes; the overall suite is not green.

G3 suite: 26 passing tests. Allocation/copy replay: 2 passing tests with both
16/64 page profiles. Shader and neighboring replays: 6 passing tests. Final
full discovery includes the new split-identity primary rotation replay (2 pass).
Windows real-screen and GPUVA contract executables both pass, including odd-tail
multichunk transfer, partial-edit preservation, persistent/imported mappings and
fail-closed wait/readback/unlock behavior. Independent review's rotation finding
was repaired with RED/GREEN and rechecked; no remaining material finding.

Whole ARM64 translation units compile: three KMD and two UMD files under
/W4 /WX /analyze; native agx_state.c, Device.cpp, Shader.cpp and presentation
winsys compile with their pinned build settings. Native builds retain existing
compiler warnings; these are object checks, not a linked or signed package.

Exact commands, object hashes, source manifest, baseline/final log hashes and
saved EXP857 attribution are in `investigation/evidence/R145-offline-staging.json`.
Review and resolution are in `investigation/analysis/R145-review.md`.
No Air access, hardware experiment, firmware change, package build or install.
