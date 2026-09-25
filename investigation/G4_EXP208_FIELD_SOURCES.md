# R96: EXP208 template field provenance for AGX4 v2

2026-09-25 R97 single source pass. The EXP208 image supplies G13/V13_5
firmware layout. Process data ownership below follows Asahi source, not bytes
from the recorded draw. The G4 constructor is bounded by the retained image's
16 TVB descriptors; dynamic buffer-manager expansion is still required.

| EXP208 object / field | Source for a native G4 job | Coverage |
| --- | --- | --- |
| Objects 0–8, 11–13, 20–25: context, queues, event control, buffer-manager bookkeeping | KMD context-0 shared memory and queue owner; relocate the recorded object graph | Existing EXP208/B1 path, but per-job reuse and ownership must be checked |
| Objects 9–10, 26–35; barrier 14; microsequences 15/17: stamp addresses, event numbers, previous/current values, queue command count | KMD context-0 event lease, sequence and expected TA/3D stamps; `AppleAgxExp208PatchDynamic` supplies recorded offsets | Existing EXP208/B1 path; no G4 fence owner yet |
| Seven `context_id` fields in objects 15–19 | Leased process VM slot (1–62), never the EXP208 value 63 | Existing `AppleAgxRenderTemplateSelectVmSlot` |
| Object 19 encoder pointer `+0xd0` and TA VDM stream | AGX4 `VdmCtrlStreamBase` | **Gap:** native stream must replace the template encoder payload and every reference to it |
| Objects 36–39: descriptor/sampler area, encoder, scissor, depth-bias streams | AGX4 `SamplerHeap`, `VdmCtrlStreamBase`, `IspScissorBase`, `IspDbiasBase`, with graph-proven process VA | **Gap:** the template has fixed packed objects; the native data is already in UMD BOs and must not be copied from EXP208 |
| Object 40: color output and object 15 fragment attachment | AGX4 fragment attachment pointer and size; KMD validates the mapped writable range | **Gap:** EXP208 has a 16 KiB output. Arbitrary DWM target geometry, format and attachment routing are unproved |
| Object 41 page list, 42 block list, 43–58 block heap | AGX4 v2 `Process[0..2]`; UMD prepares page/block contents from `Process[2].Va` | Addresses and capacities supplied; **gap:** all TA/3D references and page-list counts must be rebound, not merely the three bases |
| Object 59: auxiliary framebuffer | Asahi `queue/render.rs` allocates an empty private `0x8000` scene buffer; UMD owns zeroed `Process[8]` | Builder binds `Process[8].Va`; UMD clears the whole BO |
| Objects 60–62: preemption scratch 1–3 | Asahi `buffer.rs` allocates one empty contiguous buffer, with offsets determined by adjusted cluster count and t600x sizes `0x540/0x280/0x20`; UMD owns zeroed `Process[7]` | Builder reserves the source-backed nine-cluster upper bound between subranges; actual J313 cluster count still requires a machine receipt |
| Object 63: sampler heap | Asahi `fw/job.rs` `EncoderParams.sampler_array` receives `cmdbuf.sampler_heap`; Mesa owns and fills the descriptor BO. The two work-item relocations land on those sampler-array fields | Builder binds AGX4 `SamplerHeap` or null; UMD does not copy EXP208 sampler data |
| Object 64: tail-pointer cache | Asahi `buffer.rs` allocates an empty scene TPC; UMD owns zeroed `Process[6]` sized from render geometry | Builder binds `Process[6].Va` |
| Object 65: TVB tilemap | Asahi `buffer.rs` allocates an empty scene tilemap; UMD owns zeroed `Process[4]` | Builder binds `Process[4].Va` |
| Object 66: TVB heap metadata | Asahi `buffer.rs` allocates empty scene heap metadata; UMD owns zeroed `Process[5]` | Builder binds `Process[5].Va` |
| Objects 67–71: cluster tilemaps and metadata | Asahi `buffer.rs` allocates these only with vertex clustering; `queue/render.rs` uses optional null pointers when clustering is disabled | UMD sets `NO_VERTEX_CLUSTERING`; builder nulls all five references. Core mask and tiling control still need source-backed patching before runtime admission |
| Object 72: scene user buffer | Asahi `fw/buffer.rs` `Scene.user_buffer`, allocated empty in `buffer.rs`; UMD owns zeroed `Process[3]` | Builder binds `Process[3].Va`; object13 relocation at `+24` points here |
| Objects 73–74: recorded shader aliases | Mesa `agx_batch.c` / `hk_cmd_buffer.c` emit USC pipeline and shader data in process BOs; AGX4 render carries relative BG/EOT/partial USC offsets and helper binaries | Template shader payload is not reused as native program. Parser reconstructs USC VA from the process execution base; builder must replace every program field before runtime admission |
| VDM, scissor and depth-bias streams | Mesa batch encoder and scissor/depth-bias BO writers; Asahi `queue/render.rs` copies `vdm_ctrl_stream_base`, `isp_scissor_base`, `isp_dbias_base` from the render command | KMD validates graph access and binds the process VAs; stream lengths and all indirect references still need proof |
| 3D work 18: BG/EOT pipelines, ISP/ZLS, scissor, depth bias, sample/utile/geometry scalars | AGX4 render fields (`Bg`, `Eot`, `PartialBg`, `PartialEot`, `Depth`, `Stencil`, `Ppp*`, `Isp*`, dimensions and samples) | Some corresponding physical-path offsets exist in `AdmissionDynamicOverlayRouteNative`; **gap:** complete AGX4-to-G13 field map, including duplicates and flags, has not been replayed |
| TA work 19: tile/scene pointers, encoder, sample/geometry and TVB state | AGX4 render scalars plus nine process ranges; KMD context-0 work item | **Gap:** complete pointer/suboffset and derived-capacity map is absent |
| Firmware ABI constants and reserved zero fields in work items and microsequences | G13/V13_5 template constants only after a field-by-field comparison with the same draw | **Gap:** unchanged EXP208 bytes cannot be assumed constant for arbitrary native render |

The generated relocation table covers 207 known pointers, including 64 page
list entries and 16 block list entries. `AppleAgxG4BindProcessObjects` now
binds the source-backed process ranges and `AppleAgxG4ApplySceneRelocations`
nulls unused cluster pointers. Mesa initializes the TVB page/block lists from
the VidMm heap and clears the other process BOs. AGX4 has no format field or
VDM stream length; those omissions remain runtime admission gaps.

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

**Verdict:** R96 is open. Scene binding is coded and replayed for a 1280×720
single-sample profile; the full TA/3D field constructor, real broker replay,
and fence owner remain to be connected. EXP810 remains gated by fail-closed
native submit.

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
