# AD04 native batch integration — source contract

2026-09-13; integration/ad04-windows-compiler. Offline implementation, no hardware
result or desktop acceptance. The physical Render/Patch/Submit architecture is
unchanged. This extends the existing native producer, capture, composer and KMD
materializer; there is no alternate BO allocator or renderer.

## Inspected sources and ownership

Pinned Mesa root: `/Users/pavel/public_windows/.local/reference/mesa`.

- `agx_batch.c:80-164,206-302,975-1053`: encoder allocation, stable context batch
  slots, pools, active/submitted/complete state, cleanup and synchronization.
- `agx_state.c:937-1025,2678-3028,3053-3242,3273-3420,3423-3709,4860-5255`:
  initial/viewport/dirty PPP, descriptors, native VS/FS USC, BG/partial/EOT,
  shader update, exact draw and pointer emission.
- `agx_uniforms.c:26-150`, SHA256
  `cf480b39d1fa323093673990c7127097e741247fe918e3ee295099512a0cd2ea`;
  `agx_state.h:76-201`; `agx_nir_lower_sysvals.c:22-130`: full root/stage layouts,
  direct push ranges and indirect table pointers, VBO addresses and SSBO sinks.
- `agx_pipe.c:1222-1420,1436-1633,1758-1868`: native render roots/scalars,
  termination, final scissor/depth-bias upload, flush and context creation.
- `cmdbuf.xml:224-324,406-419`; `agx_tilebuffer.h:25-67`: texture/PBE bitfields,
  native COUNTS (four bytes), tile dimensions and sample state.
- Repository existing `agx_win32_asahi_{bo,capture,pipeline}`,
  `agx_win32_reloc_capture`, `agx_win32_transport`, `apple_agx_win32_abi`,
  `umd_asahi_batch_adapter`, `umd_draw_composer`, `render_win32_transport`,
  `apple_agx_dynamic_job`, `render_dynamic_dma`, `render_dynamic_overlay`.

Classification: FULL GRAPHICS. Microsoft's [Render callback contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_rendercb)
submits the command and allocation list through the selected context; the
[SubmitCommand contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_submitcommandcb)
retains RenderCb for legacy patch contexts. The existing runtime-thread composer
continues to own that callback and its ordered completion marker. No worker
callback model or WDDM capability changes are introduced.

Native Mesa owns command/state construction and shader compilation; Windows
ScreenBuffers own allocation identity. The capture holds native BO references;
the composer owns Windows submission holds. KMD validation, immutable copying,
placement and patching remain request-scoped. Existing KMD owns interrupts,
hardware DMA/completion and recovery. m1n1/Mu/ACPI/power interfaces are not changed
or implicated by this offline lifecycle boundary; no machine-state assumptions
or new hardware observations are made. Existing GPU_CURRENT_STATE and referenced
final-capture/root results supply the retained launch constraints.

## Supported first actual native producer

One direct nonindexed triangle-list draw, exactly three vertices, first vertex
and instance zero, one instance, one uncompressed BGRA8 2D color attachment,
single mip/layer/sample; no depth/stencil or depth bias. Use real screen/context,
resource, shader/NIR compilation, state binding, clear, draw_vbo and flush calls.
No GS/tessellation/compute/indirect/queries/streamout/application textures,
samplers, UBOs or SSBOs; no scratch, spill or encoder rollover. Native VBO and
root/stage uniform construction are retained. Native internal sampler and zero
SSBO sink initialization is retained and captured. These are tested admission
limits, not advertised Full Graphics feature support.

## Why v3 cannot represent the source

The native flush has three additional USC roots (BG, partial BG, EOT), including
packed resource counts and USC flags. Partial EOT uses the same EOT tuple. They
are not linked from VDM/PPP and cannot be invented as encoder pointer fields.
The v3 payload has no fields for these roots. Its mandatory depth-bias root is
also false for a native draw with zero depth-bias bytes. Constant push slices
currently have no outgoing-pointer policy and are not immutable copied sources.
Texture/PBE address fields are masked packed words, not generic pointer64.

The graph includes one encoder, three PPP updates, five USC programs, final
scissor, attachment texture/PBE, internal sampler, VBO, compiled shader sources
and full uniform blocks. The old sixteen references cannot be treated as a
complete-graph budget. Exact counts depend on the real compiled shader and must
be recorded by the integrated execution. The legacy DMA overlay additionally
limits ten objects and relocation kinds through seven; it must remain closed
until independently extended to consume the complete graph.

## Versioned native batch wire mapping

Keep v1-v3 shapes and limits (16 references, 64 relocations, 4096 command bytes).
Add native batch v4 metadata after the unchanged draw prefix in the existing
command path. Bounded v4 storage: 64 references, 128 relocations, 8192 bytes;
capacity overflow fails before submission. Three pipeline tuples carry
`UscReference`, exact native `PackedCounts` U32 and `UscFlags`. Native scalars
carry samples, layers, sample bytes, utile dimensions, PPP control, PPP
multisample control and the supported empty-tile processing flag. No Linux
submission pointer structures cross this boundary.

Graph roots are encoder, final scissor, optional actual depth bias, destination
and the three BG tuples. Native VS/FS USC, shaders, PPP and descriptors follow
typed edges. Optional legacy convenience fields are not synthetic roots.

Uniform role14 is an exact General/Read source copied by the materializer.
USC uniform instructions resolve an interior subrange of that source rather
than creating overlapping slice references. UniformAddress64 patches native
root/stage pointer fields to Uniform, Vertex, Constant or Descriptor sources.
Root self/stage pointers are allowed graph cycles, not another source registry.
v4 Constant sources such as the native zero sink and clear/stipple data are
copied too. VBO source intervals retain native offsets and clamps.

Descriptor address kinds both patch eight bytes at descriptor offset eight:
texture masks bits 2..37 and stores `(address >> 4) << 2`; PBE masks bits 0..35
and stores `address >> 4`. Both require 16-byte aligned addresses below 2^40 and
preserve every other field. The attachment is one RW RenderTarget identity,
since partial BG reads what EOT writes. Compression and array extensions remain
unsupported. No format/stride bits are replaced by a generic pointer write.

## Stable lifecycle and callback integration

1. Batch slot/capsule allocation owns capture, encoder root, metadata and adapter
   transaction storage. Begin v4 at initial encoder creation before initial PPP.
2. Enter the persistent root around actual initial state, draw and flush calls;
   leave at native returns while the capsule and BOs remain stable. Borrowed
   state emission scopes never replace or shorten the root.
3. At real upload/emission sites capture initial/viewport PPP, root/stage
   uniforms and pointers, shader ranges, BG/partial/EOT and their texture/PBE
   attachment descriptors. No address scanning or inferred neighbouring spans.
4. Native flush writes its exact 69-byte stop sequence without advancing the
   native current pointer. Final end is `current + sizeof(stop)`. Capture final
   native scissor upload; absent depth bias remains absent. Finish all child
   scopes, detach and finalize the one encoder root once.
5. Only a complete successful capture enters the existing adapter Seal/Dispatch
   once. Render entry makes replay and abort illegal even if the completion
   marker initially fails. Adapter Retire alone retries that marker.
6. Timeout preserves capsule, framebuffer, pools, source BOs and Windows holds.
   Successful ordered retirement releases both hold sets, then permits native
   batch cleanup and slot reuse. Context destruction cannot free in-flight
   storage. Linux syncobj/virtio/ioctl behavior is replaced by these callbacks,
   not emulated as successful stubs.

## Verification and next checkpoint

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A real native source pointer
must never survive immutable placement; native batch storage must not be freed
on timeout; legacy payloads must not acquire v4 admission. The integrated test
must invoke actual native init/state/draw/flush, run the existing adapter and KMD
validator/materializer at two placements, then prove ordered retirement and
balanced owner holds. Source snapshots must remain unchanged and every graph
root must identify the actual native producer output. Record actual reference,
relocation, object and source-byte census and fail closed on overflow.

Use real pinned compiler/NIR libraries to close symbols; no success stubs for
native draw/resource/compiler semantics. x64 must build/link/execute; ARM64 must
build/link. A source-anchor pass alone proves only projection applicability.

The eventual smallest hardware discriminator is one complete Windows-native
request reaching physical AGX completion and the existing Windows fence, with
output evidence. It requires all playbook hashes/package/health/preregistration
gates and ordinary GPU-visible exact-package recovery. This source mapping has
no hardware uncertainty and does not authorize a hardware experiment.

Native flag translation correction: pinned Mesa `asahi_drm.h:781` defines
`DRM_ASAHI_RENDER_PROCESS_EMPTY_TILES` as bit 1 (value 2). The Windows native
metadata uses its named private flag
`APPLE_AGX_WIN32_NATIVE_RENDER_PROCESS_EMPTY_TILES`, bit 0 (value 1).
`AgxWin32AsahiBatchFinish` rejects every unsupported native DRM flag and maps
the supported bit explicitly. The private ABI validator rejects all other
private bits; consumer code and tests use the named constant. Copying the DRM
flags word would falsely reject the real clear/draw batch. The actual producer
clear/flush/consumer test exercises this translation; no helper renderer was
added. Cleanup phase 2 now checks consumer retirement counts only after both
consumer gates succeeded, so an earlier producer rejection does not invent a
second retirement failure; the final success marker still requires both.

## Existing KMD consumer extension (2026-09-13, offline)

Inspected the actual `render_dynamic_windows.c` Read/Resolve/Build chain,
`render_dynamic_overlay.c` placement/copy/restore path, `render_dynamic_dma.c`
private serialization/Patch path, `gdi_windows.c` Patch shadow sealing,
`submission_windows.c` output binding and `backend_platform_windows.c`
StageJob/BuildActiveJob/retirement. Also inspected `memory_runtime_windows.c`
fixed input mappings and generated template object/relocation tables.

Hardware-side primary sources are current Asahi Linux
`.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi/queue/render.rs`
(utile/sample/tib equations and fragment/vertex job field assignments),
`fw/{fragment,vertex}.rs`; m1n1 commit
`c6d10e04afdad5314e8ac1e67bc3919b094ab000`,
`proxyclient/m1n1/{agx/render.py,fw/agx/microsequence.py,fw/agx/cmdqueue.py}`
and `src/hv_agx_retained_root.{c,h}`; the shared retained-root ABI and context0
broker. Referenced proof boundary remains EXP680–682 as summarized in
`AD04-PRE-COMPOSER-STATE.md` and `EXP682_STANDARD_PRESENT_BOUNDARY.md`.
No ledger archaeology, Air operation, m1n1/Mu edit or new UAT mapping occurred.

The retained firmware root requires its authenticated epoch/root and exact
16KiB ownership records, with the preserved private prefix unchanged. Native
payloads continue to use existing context63 local-memory mappings; firmware
queue objects continue through the existing retained context0 command/shared
arenas. This consumer extension does not change either mapping owner.

Selected smallest private-record change: bump internal dynamic DMA 3 to 4,
reject old private records, retain existing source-command admission (legacy v1
accepted, v2/v3 consumer rejection unchanged, native v4 newly represented).
No primary source requires persistent compatibility for the old private DMA
record between exact driver packages. Microsoft's
[DXGK_CONTEXTINFO](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_contextinfo)
allows the driver to request its DMA size and nonpaged private-data capacity.
DMA capacity is 0x50000; the coordinated driver private shadow is 0x51000.
Native materializer storage stays bounded at its existing 0x40000 maximum.

Measured before adding TargetOffset: dynamic job 7208 bytes, snapshot 10808,
DMA header 120, object 32, relocation 40. Thus the old 4096-byte DMA bound was
already impossible after increasing the in-memory graph arrays. TargetOffset
and native binding metadata enlarge the private record further. The Render
snapshot and expanded build plan must be request-owned nonpaged scratch, not
large kernel-stack locals. The backend retains its existing persistent plan.

Native placement uses only already owned zero windows from the old overlay:

| Sources | Existing object and window | GPU address source |
| --- | --- | --- |
| Shader, shader rodata, USC, CF descriptor targets | 73, offset 0x20000, capacity 0x10000 | Original 0x1100020000 mapping plus offset |
| Uniform, Constant, VBO, PPP, texture/PBE/sampler descriptors | 36, offset 0x8000, capacity 0x8000 | Existing object's mapped GPU address plus offset |
| Encoder | 71, offset 0, capacity 0x180 | Existing mapped object |
| Scissor | 38, offset 0, capacity 0x4000 | Existing mapped object |
| Optional real depth bias | 39, offset 0, capacity 0x4000 | Existing mapped object |

Each variable object starts at a 256-byte boundary but copies its exact source
length. CF's need for a shader-relative address is derived from its typed
incoming relocation. No second allocator, arbitrary addresses, enlarged
mapping window or overwrite of occupied template data is introduced. Capacity
and zero-content checks fail before application. Native USC stays contiguous;
the legacy 64-byte/0x1000 pipeline splitting rule applies only to legacy plans.
Render and worker reconstruct the identical placement in reference order.

`AdmissionDynamicOverlayRouteNative` consumes the actual root references and
COUNTS after `BuildActiveJob`. For pinned G13/V13_5 object18: BG count/address
0x88/0x90; EOT 0x3c8/0x3cc; duplicated partial BG 0x610/0x618 and 0x640/0x648;
duplicated partial EOT 0x70c/0x714 and 0x72c/0x734. Scissor is routed at 0xa0
and 0x4c8, absent depth bias becomes zero at 0xa8 and 0x4b8. These offsets were
derived from the pinned m1n1 structures and cross-checked against existing
template relocation entries. The actual USC execution base is object18+0x1c0
and object19+0x120. The old receipt's “PipelineBaseRaw” at +0x170 is a different
field (mtile stride), so it must not be reused as an execution-base interface.

First hardware-consumer geometry remains the proven single 16x16 tile. Current
Asahi equations drive utile configuration and tib blocks from native metadata:
`utile=((width/16)<<12)|((height/16)<<14)|log2(samples)` and
`blocks=ceil(sampleBytes*utileWidth*utileHeight*samples/2048)`.
Native sample size eight produces four blocks instead of the template's eight.
Sample/PPP/tile-control fields and duplicate block counts are updated at their
source-derived G13 offsets. Broader geometry remains explicitly rejected.
Every copied native window is flushed for the device before the existing
queue-object flush/barrier/submit sequence.

`AdmissionDynamicDmaPatchDestination` now reapplies typed texture/PBE fields
using retained target-relative offsets when the Windows destination changes
between Render and Patch. It validates every field first, patches the private
shadow and actual DMA through their existing calls, updates relocation values,
materialized hash and outer hash, then lets the existing fence sealing proceed.
One Windows destination patch remains sufficient because every other source is
copied into the stable backend placement. No source BO bytes are modified.

Native binding caps the translated resident view to the recorded logical RT
span, validates it fits the actual allocation, and binds that same CPU/GPU/PA
range as backend object40. The former fixed GDI color-fill/16KiB extent is not
used for native requests. `AdmissionBackendImageBindNativeSubmission` preserves
the old output object and restores it at the same existing retirement boundary.
No CPU clear, color translation or synthetic destination is performed.

Scoped verification: all five tests in `test_apple_agx_dynamic_dma`,
`test_apple_agx_dynamic_overlay`, `test_apple_agx_render_backend_image` pass,
including ASan/UBSan. New checks cover a 15-object native graph, deterministic
Render/worker placements, CF low placement, all native root routing, eight-byte
sample metadata/four tib blocks, absent depth bias, bounded window overflow,
both RT packed fields moving at Patch with preserved bits/interior offsets,
idempotent retry, unchanged source bytes, old DMA-version rejection, and exact
4096-byte native destination binding/restoration without color fill.
The optional broader AD03 compiler fixture did not compile: local `ninja` and
its old Mesa build environment were unavailable. That is an environment result.

Windows KMD build and actual native-producer-to-consumer execution remain
required. Existing fixed-slot qualification receipts are skipped for native v4
because their old BG/store identities would misdescribe the new graph; physical
native receipts must come from the actual plan before any hardware claim.

## Actual producer execution boundary, 2026-09-13

Full runtime library and existing UmdContractTest link in snapshots m through p.
The original source projection retains MIT notices; these portability changes
remain build-local and do not import Linux transport implementations.

- m exposes unsupported timestamp conversion during real screen initialization.
  Windows timestamp capability is false; timer_resolution now stays zero.
- n asserts in real fragment NIR lowering. AD04-native-blend-abi proves NIR
  options layouts/bytes match, while native enum bitfields decode ZERO17 as -15.
  Windows projection uses unsigned storage for the same six packed fields,
  preserving the four-byte word. All legal standard factors/functions and exact
  packed bit positions pass in o/p before real producer calls.
- o/p execute real screen/resource/shader/clear/draw entrypoints, with actual
  BGRA8 tiled resource size and layer stride16384. p scope receipt fails at the
  first904-byte stage uniform (reference2, four references/one edge before abort).
  Source agx_upload_textures uses a zero-byte pool allocation even for no
  descriptors; pool.c returns its nonzero cursor. Canonicalize only this absent
  table pointer to zero at the original texture emission site. Nonempty table
  capture and unsupported-resource rejection remain mandatory.
- Separate source review corrected the latent query enable/count confusion:
  active_queries is a processing switch; actual query objects determine whether
  unsupported work is bound. Projected context currently omits query callbacks.

KMD pinned26100 ARM64 ClCompile with real code analysis passes on fresh snapshot
b332ac820af5392283a042461c12375fcf0f9c3ed8150f9c52c53f0e6de602fb:
99 objects, zero warnings/errors. Evidence AD04-native-kmd-compile/
retry-c4018-20260913a. No SYS/package/sign/install or hardware occurred.
Full producer submission/materialization/retirement is still unproven.

### Confirmed root sentinel boundary (r)

r x64 build/link succeeds; executable exits1 with eleven NATIVE_ROOT_QUERY
receipts all4294983680 (0x100004000), then root760 rejection at refs9/edges100.
This confirms the native no-query scratch sentinel as the first root failure.
Source archive e7833169bd48640c8d88e3119e2d04a78cea5cdf50f7aa0ea87490703dcc63a5;
EXE5a903c60114f90fe8aaf9e7238e3618372d0a411d048d8ef19b70640308ec546.
Source: agx_query.c556; agx_nir_prolog_epilog.c725-757; agx_state.c2289,
4932-4960; agx_linker.h57-79. FS statistics atomics require nonnull query key;
IA helper dispatch requires actual query pointers. The admitted hardware VS
prolog has no statistics path; test NIR introduces no counter intrinsics.
Windows projection now canonicalizes only null-query root upload slots to0.
Global agx_get_query_address remains unchanged. Actual bound queries remain
rejected before native work and nonzero captured query roots remain forbidden.
No fixed Linux scratch address is treated as a Windows-owned coordinate.
Fresh s executes this change; no full producer PASS claimed yet.

### Actual native draw reached (s)

s build/link passes; actual draw receipt is draws1, capsule1, backendFailed0,
any_faults0. Thus stage/root capture no longer rejects the original draw.
Windows flush/finalization does not yet reach Submitted; t instruments the exact
BatchFinish reject line and adapter status. No actual KMD consumer PASS yet.
s archive6fcc8b93b248671dde6058043536d88593a550b4e53f8e348a5441f675e6824a;
EXE45abbcd5e8e996fd470af1af2f39052f81124182625a65ff23085789f85c979f.

### BG/EOT finalization coordinate correction (t)

t reaches actual draw1 then native finalization, flags2/samples1/layers1;
encoder/scissor complete with30references and126relocations. Root lookup rejects
before adapter submission. Primary struct drm_asahi_bg_eot stores tagged u32 USC,
while asahi_bg_eot and capture use full64-bit native construction addresses.
The existing shader_base is0x1100000000; agx_usc_addr defines the32-bit relative
coordinate. Finalization now strips the low6 flags and restores this base before
exact typed capture lookup; it rejects oversized/overflow coordinates.
No new mapping or allocator. Fresh u tests this existing adapter seam.
t archive34cefabfd09969cf10e3ed7d598af8b413002224b068f25ebe096b362dba631d;
EXEf4630a4c4b1dbedcd303c297c50e536a031f38262908542de6ee1d6a311b5a29.

## Integrated execution proof (y) and final lifetime gate

y x64 full library build, existing UmdContractTest link and execution all exit0.
Actual producer receipt:30references,126relocations,9allocations, encoder137,
RT16384,29copiedobjects,36496source bytes,44952DMA bytes. Both production
KMD plans/materializations/DMA patches/native root routes pass at two placements.
Pending-marker retention, wrong-fence release rejection, ordered retirement,
unsupported topology before new submission, teardown retry injection and balanced
owner cleanup all pass. No manually constructed capture supplies this proof.
y archive b4a00e57002ad0a7e168aad63188652aa3c7014373e891d18bb2c0c4cbfaa7a7;
EXE1219ce1e97f617dc3c515532362627e05695a79b35b4994772763f1da3599820.

z-arm64 full library/executable build/link passes; ARM64 execution NOT_RUN.
Archive b3d7ecbc65807d3fd04308c501b82812bd2a46dc71704d60db3a2153febb1e43;
EXE6dc8372ccbf60a0e698662e1ac2cbac888bbb7b946691bd89591e980f7b9db5c.
Ten final focused hosttests pass. Fresh pinned26100 ARM64 KMD ClCompile/analysis:
99objects,0warnings/errors, archive b6e0c58fd4916098a6759ca15b24030076a8be1999a57c042e2122f90a993d84,
evidence AD04-final-checkpoint-host-20260913a / AD04-final-checkpoint-kmd-20260913a.

Final review exposed stale BatchOps/BatchOwner after successful detach. The
configuration now clears only after completed detach; failed detach retains it.
The same executable creates/destroys a second real screen/context on the same
backend. Final aa-x64 / ab-arm64 fresh runs verify this added reuse boundary.
These last changes affect native BO/test code only; compiled KMD sources are
unchanged from the final99-object analysis snapshot.

Hardware NOT_RUN. Installed UMD factory/frontend activation and native hardware
qualification remain next work, detailed in AD04-NATIVE-RUNTIME-ACTIVATION-NEXT.md.
Full Graphics desktop, standard Present/DWM and long stability remain unaccepted.
