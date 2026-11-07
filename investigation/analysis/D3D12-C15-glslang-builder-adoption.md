# C15 pinned glslang Builder Function ownership interval

Classification: external CPU-only compiler dependency. Pinned dxil-spirv
`8434b0639fb2a48ce13aac08a420ecce5d23df0e`; license and attribution
conditions are in [D3D12-C15-dxil-license-review.md](D3D12-C15-dxil-license-review.md).
Primary sources inspected: `third_party/glslang-spirv/SpvBuilder.cpp` SHA256
`90c0b0d19d322f8a8ca62d5f75347fef1f9bdf00b0dd30f7c922602c28e913a3`,
actual `Builder::makeFunctionEntry` text SHA256
`6979344e3a32bc4574c205a96f4c57af86a5090210c8cef5e99df9db047b322b`,
`SpvBuilder.h` Builder fields (Module before owning Function vector), and
`SpvIR.h` Function/Module destructors. No external implementation is
modified or copied into Git.

WINDOWS CONTRACT: a future recoverable OOM path must not leak a completed
Function when a later precision/decorations/Block/name step fails; no C++
exception may cross the C ABI or VOID DDI. Exact `_0010` error mapping remains
separate.

AGX/ASAHI CONTRACT: this is SPIR-V builder CPU ownership before Mesa NIR and
AGX shader-state creation. KMD, m1n1 and Mu have no role.

TRANSLATION: after a successful `new Function`, current
`Builder::makeFunctionEntry` stores the pointer raw until its last line.
`setPrecision`, decorations, Block construction, name creation and the owning
vector's push may allocate and throw in a future exception-enabled graph.
The local scratch candidate owns the Function immediately with `unique_ptr`,
then moves it into Builder's owning vector after the steps succeed. A normal
return comes from that vector. This corrects the raw lifetime interval, not
Function constructor failure from EXP1690 or all intermediate Builder state.

EXP1692 extracted the exact pinned function with a controlled Builder/
Function model. Appleclang21 C++14 `-fexceptions` ASan/UBSan injected
`bad_alloc` at `setPrecision`: original RED rc42 left `leaked=1 owners=0`
and one nonowning Module registration; the test observer freed the otherwise
unreachable Function after recording. Candidate GREEN rc0 left `leaked=0
owners=0`, and normal creation/destruction passed. Both stderr streams were
empty. Exact generator/source/candidate and results are under
`.local/c15-glslang-builder-adoption1692/`, outside Git.

WHAT IS STILL UNKNOWN: Module still holds a stale nonowning registration in
both failure traces. The selected future policy is to discard the entire
Builder/converter job after a caught exception, never reuse that Builder.
Actual `Module::~Module` does not iterate/delete registered pointers and
Builder declares Module before its owning Function vector, but complete
whole-graph destructor/fault evidence is still required. The `entry` out
pointer, `setBuildPoint`, decorations and Block map may also be partly
published before an exception; a later source/whole-job fault sweep must
prove discarded-job cleanup and no output publication. This site extraction
is not an 83-TU/nine-archive exception-enabled build, Windows ARM64 PE result
or J313 run, and it does not close internal converter OOM.
