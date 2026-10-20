# C15 four external ownership candidates in one actual glslang TU

Classification: external BUILD_ONLY checkpoint, no production source or DDI
change. Pinned dxil-spirv commit
`8434b0639fb2a48ce13aac08a420ecce5d23df0e`; licenses and notice
conditions remain the reviewed
[D3D12-C15-dxil-license-review.md](D3D12-C15-dxil-license-review.md).
The original retained source remains byte-identical; the modified checkout
and compiler output are under `.local/c15-oom-actual-tu1693/`, outside Git.

Primary input is the exact host `compile_commands.json` entry for
`third_party/glslang-spirv/SpvBuilder.cpp`, rebased only to a cloned source
root/output and with explicit `-fexceptions` under the same C++14, ARM64
SDK/tool and optimization flags. Four individually tested scratch candidates
were applied to that clone: `Builder::import` (EXP1687), `makeVoidType`
(EXP1689), `Function::Function` (EXP1691), and
`Builder::makeFunctionEntry` (EXP1692). The frozen manifest records exact
source/function/candidate hashes and the full compiler argv. The initial
metadata generation used the `/Volumes` alias while the old argv used the
`/Users/pavel/public_windows` alias; the script was corrected before
preregistration/compilation and the final manifest pins only the cloned
source paths.

WINDOWS CONTRACT: a future UMD C facade may catch C++ allocation exceptions
only if every selected dependency frame unwinds coherently and failed jobs
publish no shader. This macOS ARM64 object compilation checks neither a
Windows /EHsc build nor the `_0010` error callback category.

AGX/ASAHI CONTRACT: no Mesa NIR, AGX shader-state, KMD, m1n1 or Mu operation
is touched. This is one dependency TU before compiler output is used by AGX.

TRANSLATION: EXP1693 actual pinned `SpvBuilder.cpp` compiled with its cloned
patched `SpvIR.h` in one C++14 ARM64 TU, rc0/no diagnostics, object SHA256
`9bf89511958e192bee2961a23a0c0c7e28fa79c4058f28415c580fc39394beab`.
Original and candidate source hashes matched before and after compilation.
This advances the earlier source-extracted stand-in results to actual source
and type/include/codegen compatibility for those four sites. It does not
execute the object, link the selected nine archives, modify all raw grouped
constructors, test Block/Module destructor behavior, or establish internal
OOM recovery. The next justified build boundary is one coherent patch
footprint across the remaining selected owner paths and allocator/context/
C facade, followed by exact 83-TU/nine-archive exception-enabled build and
fault-injected same-thread next-job tests. No J313 hardware run can test this
unlinked external CPU contract.
