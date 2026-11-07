# C15 early R1/0011 pipeline-library absence gate

2026-10-18. One bounded read-only primary-source pass; this NEW note is the only
tracked change. RENDER_ONLY UMD contract. No source/caps/table negotiation, build,
guest, hardware, commit or ledger operation was performed.

## Decision

**There is no identified early R1/0011 GetCaps enum/structure/field through which
this driver can truthfully report pipeline-library support absent.** Public
CheckFeatureSupport permits the absence of its library flag, but that does not
supply the missing UMD DDI mechanism. The inspected callback documentation does
not authorize six generic fail-closed callbacks as a complete R1 implementation.
Keep the current0003 negotiation unchanged. Before a future0011 rollout either
obtain an explicit normative absence mapping, or implement the CPU library,
reference and serialization family described below along with the typed0010
state/shader/PSO adapters. No hardware bit probe can establish that contract.

This is a bounded negative finding about inspected sources, not a proof that no
unpublished runtime opt-out exists. It closes the proposed shortcut: no claimed
absence gate is justified by the current public evidence.

## Exact source inventory and revision distinction

Inspected `D3D12-C15-ddi-model-contract.md`; pinned WDK26100
`.local/reference/wdk26100/d3d12umddi.h` SHA256
`d338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28`:

| Boundary | Exact source evidence | Consequence |
| --- | --- | --- |
|Early caps enum|D3D12DDICAPS_TYPE lines94 onward: TEXTURE_LAYOUT1000, SHADER1004, D3D12_OPTIONS1006,0011_SHADER_MODELS1012; the complete enum contains no pipeline-library or shader-cache support request.|Do not invent a GetCaps query or cast public FEATURE_SHADER_CACHE19 to a DDI query.|
|Early options/shader structs|D3D12DDI_D3D12_OPTIONS_DATA_0003, SHADER_CAPS_0003 and R1/0011 additions have no library/cache absence field.|None is a support-bit gate for the six callbacks.|
|R1 introduction|MINOR_VERSION_R1=10, SUPPORTED_0010 build0; SUPPORTED_0011 build1 retains CORE_0010.|The library family is part of the R1 core ABI, not an optional extension table.|
|Library references|LIBRARY_REFERENCE_0010 at3192 has hLibrary and UINT PipelineIndex; appears in layout, blend, depth/stencil, rasterizer, shader and PSO creation arguments.|A working ordinary null-library path does not implement nonzero-library loads.|
|Six callbacks|3328–3351 signatures;3463–3468 table fields.|Three HRESULT operations, two SIZE_T operations and one VOID destroy; they are not six interchangeable error-return functions.|
|Later OS-managed cache|0021_SHADERCACHE_CALLBACKS, feature version SHADERCACHE_0021_0, GetValue/StoreValue runtime callbacks at4240–4270; SetExtendedFeatureCallbacks is separate.|This requests a runtime cache service; it is not an early library-support opt-out.|
|Later driver-managed cache|D3D12DDI_D3D12_OPTIONS_DATA_0080::DriverManagedShaderCachePresent and IMPLICITSHADERCACHECONTROL_0080 at10146–10193.|A later cache-control feature. Setting FALSE does not remove R1 pipeline libraries.|
|D3D11 cache|d3d10umddi.h D3DWDDM2_2DDICAPS_SHADERCACHE155 / RequestRuntimeShaderCache.|Wrong API and revision for suppressing D3D12 R1 library callbacks.|
|Public feature|winsdk d3d12.h D3D12_FEATURE_SHADER_CACHE19; D3D12_FEATURE_DATA_SHADER_CACHE::SupportFlags; LIBRARY0x2 and SINGLE_PSO0x1.|API-facing output, not a driver GetCaps structure or negotiation instruction.|

The Microsoft [cache support flags documentation](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_shader_cache_support_flags)
identifies SINGLE_PSO as always supported and associates LIBRARY with drivers
targeting the Windows10 Anniversary Update. Thus reporting all cache support
absent is not even the same public claim as reporting only libraries absent.
The association with that OS generation is evidence consistent with R1, but it
is not a normative exact DDI-build gate by itself.

The [CreatePipelineLibrary API documentation](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device1-createpipelinelibrary)
specifies the public library-flag check and unsupported result, and says old
drivers cannot use the feature. It does not identify a0011 UMD caps field or
state that CORE_0010 drivers can opt out through E_NOTIMPL.

Microsoft's [ShaderCache specification at the pinned DirectX-Specs commit](https://github.com/microsoft/DirectX-Specs/blob/71e431fa0f713818c6a3a66f49ab0e6732712d15/d3d/ShaderCache.md)
explicitly separates later driver-managed cache presence/control from existing
shader caching APIs. No early pipeline-library absence mapping was found there.
No third-party driver implementation was used as normative evidence.

## What the six registered callbacks actually promise

| Callback suffix0010 | Signature/operation | Documented opt-out result? |
| --- | --- | --- |
|CalcPrivatePipelineLibrarySize|SIZE_T(device,const CREATE_PIPELINE_LIBRARY*); arguments contain InitialDataSize,pInitialData.|No documented zero-size/unsupported signal. Zero cannot be assumed to suppress later calls.|
|CreatePipelineLibrary|HRESULT(device,const args*,hLibrary).|Microsoft page only specifies HRESULT; no explicit absence/suppression contract.|
|DestroyPipelineLibrary|VOID(device,hLibrary).|No return value; must release an actually created object correctly.|
|AddPipelineStateToLibrary|HRESULT(device,hLibrary,hPipelineState,UINT PipelineIndex).|No documented generic unsupported gate.|
|CalcSerializedLibrarySize|SIZE_T(device,hLibrary).|Pinned signature has no error output or feature gate.|
|SerializeLibrary|HRESULT(device,hLibrary,VOID*pBlob).|No output capacity argument, so size/serialization consistency is a memory-safety invariant.|

Official callback pages inspected: [private-size](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_calc_private_pipeline_library_size_0010),
[create](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_create_pipeline_library_0010),
[destroy](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_destroy_pipeline_library_0010),
[add](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_add_pipeline_state_to_library_0010),
[serialize](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_serialize_library_0010)
and [library reference](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/ns-d3d12umddi-d3d12ddi_library_reference_0010).
The serialized-size page was unavailable through the endpoint; the exact pinned
header supplies its declaration. The available pages provide little behavioral
text; generic HRESULT wording does not prove a permitted absence policy. The
reference page's listed minimum OS also must not override the header's actual
0010 introduction by inference from an OS-version label.

A failing Create callback can legitimately report a real allocation or validation
error. That does not prove an unconditional failure is the complete implementation
of a version whose table includes the library family. Registration alone likewise
does not prove functionality. We cannot establish from these sources whether the
runtime would reject the device immediately or defer the failure to an app call;
neither behavior should be discovered through capability probing.

## Smallest concrete CPU implementation alternative

The following is a proposed original implementation contract, not a claim that it
is already implemented. It uses only the exact six0010 callbacks and existing
LibraryReference fields. No extra Windows interfaces are invented. KMD, BOs,
Screen/context construction and AGX compilation do not belong to library storage.

1. **Canonical owned PSO recipe.** Define a pointer-free logical recipe for the
   currently implemented compute or VS/PS graphics stages: validated owned
   DXBC/DXIL program spans, canonical root-signature data, owned input-element and
   signature arrays, blend/raster/depth values, formats/sample/topology fields,
   root namespace/type metadata and compiler options. Do not serialize a memcpy
   of current AgxD3d12PipelineState::desc: it contains runtime/private handles.
   Current PSO code copies programs/states, but its desc still contains such
   handles and its compute/graphics objects contain owner/compiler state.
2. **Library record indexed by PipelineIndex.** Runtime owns public names; this
   DDI accepts a UINT index, not a string. A library owns immutable indexed recipe
   references. Add validates device provenance, snapshots the supported live PSO
   recipe, interns equal subcomponents by hash PLUS exact-byte comparison, and
   publishes atomically. It cannot keep a borrowed PSO/shader/root handle. Missing
   indices fail before any shader dereference; cross-device handles fail. Exact
   replacement semantics for a repeated driver index are not specified by the
   inspected DDI docs; refuse conflicting replacement rather than silently change
   existing entries. Public StorePipeline separately forbids name overwrite.
3. **Typed reference resolution.** Every changed0010 calc/create path resolves
   nonzero hLibrary/PipelineIndex through the device's registered library and
   obtains the appropriate owned component. Calc size and Create must use the
   same validated data and checked arithmetic, including when pShaderCode is NULL.
   The resulting shader/state/PSO owns its copy or retained immutable component,
   so destroying the source PSO or library cannot invalidate it. A null library
   follows the ordinary creation path. If both explicit data and a library entry
   are supplied, enforce semantic compatibility; do not silently select conflicting
   bytes. Exact null-code/runtime field-elision conventions remain a documented
   runtime-ordering unknown, not permission to dereference NULL.
4. **Own memory and lifecycle.** CalcPrivate returns the fixed driver object header
   size. Create validates empty or imported payload and initializes that header;
   dynamically allocated immutable components have checked lengths and explicit
   references. Driver-created storage can copy imported bytes even though the
   public API allows its input buffer to remain borrowed for the library lifetime.
   Destruction releases CPU references only, with clear partial-create cleanup.
   Already returned objects retain their own components. All callbacks resolve
   device-local handles before dereferencing caller-provided private pointers.
5. **Pointer-free blob and provenance.** Use an explicitly versioned driver-private
   format with fixed endianness, total length, checked offset/length tables,
   per-stage model/encoding, content hashes, driver/compiler/frontend schema
   identity and adapter/architecture provenance. No raw C++ struct padding,
   virtual addresses, function pointers, GPU handles, NIR pointers or native
   executable objects. Reject corrupt/truncated/overlapping/wrong-version data
   before publication. Do not assume pInitialData is the whole public API blob:
   it is the opaque payload actually supplied at the DDI boundary; runtime framing
   and driver framing must remain separate.
6. **A safe minimal unsized serialization policy.** SerializeLibrary has no capacity
   parameter. Do NOT use a mutable exact size from a preceding Calc call and then
   serialize a newer, larger library. A conservative initial CPU implementation
   can use a fixed bounded serialized arena length L per library schema: CalcSize
   always returns L; Serialize locks the library, emits a consistent snapshot and
   initializes exactly L bytes; used-length/table checks distinguish live data
   from zero padding. Add returns a real E_OUTOFMEMORY before exceeding L. This
   removes any dependence on undocumented Calc/Serialize call pairing, cross-thread
   ordering, or a shared last-size cache. L is a documented CPU storage policy,
   not a hardware constant or admission escape. A variable-size optimization
   requires a proven runtime capacity/ordering contract first.
7. **Thread safety and reuse.** Synchronize mutation, indexed resolution,
   serialization and destruction appropriately; retain immutable components while
   producing objects. Do not assume a global latest-library/size/device variable
   identifies concurrent callers. Rehydrate the same logical PSO compiler path;
   a first implementation may rebuild owner-native state from the cached recipe
   rather than persist GPU executable pointers. This is functional library storage,
   not a claim of a validated compiler-speed improvement.

Public [StorePipeline](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12pipelinelibrary-storepipeline)
requires unique names and defines allocation/name errors. Public
[LoadGraphicsPipeline](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12pipelinelibrary-loadgraphicspipeline)
checks the supplied PSO description against the library. These support immutable
entries and compatibility validation; they do not define additional DDI parameters.
Public [Serialize](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12pipelinelibrary-serialize)
has an explicit caller buffer length, unlike the UMD callback, and permits later
process reuse subject to driver/runtime and hardware compatibility. Its capacity
check must not be mistaken for a size argument available to the driver callback.

## Deterministic gates and remaining unknowns

Before any0011 negotiation change, actual-function CPU tests must cover the six
callbacks plus all changed0010 component calc/create adapters: empty/create/add,
lookup and null-code loads, compatibility errors, serialize/import/serialize,
malformed offsets/lengths/provenance, OOM rollback, and destruction of original
shaders/PSOs/library while returned objects survive. Use the actual pinned structs
and function-pointer types on both architectures. Prove no owner/GPU allocation
from library storage, query or serialization. Buffer canaries and a concurrent
Add/Calc/Serialize test must prove the L-byte bound. Ordinary DXBC resource/root
mapping semantics remain an independent required baseline.

Still unknown after the single primary pass:

- No explicit early DDI absence mapping was found. The public optional flag alone
  does not resolve that missing mapping; six generic stubs remain unjustified.
- Exact runtime choice of explicit versus elided component data for a nonzero
  LibraryReference, including which calc/create calls accompany a load.
- Exact runtime framing of driver pInitialData/pBlob and any serialization ordering
  guarantees beyond the signatures. A bounded private payload and constant-L
  policy avoid memory-unsafe assumptions but do not constitute observed ordering.
- Exact public error propagation for unsupported/corrupt cached driver payload
  versus runtime validation. Do not assign API error codes to undocumented callback
  cases merely because their names look appropriate.

The recommended next causal target is the typed CPU0010 adapter/library family,
not a caps experiment. Keep GetVersions and the current0003 table unchanged until
that family and the broader0011 contract are implemented. In particular,
[FillDDITable's fifth argument is tableNum, a table index](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_fillddittable),
not an interface version. Future dispatch must use the actual negotiated contract
and correct typed table size; this note makes no edit to the current callback.
