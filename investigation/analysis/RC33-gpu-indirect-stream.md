# RC33: GPU-generated pure indirect draws

Offline implementation on `codex/rc33`, based on
`44ab3aa30985140fcd43839c3b9386cf1a8b7894`. No hardware, guest, network,
builder, driver package, firmware, capability, or recovery operation occurred.
Integration owns EXPERIMENTS.md and CHANGES.csv under the handoff rules.

## Boundary and primary evidence

The owner count read removed by RC32 is absent, but pinned Mesa
`src/gallium/drivers/asahi/agx_state.c:4388-4402` recursively calls draw_vbo
once per MaxCount record. Seven calls with MaxCount 2400/12000 therefore
encode 16800/84000 native draws per frame, including GPU-predicated zero draws.

Saved EXP1963 inputs are in shared `.local/experiments/EXP1963-counted-indirect/`.
`gm1963a-umd.log:7988` and `gm1963b-umd.log:7692` record a **one-argument,
DRAW_INDEXED, 20-byte-stride** command signature; following lines 7990/7694
record kind 2, count-buffer present, MaxCount 2400/12000. There are 5480/5578
such ExecuteIndirect receipts. Both runs also create a one-argument DISPATCH
signature (16-byte stride) at lines 7782/7486. Those dispatches are outside
this change. A compact exact extraction is in the RC33 handoff's
`signature-evidence.txt`.

WDK26100 `d3d12umddi.h:2455-2474` supplies stride, argument descriptors,
MaxCommandCount, and argument/count placements. Driver
`d3d12/agx_d3d12_ddi.cpp:5885-5917` validates signature kinds; `:5954-5985`
routes signatures with multiple arguments through C31 state expansion.
Only the pure draw record reaches the owner indirect draw. Root/VB/IB changes
within a signature are neither admitted to nor decoded by the new kernel.

Unlike the task's general DGC examples, this pinned Honeykrisp revision also
unrolls indirect draws on the CPU (`src/asahi/vulkan/hk_cmd_draw.c:3799-3851`).
It nevertheless supplies the native GPU-written VDM precedent: robust-index
draws allocate command space, generate it with a kernel, and request
AGX_BARRIER_ALL | AGX_PREGFX (`:3615-3629`). The new implementation uses its
native packing helpers, not an assumed complete DGC implementation.

## Draw parameters and the stream format

Native `agx_state.c:4420-4451` points AGX_SYSVAL_TABLE_PARAMS at the last two
words of an indirect record: offset 12 for indexed, 8 for non-indexed. They
are base vertex/first vertex and first instance. `agx_nir_lower_sysvals.c:221-231`
loads those values; `:405-411` reserves four halfwords at offset zero in
the vertex ABI. Honeykrisp likewise repoints draw_params per draw
(`hk_cmd_draw.c:2931-2953`, `hk_cmd_buffer.c:711-714`).

One unchanged USC would keep reading the first record. Mutating shared uniform
memory per draw would race overlapping vertex execution. RC33 instead captures
the **actual native USC template**, including texture/sampler/shared/shader/
preshader and other uniform commands, and its PARAMS command offset while
`agx_build_pipeline` emits it. The GPU clones that small template for each
executed record, replaces only its four-halfword PARAMS load address, and
selects the resulting USC immediately before the native indirect INDEX_LIST.
Shaders not using base parameters need no PARAMS patch. Draw-ID shaders stay
on the old path; the uncompiled shader records that property after the ordinary
NIR gather, before sysval lowering (`native-asahi-indirect-stream.py:115-121`).

Native hardware definitions are `src/asahi/libagx/libagx_dgc.h:273-356`
(draw/size), `:419-426` (return), `:496-506` (call), and
`src/asahi/genxml/cmdbuf.xml:757-804,910-925`. Each generated slot contains:

| Command | Non-indexed bytes | Indexed bytes |
|---|---:|---:|
| VDM_STATE, vertex shader word 1 present | 4 | 4 |
| Vertex shader word 1, USC offset | 4 | 4 |
| Native indirect INDEX_LIST and address | 12 | 20 |
| Total | 20 | 28 |

The kernel clamps count to MaxCount, or uses MaxCount when no count buffer is
present. Threads below that count write slots; the thread equal to count writes
one four-byte return. Later threads do nothing. Launching MaxCount+1 threads
also handles count zero without a CPU read. No unexecuted argument record is
read or interpreted. Index size, buffer range, restart, and topology use
`agx_draw_indexed_indirect`/`agx_vdm_draw` exactly as native emission does.

One CPU `agx_vdm_call` enters the stream; it contains no nested calls. This
uses only one return level. The reference exposes encoders for call/return but
does **not** specify a maximum G13 return-stack depth or prove this application
of it. No deeper nesting is assumed. Actual G13 execution remains a hardware
checkpoint. Command storage starts at a 64-byte boundary; all words and
argument offsets are four-byte aligned. USC copies use a 64-byte stride and
must fit the 32-bit USC window relative to shader_base
(`agx_device.h:228-233`). Slot extents, pool rounding and USC products are
checked by division/subtraction before allocation, including WOW64.

Implementation: `agx_win32_indirect_stream.cl:7-50`, shared ABI/header `:10-24`,
emitter `agx_win32_indirect_stream_emit.h:91-177`, projection
`native-asahi-indirect-stream.py:94-189`. A later ordinary draw dirties VS
state, restoring the native pipeline after the GPU last selected a clone.

## Ordering, storage, permissions and failure

UMD/Asahi own encoding, hazards and transient lifetime. Existing KMD/VidMm
own residency, submission and completion. m1n1/Mu initialization, interrupts,
DMA translation, power and recovery contracts do not change. Live machine
inspection is prohibited by this offline assignment; EXP1963 supplies the
launch comparison. No alternative firmware or hypervisor contract is inferred.

The wrapper flushes prior count/argument writers **before** entering graphics
or binding application heaps. That matters because the graphics batch's CDM
executes before all its render work; a preceding render write must belong to
an earlier submission. Native `agx_batch.c:449-467,519-532` supplies these
nonblocking hazard flush/read-reference semantics. Capacity is checked before
entry so the internal launch cannot auto-submit an entered draw when CDM fills.

The kernel uses the existing D3D-G1 graphics-batch CDM contract:

- Native `agx_state.c:2998-3049` dispatches precompiled programs and places a
  full CDM barrier after them, including USC-cache invalidation.
- `agx_win32_gpuva_batch.c:821-848` publishes CPU-produced templates, arguments
  and ordinary encoders before the submission.
- `:961-972` places COMPUTE before RENDER in one v5 envelope; COMPUTE waits for
  earlier work, RENDER waits for this COMPUTE. AGX_PREGFX alone would not supply
  Gallium ordering because agx_launch_precomp ignores that argument.

Two **output-only batch pools** hold VDM slots and low-VA USC copies. They use
GPU_ONLY General allocations, with GPURead|GPUWrite and no CPU access. LOW_VA
is an independent address-placement constraint, not an Encoder-class request
for these allocations (`agx_win32_asahi_bo.c:405-422`). Ordinary Encoder and
Shader permissions remain unchanged. This distinction is required: the KMD
advertises only CpuWrite|GpuRead for those classes (`lifecycle.c:726-735`), and
`umd_gpuva_windows.c:1001` derives canonical write protection from access flags.

The projected pool allocator skips CPU mapping and returns a valid GPU pointer
with cpu=NULL for GPU-only output. Each output BO is explicitly added to the
batch BO list for residency/submission. Its pool retains it through ordinary
retirement/cleanup, including previous slabs after rollover. Output pools are
absent from publish_shadows; no CPU initialization or shadow copy scales with
their output size. Reused contents need no clearing: every consumed slot and
the terminal return is written before render. Existing cache keys include flags
and class, and in-flight BOs cannot be reused.

All failures stop before adding the VDM call and fault through the existing
context mechanism. Pool ownership covers partial allocations. Compute entry
is unwound on launch failure; wrapper scope and callbacks are restored.
Supported-path allocation/dispatch errors do not silently switch to a partial
draw. Query objects, tessellation/patches, GS/XFB, passthrough GS, draw-ID and
non-G13 paths use their existing RC32/RC31/native fallback with reason codes.

## Build and license review

`generate-indirect-stream.py` uses the already installed local Mesa mesa_clc
and asahi_clc to compile the original kernel for g13g and g13x. The small
generated bundle is checked in; Windows requires no OpenCL toolchain.
The normal state projection appends its ABI/program ID, and the runtime closure
appends its binaries to both native chip tables after copying the original
library. Existing program IDs and binaries are retained. Source/output/tool
SHA-256 values are in `indirect-stream-generated/manifest.json`; source changes
without regenerated output are rejected by the projection.

License/compatibility review: repository LICENSE is MIT. The included primary
libagx_dgc.h and generated GenXML encoders are MIT; their source stays in the
reference tree. New integration/kernel source is original MIT code. Compiler
output retains the Asahi notice, with third-party MIT notices retained beside
the bundle. No external C function body is copied into repository source.
The host test includes native encoders directly and retains their notices.

## Receipts and expected performance

`measure-d3d12-counted-path` now emits five words at owner `:3924-3933`:
`path, MaxCount, total_draw_vbo_us, native_encode_us, fallback_reason`.
The first three keep their previous order. Path 3 is a successfully emitted GPU
stream; 1 is RC32 predication, 2 is CPU counted fallback, 0 is ordinary uncounted
fallback/no emitted counted path. The getter reports the actual native path,
not an inference from topology. Native encode time covers native state emission
and stream preparation; total includes hazard/binding work as before. Both are
CPU wall times and can include allocation/residency waits, never GPU duration.

Fallback reason: 0 eligible; 1 tess/patch; 2 GS/XFB/restart lowering;
3 query/predication; 4 draw-ID/user-index/shape; 5 passthrough GS;
6 non-G13. Existing perf-reason fallback text is supplemental.

The CPU now emits **7 native draw states + 7 calls**, instead of RC32's
16800/84000 draw states. Expected avoided encoding is roughly
`(16800-7)*record_encode_us` / `(84000-7)*record_encode_us`, minus seven fixed
generator preparations. RC32/EXP1964 measurements can supply record_encode_us.
Relative to RC31/EXP1963, the count read also remains absent. The measured
87.334/99.332 ms containing intervals are an optimistic removable-work ceiling,
**not a predicted saving**: they include useful work and other waits.

Command encoding, output initialization and publication have no per-record CPU
loop or copy. Allocation/residency and the existing full-allocation backing
validation can still scale with bytes or page count. Those checks remain intact;
this is not an asymptotic guarantee for every operation beneath ExecuteIndirect.
GPU work and reserved scratch are bounded by MaxCount. Indexed VDM storage is
67204/336004 bytes per call; USC storage is MaxCount*align(template_bytes,64).
Only executed records are populated. Net score/FPS cannot be predicted offline.

## Verification and next hardware checkpoint

Regression evidence is in the RC33 handoff. Tests run the real kernel with the
native pack helpers under ASan/UBSan; cover indexed/non-indexed, zero, one,
clamped/UINT_MAX and no count, padded strides/offsets, 12000 records, ordinary
and high uniforms, no PARAMS, and untouched guards. Production helper replay
covers fallbacks, source hazards, capacity, bounds, allocation/reference/
entry/launch/leave failures. Operation counts are identical at 1 and 12000.
The pool replay verifies cpu=NULL, no map, valid GPU pointer and allocation
failure. Full projected C translation units and the merged shader tables compile
on the host using only the established platform declaration shims.

Independent review found the original read-only USC and linear shadow-copy
defects; both were fixed with explicit GPU-only output pools and RED/GREEN
permission coverage. Re-review found no remaining blocking defect. This does
not replace Windows ARM64/x64/x86 compilation or actual GPU execution.

Smallest checkpoint (integration only): this HEAD, unchanged EXP1963 launch
(expanded RAM, m1n1 075e3980/macho 452ab29f, full 2688 MiB pool,
Wom1GpuFaultInfoRead=1, same GPU_WAIT/capture settings), exact package hashes,
preregistration and established recovery. First C14 pure counted and uncounted
draws, 16/32-bit indices, zero/one/clamped counts, varying first vertex/base
vertex/first instance, and a following ordinary draw. Verify pixels and
query/GS/patch fallbacks before GravityMark.

WHY THIS HYPOTHESIS: EXP1963 proves seven pure indexed counted calls dominate
the draw envelope; RC32 source expands MaxCount CPU draws; the new tested native
encoding removes that expansion while preserving per-record uniforms and queue
ordering. Then run GravityMark 2k and 10k with process exit between arms.
Compare Score/FPS and median frame against **784/4.7, 182.982 ms** and
**798/4.8, 184.688 ms**. Expect seven path-3/reason-0 rows per representative
frame. Compare per-call native/total time, frame/replay tail, submissions, paging,
trims, output and DWM/device health. Reject corruption, removal or regression;
collect evidence before the established exact-package rollback.

WHAT IS STILL UNKNOWN: Windows native ABI/link gates; hardware VDM call/return
and command-fetch/USC coherence; rendered per-record values and state restoration;
actual USC sizes, active counts, kernel cost, paging impact and total performance.
No hardware validation or release/stability claim is made.
