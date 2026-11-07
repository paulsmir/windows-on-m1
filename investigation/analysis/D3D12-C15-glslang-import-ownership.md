# C15 pinned glslang `Builder::import` ownership checkpoint

Classification: external CPU-only compiler dependency, no production source
or DDI change. Pinned dxil-spirv
`8434b0639fb2a48ce13aac08a420ecce5d23df0e`; its license/dependency
conditions are in [D3D12-C15-dxil-license-review.md](D3D12-C15-dxil-license-review.md).
Primary source:
`third_party/glslang-spirv/SpvBuilder.cpp` SHA256
`90c0b0d19d322f8a8ca62d5f75347fef1f9bdf00b0dd30f7c922602c28e913a3`,
function `Builder::import` actual text SHA256
`55fb38a7712bee94f54cc06dad20a627056c06b7d731170211e5c6e36a9a61f8`.
The function constructs a raw `Instruction`, calls `addStringOperand`, then
adopts it into `imports`. A throw from the operand allocation before adoption
leaves the completed object unowned. The selected compiler originally builds
with `-fno-exceptions`; this is a source-level blocker for a future coherent
exception-enabled recoverable-OOM candidate, not a claim about current runtime
failure behavior.

WINDOWS CONTRACT: a future UMD compiler call must return an explicit failure
and publish no shader on internal OOM. No external C++ exception may cross the
project's C ABI or a VOID DDI. The exact 0010 runtime error callback category
remains separate and unresolved.

AGX/ASAHI CONTRACT: no AGX, Mesa NIR, KMD, m1n1, Mu or Windows guest operation
occurs in this checkpoint. This ownership site is wholly inside the selected
external glslang builder dependency before SPIR-V reaches Mesa.

TRANSLATION: the local candidate changes only the order of ownership at this
site: adopt the new Instruction into a local `unique_ptr` before the
potentially allocating operand setter, then move into `imports` on success.
The original pinned source remains unchanged; generated source-extraction
files and test binaries are retained under
`.local/c15-glslang-import-ownership1687/`, outside Git. No code from this
external implementation was copied into the repository.

EXP1687 uses the actual extracted `Builder::import` function with controlled
Instruction/container stand-ins and a deterministic exception at
`addStringOperand`. Appleclang21 C++14 `-fexceptions` ASan/UBSan: original
RED exited42 after recording `threw=1 live=1 imports=0 cache=0`; an explicit
test observer deleted the otherwise unreachable object after that record.
Candidate GREEN exited0 with `threw=1 live=0 imports=0 cache=0`, then a normal
import/cache reuse passed; stderr empty. Generator and exact source/output
hashes are in `.local/c15-glslang-import-ownership1687/manifest.json`, result
hashes in `results.json`.

WHAT IS STILL UNKNOWN: this extraction does not link or execute the actual
whole glslang builder or selected 83-TU/nine-archive graph with exceptions.
It does not prove the allocator, raw groupedTypes indices, Function early
registration, context destructor ledger, converter publication or C facade
safe. The next distinct checkpoint is the nearest groupedTypes raw index
push-before-owner adoption in `SpvBuilder.cpp`, with a site-specific failure
and ownership oracle; whole-graph fail-at-N and Windows ARM64 unwind policy
remain required before production DXIL/SM6 admission. No J313 hardware run
can test this unlinked CPU ownership site.
