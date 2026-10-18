# C15: versioned Windows DXIL / SM6 DDI prerequisite

2026-10-18. READ-ONLY bounded source pass. New note only: no committed C15 plan,
production code, capabilities, translator graph, builder or hardware changed.
Classification: RENDER_ONLY UMD negotiation/compiler contract, not DISPLAY_ONLY
or a KMD/firmware protocol change.

## Decision and precision of the minimum claim

The **smallest versioned SM6-capable candidate explicitly expressed by pinned
WDK26100 is `D3D12DDI_SUPPORTED_0011`**, using R1 interface `0x000c000a`, build1
(encoded UINT64 `0x000c000a00010000`), `D3D12DDI_DEVICE_FUNCS_CORE_0010` and the
0011 supported-model count/list. No wave/Int64 feature bit needs to be enabled
merely to support SM6.0: Microsoft explicitly makes both optional for that model.
There is no source justification to jump directly to0022 or a modern008x/010x
interface just because the installed WDK/runtime is new.

This is a **coherent implementation candidate**, not a proved universal statement
that every released runtime rejects SM6 under0003. Public material inspected does
not document that exact cross-version gate. Version suffixes show where a contract
was introduced; they alone do not prove that a later capability can never be queried
under an older negotiation. Current0003 runtime evidence itself includes a newer
0081 pipeline-level caps query. Therefore this note does not endorse adding a
model bit to0003 as a shortcut, or claim a hardware observation of0011 negotiation.
A universal minimum requires explicit runtime/spec evidence; that remains open.

The actionable smallest path is: implement the full R1/0011 table/argument and
library-reference boundary while preserving existing resources/root maps/types;
prove its ordinary DXBC baseline and model enumeration; integrate the owned DXIL
program frontend; advertise SM6.0_RELEASE only after required semantics/runtime
contracts pass. Do not cast0003 callbacks into the new table.

## Sources inspected and bounded scope

Pinned `/Users/pavel/public_windows/.local/reference/wdk26100/d3d12umddi.h`, SHA-256
`d338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28`:

| Lines | Relevant contract |
| --- | --- |
| 38-56 | R0/0003 interface/build encodings |
| 96-110 | caps IDs, notably SHADER and0011_SHADER_MODELS |
| 2076-2089 | signature entry and0012 Stream addition without size change |
| 2209-2222 |0003 shader calc/create token-pointer signatures and SAL length |
| 2240-2267 |0001 PSO arguments contain destination handle in the structure |
| 2518-2529,2608-2672 | optional tables, fill callback, versions, device/runtime callback layout |
| 3060-3175 |0003 core table |
| 3180-3469 | R1/0010 encoding, argument layouts, core table, library callbacks |
| 3473-3507 |0011 encoding, release/experimental model enums, count/list data |
| 3512-3668 |0012 wave caps and debug allocation callback/table addition |
| 4031-4059 |0015 Int64 shader caps and R2/0020 boundary |
| 4874 onward |0022 runtime callback expansion, excluded from minimum R1 candidate |
| 5537-5570 |0026 shader-code hash addition, not needed merely to accept SM6.0 |
| 12686-12690 | DDI OPTIONS1_0103 is WorkGraphsTier, not public OPTIONS1 wave data |

Current source inspected:
`drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp` GetVersions(:73), GetCaps(:132),
CreateDevice(:248), shader storage/calc/create(:420-460), state/PSO creation and
FillDDITable(:3624). Exact runtime reference is the EXP1381 trace identified by
GPU_CURRENT_STATE, not a new guest query:
`.local/experiments/EXP1381-d3d12-srv-component-map/evidence/EXP1381/tool-d3d12-on-umd.log`.

Official primary pages used:

- [0010 core table](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/ns-d3d12umddi-d3d12ddi_device_funcs_core_0010)
  and [0010 create-shader callback](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_create_shader_0010).
- [FillDDITable](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_fillddittable) explicitly documents its fifth argument as tableNum, a table index.
- [Library reference](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/ns-d3d12umddi-d3d12ddi_library_reference_0010)
  and [CreatePipelineLibrary API](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device1-createpipelinelibrary).
- Microsoft's [SM6.0 compiler specification page](https://github.com/microsoft/DirectXShaderCompiler/wiki/Shader-Model-6.0),
  [DXIL representation/container specification](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/DXIL.rst)
  and [public OPTIONS1](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_feature_data_d3d12_options1).

The public DDI pages are sparse and some requested0011/argument pages are not
available through the documentation endpoint. The actual pinned header is the
ABI source. No inference below relies on a third-party header mirror or a WDDM
history table as proof of the UMD version gate.

## Current runtime proves0003 only

EXP1381 first device receipt is `Interface=000c0002`, `Version=00080000`, matching
pinned SUPPORTED_0003 = `(0x000c0002<<32)|(8<<16)`. It requests core table type0,
size0x2c8 on ARM64; command-list type1,size0x198 with two table instances; queue
type2,size0x38. These match89/51/7 function pointers at8 bytes, respectively.
Existing GetVersions exposes only that version and FillDDITable always fills those
old types, without a per-negotiated-interface core-table dispatch.

The trace includes actual SM4/5 token programs, for example shader receipt
stage1/version0x40/15 DWORDs and stage5/version0x50/8 DWORDs. It supplies no observed
DXIL creation callback and no proved0011 model-list query. Current newer caps queries
do not establish support for new table ABI or shader semantics. EXP1388 is a library
build-only result; EXP1389 is a host compiler-only result, neither is DDI negotiation.

## Exact candidate negotiation and table boundary

R1 definitions are `MINOR_VERSION_R1=10`, build0010=0 and0011=1. Model6.0 release
is `D3D12DDI_SHADER_MODEL_6_0_RELEASE_0011=0x00060005`; experimental is0x00060000.
Model5.1 release is0x00050015. No new CORE_0011 type exists:0011 retains CORE_0010.
Do not confuse the encoding of the **supported version** with the model enum or
the fifth UINT argument of FillDDITable. Microsoft documents that argument as
**tableNum, the table index**, not an interface revision. Current source names it
version; receipts0/1 are distinct table instances. The actual selected interface/build
and documented table size must determine typed tables/cap structures.

GetSupportedVersions uses a count pointer and optional UINT64 array. Preserve the
bounded two-call sizing/capacity behavior. A future implementation can expose only
versions for which it actually implements complete matching tables. The smallest
initial candidate can return count1 and only SUPPORTED_0011, retaining0003 as the
separate known-good package. Exposing both versions as a live fallback additionally
requires unambiguous versioned table/caps dispatch and concurrency proof: FillDDITable
receives an adapter handle and table index, not a device Interface argument. Do not
use a shared last-created-device variable as that proof. Simply prepending0011 makes
the runtime able to select its new ABI; all companion callbacks must already be
correct before that.

R1 does not introduce a new CreateDevice typedef: it still uses
D3D12DDIARG_CREATEDEVICE_0003 and the0003 runtime callback branch. The0010/0011
negotiation therefore does not require0022 allocation callbacks. Device state must
record its Interface/Version and initialize all private fields; adapter/table caps
queries can occur before a device exists, so do not depend on an owner or GPU context
to answer immutable negotiation data.

A field-by-field parse of the actual WDK core tables yields:

- 0003:89 pointers;0010:91 pointers. ARM64 sizes are0x2c8 and0x2d8; x86 sizes356 and364.
- 20 existing callback fields change type;4 old serialization fields are removed;
  6 pipeline-library fields are added. Table prefix reuse is not a valid implementation.
- There is no new R1 command-list or queue table in this boundary. Keep actual
  COMMAND_LIST_FUNCS_3D_0003 and COMMAND_QUEUE_FUNCS_CORE_0001 layouts rather than
  assuming every table number advances with DEVICE_CORE.

### All20 changed callback fields

| Family | Changed fields | Required adapter behavior |
| --- | --- | --- |
| Element layout | CalcPrivateElementLayoutSize, CreateElementLayout | Accept CREATEELEMENTLAYOUT_0010 with LibraryReference; validate ordinary layout and library-reference path. |
| Blend | CalcPrivateBlendStateSize, CreateBlendState | Accept BLEND_DESC_0010, preserve existing blend fields and consume reference correctly. |
| Depth/stencil | CalcPrivateDepthStencilStateSize, CreateDepthStencilState | Accept DEPTH_STENCIL_DESC_0010; preserve all field semantics and reference. |
| Rasterizer | CalcPrivateRasterizerStateSize, CreateRasterizerState | Accept RASTERIZER_DESC_0010; preserve current state contract and reference. |
| Shader ordinary | CalcPrivateShaderSize, CreateVertexShader, CreatePixelShader, CreateGeometryShader, CreateComputeShader | Typed CREATE_SHADER_0010 argument; unified compute callback now receives struct pointer, not the old compute argument list. |
| GS stream output | CalcPrivateGeometryShaderWithStreamOutput, CreateGeometryShaderWithStreamOutput | Nested CreateShader plus SO fields; no cast to old flattened argument. |
| Tessellation | CalcPrivateTessellationShaderSize, CreateHullShader, CreateDomainShader | Calc shares0010 ordinary shader type; select union's tessellation signatures for HS/DS. |
| Pipeline state | CalcPrivatePipelineStateSize, CreatePipelineState | CREATE_PIPELINE_STATE_0010 and explicit destination handle parameter. |

The old PSO argument begins with hDrvPipelineState. The new0010 argument removes
that leading member, appends LibraryReference, and CreatePipelineState receives
hPipelineState as a separate third parameter. Casting or memcpy-as-old would shift
all shader/state handles and corrupt the target object. Use semantic adapters,
not callback-pointer casts or a size-only FillTable workaround.

Removed: pfnSerializeObject, pfnDestroyObjectSerialization,
pfnCalcPrivateDeserializedObjectSize, pfnCreateDeserializedObject.
Added: pfnCalcPrivatePipelineLibrarySize, pfnCreatePipelineLibrary,
pfnDestroyPipelineLibrary, pfnAddPipelineStateToLibrary,
pfnCalcSerializedLibrarySize, pfnSerializeLibrary.

## Shader program versus application container and lengths

0003 shader calc/create SAL explicitly uses `_In_reads_(pShaderCode[1])`. Current
StoreShader copies that many DWORDs and derives stage from code[0] high16 bits.
0010's argument still contains `const UINT *pShaderCode`, with **no independent byte
length**. It additionally carries root signature, a Standard/Tessellation IO-signature
union, Flags and LibraryReference. No automatic length-safe application container
API appears merely by upgrading the table.

The Microsoft DXIL format defines a program header inside the DXIL container part:
ProgramVersion, SizeInUint32 (including header), DXIL magic/version, BitcodeOffset
and BitcodeSize. The bitcode offset is relative to the DXIL magic field, not to the
outer DXBC container and not to the program start. Outer container size uses a
separate ContainerSizeInBytes field. These two length conventions must not be mixed.

Read-only inspection of EXP1389's exact original fixture provides a concrete case:

| Item | Observed value |
| --- | --- |
| Application container |2824 bytes, starts DXBC |
| DXIL program part body |1276 bytes |
| ProgramVersion |0x00050060, compute6.0 |
| SizeInUint32 |319 =>1276 bytes |
| DXIL magic / version |0x4c495844 /0x100 |
| BitcodeOffset / size |16 relative to magic /1252 bytes |
| Bitcode start in program |8+16=24 bytes |

This source/fixture layout fits a self-sized program-pointer DDI. It does **not**
constitute a runtime observation that a newly negotiated DXIL callback supplies
exactly that body. The inspected public callback docs say only shader-code pointer.
Thus production must not assume the complete application2824-byte container arrives
because EXP1389 used dxil_spv_parse_dxil_blob on that container.

The bounded program-body adapter to prove offline is: validate version/stage and
minimum header, checked DWORD->byte/private-size arithmetic, DXIL magic/version,
bitcode start `8+BitcodeOffset`, and `BitcodeSize <= programBytes-start`; copy owned
program bytes; pass only the verified bitcode span to the reviewed C API
`dxil_spv_parse_dxil` (raw LLVM bitcode), whose exact implementation parses that span.
Keep standard/tessellation signature metadata owned as needed. Preserve ordinary
DXBC token handling separately. All size/calc/create paths must agree; no borrowed
shader pointer may outlive create, and a library-reference load must be handled as
such instead of blindly reading a possibly absent code pointer.

There is no external readable-byte capacity in this DDI signature. Runtime input
validity is its contract; the driver can enforce declared sizes/overflow/format
constraints but cannot manufacture an independent memory bound from pShaderCode[1].
The offline file/container parser still must use its actual caller-supplied byte
length. Do not pass a full-container magic into the old word1-length parser, scan
arbitrary memory for DXIL, or guess a numeric program size.

## Supported-model and companion capability invariants

`D3D12DDI_D3D12_SHADER_MODELS_DATA_0011` has `UINT *pNumShaderModelsSupported` and an
optional model array sized by that count. GetCaps DataSize refers to the structure,
not the number of enum entries in a flat output buffer. Validate nested count/array
pointers and caller capacity; never overflow on a short array. Preserve count-only
query then fill behavior, with explicit insufficient-capacity failure. The exact
HRESULT/partial-write convention is not described by the sparse public page/SAL;
do not present an invented return code as a Microsoft requirement.

An eventual supported list can contain5.1_RELEASE and6.0_RELEASE only when those
contracts are implemented. Returning6.0 alone does not automatically prove all older
required functionality; returning an experimental value requires a separate
experimental-feature contract and is outside this plan. Reject unsupported optional
shader requirements precisely; do not advertise model support for a one-fixture subset.

Microsoft's SM6.0 specification explicitly says wave intrinsics and64-bit integer
operations are optional for SM6.0. Therefore:

- Minimum R1/0011 does not force WaveOps=TRUE or Int64Ops=TRUE. Existing compiler
  workgroup metadata or an AGX register count is not a hardware lane capability.
- WDK0012 extends `D3D12DDICAPS_TYPE_SHADER` with WaveOps/lane min/max/TotalLaneCount;
 0015 adds Int64Ops (and names DoubleOps). These are distinct larger cap structures.
  Do not write them into the current0003 20-byte shader-caps output.
- Public `D3D12_FEATURE_DATA_D3D12_OPTIONS1` is not DDI
  `D3D12DDICAPS_TYPE_OPTIONS1_0103` (1013): pinned0103 data is WorkGraphsTier.
  Public-to-DDI feature mappings must follow the negotiated shader-caps revision,
  not matching English names or numeric coincidence.
- Root signature1.1, later shader-code hash/cache callbacks,0022 allocation callbacks,
  native16-bit, view instancing and later shader models are not prerequisites shown
  by the0011 SM6.0 contract. Keep them excluded unless independently required.

Shader model6.0 inherits the required shader-stage/resource/memory contract of its
baseline; compute-only translation does not establish that full surface. Keep all
currently required Windows/AGX features truthful, including existing optional
Doubles/ROVs/typed-load and resource binding tiers. Existing fail-closed baseline
features remain explicit implementation limits, not capabilities supplied by a
new table pointer or translator library.

## Callback implementation versus documented optional refusal

**Must implement before R1 admission:** all20 typed ordinary calc/create adapters
needed by required shader/state/PSO operations; owned DXBC/DXIL and signature lifetime;
root maps/visibility/ranges/types; correct table dispatch; count/list model query;
valid error propagation; and required stage operations rather than merely registering
AGX_REJECT pointers. Existing destroy/queue/list/resource functions retain their
actual ABI and obligations. A failed shader create must not leave an apparently
valid object that later reaches pipeline compilation.

**Pipeline-library family requires an explicit decision, not six generic stubs.**
R1 adds LibraryReference to state/shader/PSO arguments and six core callbacks. The
public API allows pipeline-library absence when its support flag is absent, returning
DXGI_ERROR_NOT_SUPPORTED; that establishes an application-facing optional path.
The inspected early R1 header does not expose a separate obvious library-support
caps field that proves how this exact negotiated runtime suppresses these callbacks.
Therefore do not claim that a generic E_NOTIMPL stub safely implements R1. Either
implement coherent library creation/add/reference/serialize/destroy semantics, or
establish the exact documented/runtime absence mapping first. Ordinary zero-library
calls alone do not clear nonzero library references or serialization/lifetime rules.

**Remain optional/fail-closed when their documented feature is not advertised:**
wave/Int64 optional shader operations; later protected/video/extended feature tables;
later root-signature versions; shader tracing/optimization flags not admitted by this
candidate; later shader-code hashing/caching and new shader stages. Do not silently
ignore an unsupported flag in a required creation call. No KMD interrupt/DMA/power
callback changes follow from this user-mode compiler boundary.

## Deterministic gates before any version/model rollout

1. Compile-only ABI/type/offset checks for0003 and0010 fields on ARM64 and x86;
   function pointer assignments must use actual WDK prototypes. Exact core pointer
   count/size and removed/added offsets catch a prefix/struct-cast defect.
2. Execute actual typed wrappers against ordinary state/PSO inputs; prove PSO
   destination is the explicit handle and all state/shader fields survive. Include
   library-reference/null-code paths once their contract is chosen.
3. Execute count-only/full/short/invalid model-list queries against actual GetCaps,
   and version selection against actual typed FillDDITable. No writes beyond caller
   capacity, no owner/GPU creation from capability queries.
4. Feed the actual1276-byte EXP1389 DXIL program body, checked malformed lengths,
   bitcode offsets and stage mismatches to a bounded raw-program adapter. Compare
   raw-bitcode C API output with the already validated full-container path; preserve
   namespace/type/metadata behavior and the actual NIR/AGX/compiler-only gates.
5. Preserve the known DXBC root map/dimensions/runtime type contracts and baseline
   while introducing any versioned table. No SM6 bit is used to discover an ABI.

No build/test implementation was performed in this read-only task; these are
specific proposed invariants that catch real layout, pointer, length and alias bugs.

## WHAT IS STILL UNKNOWN / smallest coherent future checkpoint

Unresolved after this single bounded source pass:

1. Normative released-runtime minimum for querying/admitting6.0_RELEASE under0003
   versus0011; WDK identifies0011 as the first versioned model contract, but a suffix
   and an API OS-version table do not prove the runtime's cross-version decision.
2. Exact runtime DXIL pShaderCode payload form and calc/create/signature ordering;
   self-sized program-header behavior is strongly source-consistent, not yet observed.
3. Exact R1 pipeline-library absence/support mapping and library-reference/null-code
   behavior; without that mapping, implement the complete core family before admission.
4. Exact runtime table/model query ordering and short-capacity convention where the
   public docs/SAL do not specify it. Do not turn this into arbitrary cap/flag probes.

The smallest **versioned** implementation target remains R1/0011 plus CORE_0010,
unchanged0003 command-list/0001 queue/runtime callback baseline and the0011 model-list
contract. It is a substantial typed-interface/lifecycle change, not a one-bit SM6
patch. Any future experiment must follow complete offline implementation and
preregister the remaining runtime ordering/form uncertainty. A coherent R1 baseline
with only an honestly supported older model can separate table/lifecycle behavior
from later SM6 execution; model6.0 is enabled only after its mandatory feature and
production binding contract is complete. Parent must approve this staging before
changing the committed integration plan.

If runtime ordering/form observation is still needed then, the falsifiable checkpoint
is one fresh device using the fully implemented negotiated table, bounded receipts
for selected interface/build/table sizes and shader program header/length/stage,
followed by an already valid baseline shader. The recovery is the existing exact
package rollback to the inert GPU-visible guest. No such package/build/run is
authorized or performed by this note, and there is no reason here to alter KMD,
power, MMIO or firmware state.

## Later exact-runtime implementation evidence

The [26100.9549 static gate receipt](D3D12-C15-sm60-minimum-ddi-evidence.md)
now proves why0011 is needed for this saved runtime. Its constructor maps the
selected interface/build to an ordinal;0003 is1 and0011 is3. Shader-model
initialization starts at5.1 and returns before query1012 for ordinal<3; the public
consumer clamps against that same maximum. This resolves the selected-runtime
eligibility question without a capability/hardware probe. It does not establish
a cross-runtime normative minimum or complete the typed/library companions.
