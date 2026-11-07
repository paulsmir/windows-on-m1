# C15 reviewed composite: observed ordinary-new failure sweep

Classification: host CPU test overlay on pinned, reviewed dxil-spirv
`8434b0639fb2a48ce13aac08a420ecce5d23df0e` composite+profile source.
The parent source and current project facade/owned adapter remain unchanged;
licensing and notices remain those in
[D3D12-C15-dxil-license-review.md](D3D12-C15-dxil-license-review.md).
An original executable-only `operator new/new[]` replacement uses `malloc`
and fixed TLS counters/PC storage; it is a fault-injection observer, never a
production allocator or global-new workaround.

WINDOWS CONTRACT: the current checked facade may catch `bad_alloc` and the C
owned adapter must publish no result on failure. Its host status3 is an
internal reported-error category, not a proven `_0010` `pfnSetErrorCb` code.
The macOS executable interposition does not prove Windows ARM64 `/MD` symbol
resolution or exception/runtime behavior.

AGX/ASAHI CONTRACT: the jobs convert DXIL to owned SPIR-V and binding maps
on CPU. Mesa NIR, Asahi shader state, KMD, m1n1 and Mu are not exercised.

TRANSLATION: EXP1721 calibrated one-key at 9 and mixed-five-key at 10
ordinary-new calls during successful jobs. EXP1722 checked that the
executable defines `__Znwm` and `__Znam` and all 19 observed return PCs
resolve to selected dxil-spirv/LLVMBC paths, including
`std::vector<ChainAllocator::Block>` metadata; normal outputs remained
980/2152 bytes. EXP1723 failed N=1…9 and N=1…10 once per child, with
status3/empty result and valid next same-thread job in all 19 cases.
EXP1724 independently checked all 21 baseline+injection receipts.

For cleanup under persistent failure, EXP1731 first calibrated return-PC
membership in each child, then kept a failure armed at the first selected
ordinary-new site through job return. EXP1732 extended that to N=1…9 and
N=1…10, calibrating a successful job in **each** child before injection;
unmatched PCs were allowed rather than risking C++ EH/observer allocations.
Every one of the 19 failed jobs returned status3 with a byte-zero result,
one selected-site failure and no unmatched ordinary-new call before job
return; after the fault was disabled, a valid same-thread job returned
980/2152 bytes. All stderr streams were empty. EXP1733 independently
verified every line, status, site symbol and archive pin:
`.local/c15-composite-sweep1717/root-review1733.json`.

WHAT IS STILL UNKNOWN: these finite sweeps cover only ordinary-new calls
actually observed for two fixture jobs on this host. Some selected library
allocation sites may be class-specific/custom and bypass global new; untested
control-flow, huge/no-TLS, parser/reflection variants, output-size growth and
C++ EH emergency paths remain. The selected nine archives were not uniformly
ASan/UBSan-instrumented, and host executable symbol interposition does not
establish Windows ARM64 `/MD` interposition. The next distinct gate is an
exact Windows-builder/guest CPU test overlay or an independently reviewed
ordinary-allocation trace for the full selected graph, followed by complete
same-thread fail-at-N for the exact project-used API subset. No CORE_0010,
SM6 or J313 GPU support follows from these host tests.
