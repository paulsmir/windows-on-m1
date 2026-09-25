# R96: EXP208 template field provenance for AGX4 v2

2026-09-25 R97 single source pass. The EXP208 image supplies G13/V13_5
firmware layout. Process data ownership below follows Asahi source, not bytes
from the recorded draw. The G4 constructor seeds 32 queue-lifetime TVB
blocks, covering the 2560×1600 primary (80×50 tiles → 32 blocks).

| EXP208 object / field | Source for a native G4 job | Coverage |
| --- | --- | --- |
| Objects 0–8, 11–13, 20–25: context, queues, event control, buffer-manager bookkeeping | KMD context-0 shared memory and queue owner; relocate the recorded object graph | Existing EXP208/B1 path, but per-job reuse and ownership must be checked |
| Objects 9–10, 26–35; barrier 14; microsequences 15/17: stamp addresses, event numbers, previous/current values, queue command count | KMD context-0 event lease, sequence and expected TA/3D stamps; `AppleAgxExp208PatchDynamic` supplies recorded offsets | Existing EXP208/B1 path; no G4 fence owner yet |
| Seven `context_id` fields in objects 15–19 | Leased process VM slot (1–62), never the EXP208 value 63 | Existing `AppleAgxRenderTemplateSelectVmSlot` |
| Object 19 encoder pointer `+0xd0` and TA VDM stream | AGX4 `VdmCtrlStreamBase` | Builder binds the native VDM VA; indirect stream references still need validation |
| Objects 36–39: descriptor/sampler area, encoder, scissor, depth-bias streams | AGX4 `SamplerHeap`, `VdmCtrlStreamBase`, `IspScissorBase`, `IspDbiasBase`, with graph-proven process VA | **Gap:** the template has fixed packed objects; the native data is already in UMD BOs and must not be copied from EXP208 |
| Object 40: color output and object 15 fragment attachment | AGX4 BGRA8 single-sample pointer and size; KMD resolves contiguous writable local memory | 2560×1600 host replay passes; other formats and samples reject |
| Object 41 page list, 42 block list, 43–58 block heap | AGX4 v2 `Process[0..2]`; UMD prepares 32 page/block entries and retains these BOs for InitBM queue lifetime | Builder patches `Info.page_list_size/page_count/max_blocks/block_count/last_page/max_pages`, `InitBuffer.block_count`, `BlockControl.total/wptr`. Recorded relocations name the first 16 blocks; UMD list names all 32. |
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
the VidMm heap and clears the other process BOs. AGX4 v2 names BGRA8 in its
private header; VDM stream length and indirect references remain gaps.

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

**Verdict:** R96 constructor and KMD/B1 submit path are wired for a 2560×1600
single-sample BGRA8 profile. The real-C broker replay covers the mapped VA
and lease/job lifecycle. Native Mesa stream contents, every indirect stream
reference, and a full WDDM Present/retire replay remain unproved; EXP810 is
still gated by those checks.

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

## EXP810-R95-VDM-OFFLINE — one bounded discriminator, 2026-09-25

The existing 2560×1600 BGRA8 replay computes 80×50=4000 tiles and
`round_up(ceil(4000/128),8)=32` TVB blocks: 128 page entries, 64 block-list
entries, and a 4 MiB heap. The builder patches the 32-block counts and the
production UMD initializer writes all 128/64 entries. Both affected host
replays pass. This resolves R98's 16-descriptor capacity objection for this
geometry, but those tests still construct a synthetic render packet.

| Boundary | Deterministic observation |
| --- | --- |
| WINDOWS | `AgxWin32GpuvaSubmit` waits the MakeResident paging fence and retains the BO handles through completion. G4 UMD includes `batch->vdm.bo`, process BOs, the output and batch BO list. No combined CreateDevice→Present/retire replay or DWM UMD receipt exists for package813. |
| AGX | Mesa allocates an initial 0x80000-byte encoder BO and may emit `VDM_STREAM_LINK` to a 0x10000-byte continuation when space runs low. Its decoder follows links and parses PPP state, shader pipelines, index/indirect buffers and stream termination. AGX4 v2 carries `VdmCtrlStreamBase` but no stream byte count; the host packet has no actual VDM bytes. |
| TRANSLATION | The KMD graph callback validates only `(VdmCtrlStreamBase, 1, read)`; the builder binds that VA. The synthetic broker replay validates known firmware relocations and mapped BO ranges, but cannot enumerate a real stream's link targets or indirect references. Residency of a BO is not proof that each decoded reference falls inside its published grant. |
| WHAT IS STILL UNKNOWN | The exact end of a real 2560×1600 textured-quad VDM stream, its continuation/link targets, and every indirect byte range versus a grant are deterministic gaps. DWM's actual resource/primary/Present sequence is also unobserved. Firmware execution and visible output remain hardware unknowns only after these input checks pass. |

Verdict: **R95 incomplete, EXP810 hardware not launched.** The next smallest
offline proof is one actual Mesa draw (or a captured native draw from the exact
UMD), decode its VDM chain to terminate, validate every indirect byte range
against the published VA graph, then replay the same packet through the KMD
builder, real C broker and Present/fence retire path. Package813 remains
build-only until that proof or a narrower source-backed hardware hypothesis.

## R95 hardware-gate correction — 2026-09-25

Claude's R95 decision accepts EXP810 with exact package813 before an offline
decode of every VDM indirect reference. Microsoft's [GpuMmu model](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpummu-model)
assigns process VA mapping to VidMm/UMD and treats an invalid GPU VA access as a
GPU fault; it does not require the KMD to parse a UMD command stream. Asahi
`drivers/gpu/drm/asahi/gpu.rs` `handle_fault` reads fault info, marks pending
events and invokes recovery. The real-C broker replay establishes that its
model creates PTEs only for registered mappings, not that the exact Mesa draw
was decoded. Therefore the exact VDM bytes and firmware response are measured
in EXP810, with post-run offline stream/grant analysis. This correction does
not reinterpret the earlier synthetic replay as an actual draw.
