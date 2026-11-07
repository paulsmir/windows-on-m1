# C15 first-backing-failure parse facade checkpoint

Classification: external compiler plus original project test facade, CPU-only;
no production driver or external source is copied into Git. Pinned
dxil-spirv `8434b0639fb2a48ce13aac08a420ecce5d23df0e`, licenses and
attribution reviewed in
[D3D12-C15-dxil-license-review.md](D3D12-C15-dxil-license-review.md).
Primary sources inspected: pinned `util/thread_local_allocator.cpp/hpp`,
`bc/context.cpp/hpp`, `dxil_spirv_c.cpp` `parse_dxil_blob`, their prior
EXP1413/1415/1416 fault/ownership candidates, and EXP1704 actual linked C
API fault receipt. The four earlier glslang ownership site candidates from
EXP1693 remain in the scratch parent graph.

WINDOWS CONTRACT: an in-process UMD C facade must report a checked internal
failure without letting C++ exceptions cross project C call frames or
publishing an incomplete shader. This checkpoint has a private status
`-1001` for `std::bad_alloc`; it does not choose the still-unknown allowed
`pfnSetErrorCb` code for `CreateShader_0010` or ship a Windows /EHsc PE.

AGX/ASAHI CONTRACT: no Mesa NIR or AGX GPU work is performed. KMD, m1n1 and
Mu cannot repair an exception from this user-mode parser. The output is only
a parsed CPU blob; converter creation/run and shader-state publication are
separate.

TRANSLATION: EXP1701 first built the partial allocator-A graph with one
site-specific 64-KiB backing-allocation hook, nine archives/84 members/103
C++ commands all `-fexceptions`. EXP1703 preserved the normal 980-byte
SPIR-V and negative remapper result. EXP1704 then observed one targeted
backing failure escaping the existing external C API as `std::bad_alloc`
(`hook_failures=1 blob=0`, caller observer rc42).

EXP1705 cloned that graph outside Git and added the previously isolated
LLVMContext ownership candidate plus a narrow `unique_ptr` publication
change to `dxil_spv_parse_dxil_blob` only; its output slot is cleared before
work, and the completed blob is released to the caller only on success.
EXP1706 independently checked nine archives/84 members including hook, 103
exception-enabled C++ commands and source pins. EXP1707 compiled an original
scratch C++ `noexcept` facade around only this parse function, linked the
nine archives, and ran a valid DXIL container in a child process under
Appleclang21 ASan/UBSan caller instrumentation. The first targeted backing
failure yielded `first=-1001 hook=1 blob=0`; reset/end followed by a fresh
context and the same valid input on the **same thread** yielded `next=0
blob=1`. Process rc0 and stderr empty; exact input/archive/output hashes
are in `.local/c15-oom-graph1705/facade1707-result.json` and its parent
manifests. The original pinned source is unchanged.

WHAT IS STILL UNKNOWN: this is one allocation site in one parse entry. The
other C API parse/reflection entries, allocator metadata ordinary new,
converter creation, resource scan, remapper/entry setters, run/finalization,
C callback behavior, ownership of every grouped type/Block/Function path,
normal and exceptional destructor graphs, and project output allocation
remain outside the facade. The nine archives were built on host, not Windows
ARM64; only the scratch caller was instrumented with ASan/UBSan, so no
whole-library leak/UB claim follows. The next causal checkpoint is a
reviewed facade for the exact project-used C API subset with fail-at-N across
parse/scan/create/setters/run/output, empty public result and a valid next
job on the same thread. Only after actual selected graph and Windows /EHsc
passes may DXIL/SM6 admission be considered. No J313 hardware run can
resolve this unlinked internal OOM contract.
