# G4 GPUVA integration to EXP810

Scope: merge the offline G4 Mesa/UMD path into the established G3 GpuMmu
launch, admit one native render submission, and observe DWM presentation on
the validated scanout surface. No Mu, m1n1, signer, caps, or recovery change
is bundled with this phase.

## Sources and measured boundary

- `investigation/GPU_CURRENT_STATE.md` and EXP809S: Code0; DWM PID1220 uses
  Apple LUID `0x4cc44` and loads our UMD; Apple Present/Blt/submit counters
  are zero. EXP806 made stripes visible through the selected PA and DCP.
- `docs/superpowers/specs/2026-09-24-gpuva-g4-umd-contract.md`, G4 Mesa
  `agx_win32_gpuva_batch.c`, current KMD `gpuva_g3_windows.c`,
  `callbacks.c`, `submission_windows.c`, `render_backend_image.c`, and
  `backend_platform_windows.c`: UMD emits a 24-byte AGX4 header plus native
  Asahi render bytes; G3 KMD still requires physical Render shadow and its
  backend binds an existing template rather than this native command.
- Asahi Linux `drivers/gpu/drm/asahi/queue/mod.rs` and Mesa
  `include/drm-uapi/asahi_drm.h` explain native command framing and address
  fields. The UAPI header is MIT licensed, compatible with this repository's
  MIT license; any copied definitions must retain its copyright notice.
  No external implementation code has been copied.
- `m1n1_windows/src/hv_agx_gpuva_v5.c` and the G3 graph own per-process UAT
  publication, validated backing, job pins and TLB. The Mu submodule is not
  populated in this worktree; its exact frozen EXP809S image and APPL0002
  ACPI exposure remain the only measured Mu contract for this phase.
- Pinned WDK 26100 `d3dkmddi.h`/`d3dumddi.h` and Microsoft Learn
  [GpuMmu](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpummu-model),
  [virtual submit](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_submitcommandvirtual),
  [residency](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/residency-overview),
  and [virtual context](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/ns-d3dumddi-_d3dddicb_createcontextvirtual)
  define private data, no-patching context metadata, residency and fencing.

## Ownership and contract difference

VidMm owns allocation backing, page tables and residency. UMD owns stable
GPUVAs, map/residency waits, native bytes, written-primary references and
resource lifetime. KMD must validate AGX4 and every reachable native VA
against the process graph before GPU access, build the TA/3D job, correlate
completion with the scheduler fence, and keep resources pinned until completion.
m1n1 publishes the UAT and enforces backing ownership; Mu only exposes the
unchanged device. Physical/patch-list KMD submission cannot consume G4 data.
`CreateContextVirtualCb` yields a context handle; the KMD's
`DxgkDdiCreateContext` supplies no-patching metadata and private-data capacity.

## Implementation sequence

1. G4 UMD selection and process-local HRESULT receipts for adapter/device,
   resource, present, virtual-context, submit and render fence. Host callback
   replay and pinned ARM64 UMD link. **Done:** `1b3ade74`.
2. WDDM virtual render context: `NoPatchingRequired`, 16 written-primary
   references, zero patch locations; preserve G3 system/GDI contexts. Real C
   replay RED to GREEN and pinned ARM64 KMD link. **Done:** `453f6cc8`.
3. Versioned AGX4 envelope and native render parser. Check exact command VA
   and length, private-data bounds, command framing, every command-specific VA
   and graph permissions before touching the GPU. Direct compute remains
   unsupported. Host malformed-input and real graph replay must be RED to
   GREEN. Until step 4, accepted envelopes still fail closed.
4. Translate the admitted native render payload into the existing AGX queue
   and firmware structures without fabricating completion; use Asahi behavior
   as a reference with license review. Pin command and target mappings through
   TA/3D completion, then signal the scheduler/render fence. Evict/free waits
   for the exact completion or poisons uncertain ownership. Host backend and
   completion replay must be RED to GREEN.
5. Route written-primary Present/flip to the already validated scanout PA,
   with explicit cache and ordering evidence. Run focused tests, one full host
   suite, pinned ARM64 KMD/UMD link, source-manifest hash gate and exact package
   preregistration before EXP810.

EXP810's smallest hardware checkpoint is Code0 plus durable UMD
CreateDevice/Present/SubmitCommandCb HRESULT, KMD AGX4 admission/TA/3D
completion/Present/flip receipts, and a LOOK_NOW operator observation held
until LOOK_DONE (at most 30 minutes). R84 starts from a full SoC reset. R60
permits one bounded disarmed recovery only with durable arm consumption and
exact package identity; otherwise use the GPU-hidden dump-first path. The
normal end state is the frozen GPU-visible Code28 guest with the exact package
removed. R86 range grants are a separate change only if measured broker
capacity blocks DWM load.
