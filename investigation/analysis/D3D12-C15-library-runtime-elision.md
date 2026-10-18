# C15 LibraryReference elision: bounded local static pass

2026-10-18. Read-only inspection of already-local runtime bytes/disassembly and
pinned SAL; no new documentation-absence search, no module execution, symbol
fetch, compiler/build, Windows probe, capability/version change, guest, ledger
or commit. This pass stops with a precise unresolved dispatch association.

## Exact inputs and established ABI facts

Same local D3D12Core10.0.26100.9549 as the proved SM6 version gate, SHA256
`052241d7d9fdcf32dc2a86491c56e524f80e78990e6a3ddd787f75767bd41a82`,
COFF-ARM64X, image0x5eb000, CodeView
`CA04B74C-704D-76D2-917D-3123557CF688`/age1. Existing scratchpad directory
`d3d12core/` contains d3d12core.dll, D3D12.dll and dis.txt. No matched local
D3D12Core PDB was found in the project evidence/archive/scratchpad search.
Nearest-export disassembler labels are not internal method symbols.

Pinned WDK26100 SHA256
`d338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28`:
CREATE_SHADER_0010 contains root handle, program pointer, signature-union pointer,
flags and LibraryReference. The outer callback argument is `_In_`; it does not
conditionally annotate the interior code/signature/root fields for a library load.
The same applies to the full ordinary fields in CREATE_PIPELINE_STATE_0010.

A direct parse of actual CORE_0010 declarations gives these zero-based positions:

| Callback | Index | Raw ARM64 table offset |
| --- | ---: | ---: |
|CalcPrivateShaderSize|15|0x78|
|CreateComputeShader|19|0x98|
|CalcPrivatePipelineStateSize|33|0x108|
|CreatePipelineState|34|0x110|

These are offsets in the RAW91-pointer DDI table, not arbitrary C++ vtables or
necessarily the runtime's normalized per-version dispatch object. They cannot
identify a callsite unless the loaded table's provenance is independently proved.
In particular,0010 CreatePipelineState has a separate destination argument; its
ordinary descriptor is not0001's descriptor with a leading destination member.

## Bounded inspection and why it does not prove elision

The local disassembly contains176 candidate indirect pointer loads at offset0x98.
Representative candidates also occur in plainly unrelated object handling, so a
raw offset search is insufficient to identify pfnCreateComputeShader. No candidate
was promoted to a shader-library callsite based solely on this numeric match.

A concrete public-side pipeline-creation region at RVA0x7d940–0x7da24 references
the local ASCII diagnostic string `SODB: CreatePipelineState` (rdata RVA0x479f98)
and calls internal normalization/creation helpers. Its analogous second code copy
is nearRVA0x252efc. This establishes a nearby pipeline-operation region, not the
identity of the final typed0010 callback invocation or the source of its
LibraryReference. Internal virtual dispatch and version adaptation intervene.
The diagnostic name alone does not justify assigning a stack slot to pShaderCode,
hRootSignature, IOSignatures or LibraryReference.

No single candidate's final call target AND argument provenance was established
through the normalized runtime dispatch to the raw CORE_0010 field. Therefore
there is no hard evidence in this pass that nonzero-library requests either retain
or elide any particular ordinary field. Nor is there evidence that a NULL field
should mean inherit, absent or mismatch. No old probe's lack of library operations
is used to infer those semantics.

## Exact unresolved source question and stopping decision

Needed evidence is the mapping from the exact runtime's per-version shader/PSO
adapter to its raw CORE_0010 CreateComputeShader/CreatePipelineState callbacks,
followed by the dominating field stores/branches constructing the actual
CREATE_SHADER_0010 / CREATE_PIPELINE_STATE_0010 argument when LibraryReference's
hLibrary is nonzero. Without that association, inspecting more unidentified
stack writes would be speculative.

A matching already-available PDB or a primary runtime/UMD contract for that specific
adapter would resolve the boundary. No symbol fetch or broad second archaeology
pass was authorized/performed here. Stop this pass rather than assume a calling
convention from a public API description. Repeated PipelineIndex, opaque blob
framing and DDI error translation were intentionally not reopened.

Consequence: the pure owned recipe/library module remains independently designable,
but this pass supplies no justification to simplify the typed library-reference
integration into ordinary input copies or a NULL-inherit rule. The separately
proved0011 eligibility gate remains unchanged; it must not be inferred backward
from this missing field-construction proof.
