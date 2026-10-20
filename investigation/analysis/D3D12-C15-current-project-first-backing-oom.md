# C15 current owned converter: first backing fault on reviewed composite source

Classification: host CPU compiler checkpoint, no production external source
mutation or DDI/GPU change. Pinned dxil-spirv commit
`8434b0639fb2a48ce13aac08a420ecce5d23df0e` and the already reviewed
composite+profile source under `.local/c15-profile-source1537/external` were
the parent. License/notice conditions remain those in
[D3D12-C15-dxil-license-review.md](D3D12-C15-dxil-license-review.md).
A fresh external scratch copy under `.local/c15-composite-fault1711/source`
added only the separately tested EXP1413 `AGX_C15_BACKING` allocation hook
and its CMake test TU. The reviewed parent remained byte-identical; the
root review compared all 2636 parent files with 2638 scratch files and
pinned the four test-overlay deltas.

WINDOWS CONTRACT: the current original project
`agx_dxil_checked.cpp` provides `noexcept` C-linkage wrappers for the exact
C API calls used by `agx_dxil_owned.c`; an internal exception must not cross
that project C frame or publish a partial owned result. The adapter currently
maps checked parse errors to `AGX_DXIL_OWNED_REPORTED_ERROR` (3), without a
separate public internal-OOM category. This host result does not select a
permitted `_0010` `pfnSetErrorCb` code or advertise shader model6.

AGX/ASAHI CONTRACT: these are CPU shader bytes and binding-map outputs
before Mesa NIR or the Asahi owner. No KMD, UAT, queue, firmware, m1n1 or Mu
work occurs. The result is project-owned SPIR-V after converter/blob/TLS
cleanup; it is not an AGX GPU execution receipt.

TRANSLATION: EXP1711 built the reviewed composite+profile selected graph with
one test-only 64-KiB backing hook. EXP1712 verified nine static archives,
84 objects including the hook and 103 C++ commands all carrying
`-fexceptions` with no `-fno-exceptions`. EXP1713 compiled and ran the
**current project** `agx_dxil_program.c`, `agx_dxil_owned.c`, and
`agx_dxil_checked.cpp` against that graph: all 12 one/mixed/profile commands
rc0, accepted mixed 2152-byte SPIR-V/240-byte map hashes, 2612 profile
checks PASS. EXP1714's initial reviewer accidentally checked no `.d`
files; EXP1715 corrected it and verified 697 actual dependency paths with
UNKNOWN0, exact expected stderr, all parent/scratch files and archive pins.

EXP1716 linked the three current project objects to the nine reviewed
archives with an original scratch C++ caller under ASan/UBSan. On valid
`cs_6_0` input, one targeted first 64-KiB backing failure produced
`first=3 hook=1 empty=1`. Without leaving the thread, a second valid job
produced `next=0 valid=1 spirv_bytes=980`; the caller freed the result and
verified it was zeroed. Process rc0, stderr empty and archive hashes matched
after the run. Exact receipt:
`.local/c15-composite-fault1711/project-fault1716-result.json`. Neither
reviewed parent nor production project source was edited for this test.

WHAT IS STILL UNKNOWN: this proves one allocation site and two sequential
jobs only. It does not sweep ordinary `new`, allocator metadata growth,
parser/context registration, grouped types, Block/Function creation,
converter scan/setters/run/final output, cleanup under persistent failure or
project output allocation. The selected archives were host-built; only
project test objects/caller were ASan/UBSan-instrumented. Windows ARM64
`/EHsc`/`/MD`, signed package, truthful CORE_0010/caps and GPU execution
remain unvalidated. The next causal checkpoint is the **reviewed full
fail-at-N test overlay** across the exact project-used API subset, with
site-classified counters, empty output on every failure and a valid next job
on the same thread. Do not infer general recoverable OOM from EXP1716 or
run J313 to test an unlinked CPU dependency.
