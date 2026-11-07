# C15: exact local runtime R1 payload and framing contract

2026-10-18. Source-only analysis of the already-local D3D12Core image,
matching Microsoft PDB, pinned WDK, original DXIL fixture and reviewed C API.
No runtime module was loaded, no driver/capability was changed, and no external
implementation was copied. This note records implementation evidence for one
runtime; it is not a universal Microsoft DDI guarantee or R1 admission decision.

## Scope and preserved earlier results

The runtime is D3D12Core **10.0.26100.9549**, COFF-ARM64X, image size
`0x5eb000`. The previously established negotiation map for this image assigns
`0003` ordinal 1, `0010` ordinal 2 and `0011` ordinal 3. The positive early-R1
claims below apply to **ordinals 2/3** and the inspected successful creation
paths. They do not infer early behavior from a latest-version signature.

This supersedes only the unresolved *analysis boundary* in
`D3D12-C15-library-runtime-elision.md`: the new matching PDB permits named
adapter association that the earlier anonymous static pass could not establish.
The earlier note remains unchanged, SHA256
`193949d5828a15f738983c170a3d035e5c1452166892dd5d831ec9c55590f009`.
Its stopping decision was appropriate to its evidence. EXP1411's strict
InfoAge-equality failure is also preserved; EXP1412 is a separate validation
using the source-derived matching policy, not a rewritten EXP1411 verdict.

## Exact inputs and identity

| Input | Bytes / identity | SHA256 |
| --- | --- | --- |
| Scratchpad `d3d12core/d3d12core.dll` | 6,235,552; version 10.0.26100.9549 | `052241d7d9fdcf32dc2a86491c56e524f80e78990e6a3ddd787f75767bd41a82` |
| Microsoft `d3d12core.pdb` | 13,627,392 | `bdbf007ee03a383b264a0bb50a3f96bf3d8818fae2562ad4dfc068b982543231` |
| WDK 26100 `um/d3d12umddi.h` | 1,107,252 | `d338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28` |
| Original `tests/fixtures/dxil/exp1397_spaces_cs60.dxil` | 3,704 | `b22b71c9856cd3f2514a98609331b9ee4ab2b5e83aa8086d148243c277d52de5` |
| LLVM 23.1.0 `llvm-pdbutil` | 875,936 | `f73b222e20ac6c743713274a7387a985dc4143189f4974d0dd09cef58339ffc2` |

The DLL RSDS record names `d3d12core.pdb`, GUID
`CA04B74C-704D-76D2-917D-3123557CF688`, image age 1. The exact symbol-server
key is `CA04B74C704D76D2917D3123557CF6881`. EXP1411 fetched only the
[corresponding Microsoft symbol URL](https://msdl.microsoft.com/download/symbols/d3d12core.pdb/CA04B74C704D76D2917D3123557CF6881/d3d12core.pdb),
preserving HTTP headers, effective HTTPS URL, elapsed time and content hash.

Microsoft's [PDB `OpenValidate` implementation](https://github.com/microsoft/microsoft-pdb/blob/master/PDB/dbi/pdb.cpp)
(the inspected lines 762–815) requires exact GUID, PDB InfoAge at least image
age, and matching nonzero DBI age. The parent inspected this primary source;
no additional source download occurred in the analysis lane. A content-pinned
copy of that Microsoft source was not produced by this checkpoint, so its URL
must not be confused with a source-content hash.

EXP1412's original bounds-checked MSF reader read stream 1 and stream 3:

| Check | Observed | Result |
| --- | --- | --- |
| Stream 1 GUID | exact RSDS GUID | PASS |
| Stream 1 InfoAge | 4 >= image age 1 | PASS |
| Stream 3 DBI Age at offset 8 | 1 == image age 1 | PASS |

No legacy zero-DBI-age waiver was used. The local LLVM `MSFCommon.h` and
`RawTypes.h` layouts, reader source and input hashes are in the EXP1412 receipts.
Only after PASS did the pinned reader extract publics/globals/section map.
EXP1411 remains FAIL under its original, overly strict summary-age predicate.

## PSO: ordinary fields and library reference are independently constructed

The matching PDB and existing disassembly identify this chain:

| Named boundary | RVA |
| --- | ---: |
| `CPipelineState::CDriverBlob::GetOrLoadPSO` | `0x2c18d8` |
| `CDevice::CreatePipelineStateImpl` | `0x253008` |
| `CPipelineState::FinalConstruct` | `0x1f2bb8` |
| `CSafeDDICreatePipelineState` constructor | `0x1dd4b8` |
| `CDevice::VersionedCreatePipelineState` | `0x2c41c0` |
| Three-argument typed `_0010` descriptor invoker | `0x2c02a8` |
| Descriptor invoker with runtime PSO handle | `0x2c0338` |

`GetOrLoadPSO` returns an existing cached PSO without another DDI creation, or
copies all `0x370` bytes of the caller's public pipeline stream, injects the
cached-PSO subobject, and forwards the separate original library reference to
`CreatePipelineStateImpl`. It does not strip ordinary stage/state fields.

On the successful creation continuation, `CreatePipelineStateImpl` selects
stored entry metadata when the library is nonzero and skips some repeated
validation/linkage checks. It still obtains ordinary shader-bytecode objects,
creates/looks up ordinary shaders and graphics states, and stores their results
in layered creation args. Compute versus graphics is selected by public CS
bytecode presence, not by library presence. The directly inspected bytecode and
blend/depth/raster helpers rejoin ordinary cache/create paths after their
library-dependent validation branches; they do not return NULL simply because
the library exists.

The layered base is `x26+0x28`: metadata at `+0x18`, compute at `+0x20`, VS/PS
at `+0x28/+0x30`, GS/DS/HS at `+0x38/+0x50/+0x58`, input/blend/raster/depth
at `+0x60/+0x68/+0x70/+0x78`. The original library reference is copied
independently to `+0x88`. These offsets agree with the safe constructor's loads.

The safe constructor writes all ordinary `_0010` fields before constructing
`LibraryReference`. Missing individual stage/state objects can yield NULL
handles; those checks do not inspect the library. It separately forms the
library handle/index at output offsets `0xa0/0xa8`. The WDK ARM64 `_0010`
descriptor is `0xb0` bytes.

`IsD3D12DDIVersionOrLater` at `0x216e20` compares the stored ordinal against
the requested threshold. For ordinals 2/3, the PSO adapter's
`0x2c430c..0x2c4350` branch copies the complete `0xb0` descriptor and invokes
the typed `_0010` callback without modifying fields. No branch in these
inspected producer/constructor/adapter layers uses nonzero library to elide
ordinary fields. Generic object-factory dispatch was not reconstructed by
classifying anonymous virtual slots; the producer and named consumer agree on
the full layered schema.

**Consequence:** this runtime PSO path provides no basis for discarding ordinary
fields or treating NULL as “inherit from library.” It does not make every
optional stage nonnull, prove every public-input/error path, or settle the
library's record/replacement/serialization rules.

## Shader: actual early-R1 slot and ordinary payload

The native chain is `CDevice::CreateShader` (`0x2548a0`) →
`CShader::FinalConstruct` (`0x257a88`) → native `CSafeDDICreateShader`
constructor (`0x244c58`). The earlier inspected `0x72460` is the other ARM64X
copy, not this native call target; its relevant stores agree.

The `_0010` ARM64 prefix is:

| Field | Offset |
| --- | ---: |
| `hRootSignature` | `0x00` |
| `pShaderCode` | `0x08` |
| `IOSignatures` | `0x10` |
| `Flags` | `0x18` |
| `LibraryReference.hLibrary` / `PipelineIndex` | `0x20` / `0x28` |

Size is `0x30`. WDK `_0026` preserves this exact prefix and adds
`ShaderCodeHash` at `0x30`. The native safe constructor derives ordinary root,
code, signature and flags from layered args, independently constructs the
library reference, and has no library-dependent ordinary-field omission.

Compute stage 7 explicitly tests ordinal >= 2 at
`0x257b58..0x257b80`. Ordinals 2/3 call invoker `0x24ce30` with that descriptor;
the older branch uses different loose arguments. Although the generic invoker's
PDB signature names `_0026`, the early-R1 association is established separately:
the DDITraits getter is `GetpfnCreateComputeShader`, its early-R1 vftable facts
and actual PE bytes are recorded, and both getter copies load raw DDI table
`+0x98`, WDK `_0010` index 19. Invoke helper `0x2257e0` forwards the preserved
device/descriptor/handle without rewriting fields.

Thus this is not an assignment of a latest callback to `_0010` from signature
similarity alone. Keep exact decorated names from the receipts; abbreviated
template tuple labels copied from encoded digits are not numeric-version proof.
The inspected VS/hull adapters have the same explicit early-R1 threshold, but
geometry-SO, mesh and task contracts are not generalized from this result.

**Consequence:** successful fresh compute creation at ordinals 2/3 supplies
ordinary root/code/signature/flags plus library reference. Nonzero library
alone does not explain a NULL ordinary input in this path. This is not a
universal NULL-inheritance policy or a guarantee for arbitrary caller pointers.

## Framing: program payload, then bounded raw-bitcode extraction

`VersionedDeviceData::GetShaderBytecode` (`0x3b9080`) selects code by shader
model/projection state, without a library argument. On its successful SM6 DXIL
path it calls `DXBCGetDxilShaderCode` (`0x2d6228`). That helper finds the `DXIL`
part and `CDXBCParser::GetBlob` (`0x1fcd20`) returns:

`container base + part offset + 8`.

This points at the **DXIL program header**, including its two initial DWORDs and
bitcode header. It is neither the outer container nor raw LLVM bitcode. The
fallback driver-code path selects `SHEX`/`SHDR` payload instead; SM6 selection
must not be equated to an unconditional DXIL result on malformed/missing parts.

Before returning DXIL, this runtime helper checks a nonnull payload, at least
8 payload bytes, and `payload[1] <= floor(part_payload_bytes / 4)`. It does not
itself enforce a six-DWORD program minimum or validate all bitcode fields.
`ReadDXBC` validates container/part ranges against the supplied size;
`ReadDXBCAssumingValidSize` derives that size from the container's own header.

On the ordinary nonlibrary compute path, the named compute validator separately
compares container-declared size with public
`D3D12_SHADER_BYTECODE.BytecodeLength` at `0x2cb634..0x2cb650` and rejects
zero/mismatch before continuing. Library paths skip repeated validation, so
this check must not be represented as freshly executed for every library load.

The original EXP1397 fixture makes the mapping concrete:

| Item | Value |
| --- | ---: |
| Container bytes | 3704 |
| DXIL part header / program payload offset | 2016 / 2024 |
| ProgramVersion | `0x00050060` |
| SizeInUint32 / program bytes | 420 / 1680 |
| DXIL magic / version | `0x4c495844` / `0x100` |
| Bitcode offset from program / bytes | 24 / 1656 |

The reviewed dxil-spirv pin is
`8434b0639fb2a48ce13aac08a420ecce5d23df0e`:

| Source | SHA256 |
| --- | --- |
| `dxil_spirv_c.h` | `4fbca14fe1de04b463cd8d8f6f449fc4e57490c5c900eaff9e6d6d65ea55303c` |
| `dxil_spirv_c.cpp` | `0bf241fa36d2967fe6e06b9f5d446c8d7aed06b8484ed610d6ba8a48cb57d81d` |

`dxil_spv_parse_dxil` takes raw LLVM BC; `dxil_spv_parse_dxil_blob` takes a full
DXBC archive. Neither is documented to accept a naked DXIL program. The
original owned adapter's correct mapping is explicit-length PROGRAM input →
`agx_dxil_parse` → validated bitcode span → `dxil_spv_parse_dxil`.
For the fixture that means 1680 program bytes become 1656 raw-bitcode bytes.

A future DDI-specific span-acquisition adapter may rely only on the proven
producer boundary: first eight bytes are accessible and the runtime bounds the
word count by the part size; checked minimum/overflow/policy limits must precede
the explicit-length PROGRAM parser. The bounded parser API itself must continue
requiring actual accessible length. No unsized-pointer adapter was implemented
or tested here, and this scoped runtime evidence is not a general safe-reader
contract for arbitrary pointers.

## CalcPrivateShaderSize uses the same early-R1 framing

The named `CDevice::GetLayeredChildSize` builds the descriptor with the same
native safe shader constructor. Its actual stage-7 switch selects
`0x1e55f0`, calling `VersionedCalcPrivateShaderSize` (`0x27c850`). The ordinal
>= 2 branch forwards the descriptor through invoker `0x225240`; its getter
loads raw table `+0x78`, pinned `_0010` index 15. The actual invoke helper
does not change payload fields. Ordinals 2/3 therefore use the same ordinary
prefix and program framing in size and compute-create callbacks. Below 2 is a
separate legacy path. This does not establish every other shader/state size DDI.

## Remaining gates

These findings reduce runtime ambiguity; they do not authorize production R1,
SM6 caps, translator linkage or hardware admission. Remaining gates include:

- Owned library/PSO recipe implementation, device provenance, immutable lifetime
  and retention across source object/library destruction.
- Exact serialization framing and capacity, repeated-index/replacement semantics,
  malformed import handling and error mapping. No rule is invented from missing
  documentation or from the public API being optional.
- Original DDI-specific program-span acquisition and deterministic tests using
  this evidence, while preserving the explicit-length parser boundary.
- Reviewed dependency OOM policy: the existing C wrapper cannot make unchecked
  internal allocator/vector failures recoverable. Existing production OOM
  blocker and license/dependency boundaries remain in force.
- Shared-context concurrency across independent PSOs, callback reentry, TLS
  ownership/nesting and owner/native integration. C22 publication work does not
  establish these broader contracts.
- Truthful model/capability/companion-DDI coverage. The exact local runtime's
  `0011` eligibility result is not a universal WDK rule or proof that registered
  fail-closed library callbacks are sufficient.

## Immutable local receipts

All paths below are under `/Users/pavel/public_windows/.local/`. Manifests carry
the individual source/tool/input/output hashes and raw excerpts. Earlier files
were not overwritten by later appendices.

| Receipt | SHA256 |
| --- | --- |
| `c15-runtime-symbols-1411/artifact-manifest.json` | `ea293180713e2617e1d04314288b4e2e760e4a8593226aa7dffe0acd8013e445` |
| `c15-runtime-symbols-1411/verdict.json` | `0debd6c86516173bb8b7cc428d6dc860dfa603d7fa93c41170339226b305e024` |
| `c15-runtime-identity-1412/artifact-manifest.json` | `1a11b7d0d0984f579328313a79bdf998b12e5a9784d7ca6145bbe6cdaf232217` |
| `c15-runtime-identity-1412/verdict.json` | `49f7e1b2b2b8254e26dd39717df03565674fd91466434e72d74038f55dfe5d92` |
| `c15-runtime-pso-contract/artifact-manifest.json` | `35644158ab447181972d5f0a71fb89f84dd36a69594a9274158f1a8cbaef21b5` |
| `c15-runtime-pso-contract/extension-artifact-manifest.json` | `c5db907b1c2e26282425a9611f0e34725157ecf8bc8be3e86f8c0d6c947cd0d2` |
| `c15-runtime-pso-contract/getorload-manifest.json` | `aaf5d7f43dfc2f18d2251bae2d067c01d3c17c3afde93ad04c865455ae99c5e4` |
| `c15-runtime-pso-contract/producer-manifest.json` | `c9153e96ffe17c8521303b6c5162db6a1a5218a299b7b3e156a1514306e18092` |
| `c15-runtime-pso-contract/shader-manifest.json` | `8663a4d6ea4c43d9068365c88461dd8a2a3cbda15681f5439911c95f2521590e` |
| `c15-runtime-pso-contract/framing-manifest.json` | `5b060810ceb160f17bd71f68693f70c3627e5c021b2bfb7c43986d01550aa7b0` |
