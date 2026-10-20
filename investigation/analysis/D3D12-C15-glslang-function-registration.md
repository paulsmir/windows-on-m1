# C15 pinned glslang Function constructor registration boundary

Classification: external CPU-only compiler dependency. Pinned dxil-spirv
`8434b0639fb2a48ce13aac08a420ecce5d23df0e`; licensing and complete
attribution conditions remain those in
[D3D12-C15-dxil-license-review.md](D3D12-C15-dxil-license-review.md).
Primary sources inspected: `third_party/glslang-spirv/SpvIR.h` SHA256
`77844d71efa0428e4b47b22a272de89297148603511c543ebb288c64cb8e4bae`,
actual `Function::Function` text SHA256
`983e4756b599f9aac6421897347ba48d04ddeb217df63eafb3cb0afdb11a775d`,
`Module::mapInstruction/addFunction` and Function destructor in that header,
plus `SpvBuilder.cpp` `makeFunctionEntry` and Block creation. No external
source was modified or copied into the repository.

WINDOWS CONTRACT: a future DXIL compiler job inside the UMD must fail with
no published shader if C++ object construction exhausts memory. Exceptions
cannot cross the C ABI/VOID DDI; the exact `_0010` runtime error category
remains an independent gate.

AGX/ASAHI CONTRACT: this graph is wholly before SPIR-V-to-NIR and Asahi
shader-state creation. KMD, AGX hardware, m1n1 and Mu cannot repair a
Module pointer to an incompletely constructed CPU Function.

TRANSLATION: the current Function constructor maps its member OpFunction
Instruction and registers `this` in Module before constructing parameter
Instructions. If a later parameter construction throws, C++ destroys
constructed members but does not invoke Function's destructor. Earlier raw
parameter Instructions remain allocated, while Module retains nonowning
pointers to the failed Function/member/parameter. Builder::makeFunctionEntry
also holds the completed Function raw through precision/decorations/Block/name
steps before adopting it into an owning `unique_ptr`; a constructor-only fix
cannot establish the full job contract.

EXP1690 extracted the actual pinned constructor into a controlled
Instruction/Module model, injecting `std::bad_alloc` on the second parameter.
Appleclang21 C++14 `-fexceptions` ASan/UBSan returned diagnostic rc42:
`threw=1 leaked=1 mapped=2 functions=1`. The test observer freed the one
unreachable parameter after recording RED. Manifest and raw result are under
`.local/c15-glslang-function-registration1690/`, outside Git. This is a
source-extracted model result, not actual whole-library execution.

WHAT IS STILL UNKNOWN: a coherent production correction needs an owning
local guard for every parameter until constructor success, and ownership of
the Function in Builder before any later allocating step. It must define
whether a caught exception discards the entire Builder/converter job or
whether Module maps/functions must be rolled back for same-Builder reuse.
The current C15 OOM plan selects whole-job abort, so stale nonowning Module
indices may be discard-only **only after** actual destructor behavior proves
that cleanup never dereferences them. Module::mapInstruction may allocate by
resizing its pointer vector; Block construction and Builder decoration/name
steps create further throw sites. A two-phase Function/Module/Builder
transaction or explicit rollback is required before any GREEN claim. The
next smallest discriminator is an actual-function candidate with parameter
RAII and delayed Module function-list publication under controlled failures,
followed by a separate Builder adoption test. Only after those should the
selected 83-TU/nine-archive graph be rebuilt with coherent exception flags
and whole-job fail-at-N. No J313 run is justified for this CPU defect.
