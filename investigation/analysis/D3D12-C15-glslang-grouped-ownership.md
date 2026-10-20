# C15 pinned glslang grouped-type ownership checkpoint

Classification: external CPU-only compiler dependency. Pinned dxil-spirv
`8434b0639fb2a48ce13aac08a420ecce5d23df0e`, reviewed under
[D3D12-C15-dxil-license-review.md](D3D12-C15-dxil-license-review.md).
Primary sources: `third_party/glslang-spirv/SpvBuilder.cpp` SHA256
`90c0b0d19d322f8a8ca62d5f75347fef1f9bdf00b0dd30f7c922602c28e913a3`,
actual `Builder::makeVoidType` function SHA256
`975bef41f6f3d4a28aa1183bf698ed2b52f760f14c00746bc6fce7b200a27a97`,
and `SpvIR.h` `Module::mapInstruction` (resizes an ID-to-Instruction vector
before assigning a pointer). Neither external source is modified in place
or copied into this repository. Full license/notice review remains the cited
C15 review, not inferred from this checkpoint.

WINDOWS CONTRACT: a future exception-enabled UMD compiler job must return a
checked internal failure with no published shader. No C++ exception may
cross the C ABI/VOID DDI. This checkpoint says nothing about the still
unresolved `_0010` runtime error category or general OOM recovery.

AGX/ASAHI CONTRACT: no GPU or Mesa NIR work occurs. The selected glslang
builder constructs SPIR-V before Asahi sees a shader; KMD, m1n1 and Mu
cannot repair a raw or dangling CPU ownership record.

TRANSLATION: current `makeVoidType` constructs a raw Instruction, publishes
its pointer into nonowning `groupedTypes`, moves it into owning
`constantsTypesGlobals`, then maps its ID. A throw at the first push leaks
the raw object; a throw at the owner push leaves a stale grouped pointer;
a throw during module map leaves a partially published owner/grouped pair.
The local scratch candidate owns the Instruction in a `unique_ptr` before
any push and rolls back grouped/owner publications if a later step throws.
It changes this one site only; other grouped-type constructors still have
similar patterns and require their own reviewed treatment.

EXP1688 first host fixture stopped at a C++14 test-model initializer error;
no ownership result was inferred. EXP1689 changed only that model qualifier,
retained the exact extracted function and candidate, and ran Appleclang21
C++14 `-fexceptions` ASan/UBSan with three controlled failure sites:

- Original RED rc42: grouped push `live=1/grouped=0/owned=0`; owner push
  `live=0/grouped=1/owned=0`; map `live=1/grouped=1/owned=1/map=0`.
  An explicit test observer cleaned the otherwise unreachable first object
  after recording each state.
- Candidate GREEN rc0: all three throws leave `live=0/grouped=0/owned=0/map=0`;
  normal type creation and cached reuse pass. Both stderr streams are empty.

Generated sources, upstream/source hashes, test-model source and results are
under `.local/c15-glslang-grouped-ownership1689/`, outside Git. This is an
actual-function extraction with model containers, not an actual 83-TU/
nine-archive exception-enabled build, Windows ARM64 PE run or J313 result.
It does not close allocator, context, parser, all grouped-type sites,
Function early registration, converter output transaction or C facade
fail-at-N coverage. Next causal target is the distinct early Function
registration path in pinned `SpvIR.h`, followed by whole-graph ownership and
unwind verification; no hardware experiment is justified here.
