# R96: EXP208 template field provenance for AGX4 v2

2026-09-25 offline audit. This is a construction gate, not an EXP810 package.
The EXP208 image is one 16×16 color-fill draw. Its 75 objects and 207 recorded
relocations are not a generic `drm_asahi_cmd_render` serializer. The comparison
requested by R96 requires a G4 producer of that *same* draw, then a byte
comparison after normalizing only relocated GPU VAs and job-owned stamps.

| EXP208 object / field | Source for a native G4 job | Coverage |
| --- | --- | --- |
| Objects 0–8, 11–13, 20–25: context, queues, event control, buffer-manager bookkeeping | KMD context-0 shared memory and queue owner; relocate the recorded object graph | Existing EXP208/B1 path, but per-job reuse and ownership must be checked |
| Objects 9–10, 26–35; barrier 14; microsequences 15/17: stamp addresses, event numbers, previous/current values, queue command count | KMD context-0 event lease, sequence and expected TA/3D stamps; `AppleAgxExp208PatchDynamic` supplies recorded offsets | Existing EXP208/B1 path; no G4 fence owner yet |
| Seven `context_id` fields in objects 15–19 | Leased process VM slot (1–62), never the EXP208 value 63 | Existing `AppleAgxRenderTemplateSelectVmSlot` |
| Object 19 encoder pointer `+0xd0` and TA VDM stream | AGX4 `VdmCtrlStreamBase` | **Gap:** native stream must replace the template encoder payload and every reference to it |
| Objects 36–39: descriptor/sampler area, encoder, scissor, depth-bias streams | AGX4 `SamplerHeap`, `VdmCtrlStreamBase`, `IspScissorBase`, `IspDbiasBase`, with graph-proven process VA | **Gap:** the template has fixed packed objects; the native data is already in UMD BOs and must not be copied from EXP208 |
| Object 40: color output and object 15 fragment attachment | AGX4 fragment attachment pointer and size; KMD validates the mapped writable range | **Gap:** EXP208 has a 16 KiB output. Arbitrary DWM target geometry, format and attachment routing are unproved |
| Object 41 page list, 42 block list, 43–58 block heap | AGX4 v2 `Process[0..2]`; UMD prepares page/block contents from `Process[2].Va` | Addresses and capacities supplied; **gap:** all TA/3D references and page-list counts must be rebound, not merely the three bases |
| Objects 59, 60–63, 64–66, 67–72: aux FB, scratch/user buffer, TPC, tilemap, heap metadata and other tile buffers | Candidate AGX4 v2 `Process[8]`, `[7]/[3]`, `[6]/[4]/[5]`, `[7]` respectively | **Gap:** several object-to-range suboffsets and native initialization contents have no verified mapping; an object name is not proof of an offset |
| Objects 73–74: shader/pipeline and constant input aliases | AGX4 vertex/fragment helpers and BG/EOT/partial USC fields, with UMD-owned process VAs | **Gap:** template shader bytes and alias layout belong to EXP208; AGX4 v2 does not carry an explicit shader-object layout |
| 3D work 18: BG/EOT pipelines, ISP/ZLS, scissor, depth bias, sample/utile/geometry scalars | AGX4 render fields (`Bg`, `Eot`, `PartialBg`, `PartialEot`, `Depth`, `Stencil`, `Ppp*`, `Isp*`, dimensions and samples) | Some corresponding physical-path offsets exist in `AdmissionDynamicOverlayRouteNative`; **gap:** complete AGX4-to-G13 field map, including duplicates and flags, has not been replayed |
| TA work 19: tile/scene pointers, encoder, sample/geometry and TVB state | AGX4 render scalars plus nine process ranges; KMD context-0 work item | **Gap:** complete pointer/suboffset and derived-capacity map is absent |
| Firmware ABI constants and reserved zero fields in work items and microsequences | G13/V13_5 template constants only after a field-by-field comparison with the same draw | **Gap:** unchanged EXP208 bytes cannot be assumed constant for arbitrary native render |

The generated relocation table covers 207 known pointers, including 64 page
list entries and 16 block list entries, but cannot bind an arbitrary process
range by itself: its targets are fixed template objects. A scan of materialized
objects 18, 19 and 63 also yields 0x10–0x14-prefixed 64-bit candidates
outside those 207 relocations. Some are overlapping scalar fields; each must
be classified before a generic builder can use the image. Treating them all
as immutable constants would be a hardware guess.

Current contract: UMD owns GPU-visible BO allocation, mapping, residency and
native command bytes. VidMm owns the process VA. KMD owns context-0 firmware
objects, validated graph access, the process VM lease, job lifecycle and the
Windows fence. m1n1 owns the v5 broker and AGX VM switch. Mu/ACPI state is
unchanged. B1 proves `LEASE → JOB_BEGIN → firmware TA/3D completion → JOB_END
→ RELEASE` for the EXP208 job; it does not prove that a G4 native command
produces those work items or that a WDDM submission fence is retired.

Smallest checkpoint: one real G4 host draw of the EXP208 16×16 color fill,
passed through the actual AGX4 v2 parser, a source-backed constructor and
real-C broker replay. Compare all firmware objects with the EXP208 image after
normalizing only VAs and ctx0 job stamps; require exact graph ownership and
fence retirement. A mismatch fails closed before any Air launch. Recovery for
the eventual hardware run is the ordinary GPU-visible Code28 profile under
R54/R60; preserve the immutable GPU-hidden image for emergency rollback.

Sources inspected: `GPU_CURRENT_STATE.md` G4 checkpoint; REVIEW R95/R96;
`apple_agx_render_template.generated.c`, `_rebase.c`, `_vm_slot.c`;
`apple_agx_gpuva_b1_submission.c`, `_b1_graph.c`; AGX4 v2 parser and UAPI;
the existing EXP208 dynamic patcher, physical native overlay, G4 UMD process
buffer producer, B1 firmware submit path and KMD virtual submit gate. No new
register, IRQ, power or DMA value is proposed.

**Verdict:** R96 is open. The constructor and host replay have explicit
unmapped fields. EXP810 remains gated by the current fail-closed native submit.

## R95 single offline pass at the same boundary

| Check | Source observation | Remaining proof |
| --- | --- | --- |
| 4 KiB / 16 KiB / 64 KiB | G4 Mesa BO creation rounds bytes and alignment to 64 KiB; GPUVA reserve and map round to 64 KiB; KMD `CreateAllocation` advertises local segment preference and rounds the physical extent to 64 KiB. Imported primary keeps its logical byte size while KMD owns the rounded allocation. | Combined native draw with an imported primary through a real VidMm graph. |
| Admission checks | AGX4 parser checks envelope framing, geometry, mapped VA access, process-range sizes and attachment access. The KMD virtual path rejects a valid parsed command with `STATUS_INVALID_PARAMETER` because no constructor/completion owner exists. | Audit each native flag and capability against the pinned WDK before changing the fail-closed return. |
| Residency/order | `AgxWin32GpuvaBind` waits a returned Map paging fence; `AgxWin32GpuvaSubmit` waits MakeResident's paging fence before `SubmitCommandCb`, and holds the referenced BO set to render retirement. | A native KMD job must consume only VA ranges in the published process graph and wait for its actual TA/3D completion. |
| Primary/flip | G4 UMD passes written-primary allocation handles with `SubmitCommandCb`. KMD `SetVidPnSourceAddress` resolves the selected local primary from the Windows allocation/address. | Prove that a DWM render attachment is the selected primary or an explicitly presented copy before claiming a visible frame. |
| Replay | Separate G4 host process-buffer/parser replays and G3 broker/primary replays exist. | One scenario `CreateDevice → resources → Map/MakeResident → native submit → Present` through the actual broker remains open; it depends on the R96 constructor. |

This pass found no source-backed correction that can safely turn on native
execution. The unverified steps above remain gates, rather than inferred
success from the existing individual replays.
