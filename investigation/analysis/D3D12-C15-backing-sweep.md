# C15 reviewed composite: bounded 64-KiB backing-allocation sweep

Classification: host CPU test overlay on the reviewed pinned dxil-spirv
`8434b0639fb2a48ce13aac08a420ecce5d23df0e` composite+profile source.
Licensing/notice terms remain those in
[D3D12-C15-dxil-license-review.md](D3D12-C15-dxil-license-review.md).
Neither reviewed external parent nor production project source is modified.
A separate scratch clone under `.local/c15-composite-sweep1717/source` changes
only the original test allocator hook to count/fail the Nth matching
`AGX_C15_BACKING` 64-KiB allocation; it preserves the production allocator
semantics and adds no fault dispatcher to a production package.

WINDOWS CONTRACT: the current original project checked C++ facade must
contain an internal exception and let the C owned adapter return a failure
with an empty public result. No `_0010` callback or public SM6 capability
is active. The host status `AGX_DXIL_OWNED_REPORTED_ERROR` (3) does not by
itself select a permitted WDDM `pfnSetErrorCb` code.

AGX/ASAHI CONTRACT: this exercises CPU parsing/conversion and owned SPIR-V
without Mesa NIR, Asahi shader-state, KMD, m1n1 or Mu. The following valid
job on the same thread checks TLS/job cleanup rather than GPU execution.

TRANSLATION: EXP1717 built the scratch composite+profile graph with the
Nth-hook overlay, and EXP1718 independently checked nine archives/84 object
members including the hook, 103 exception-enabled C++ commands and all
source/archive hashes. EXP1719 calibrated a successful one-key job at **3**
matching 64-KiB backing allocations and mixed-five-key at **5**. It then
ran each N=1…3 and N=1…5 in a separate child process, one injected failure
per job. All eight returned status3 with hook=1 and byte-zero public result;
each next valid job on the **same thread** returned status0 with 980/2152
bytes of owned SPIR-V and 1/5 resource keys. All ten child runs returned0
with empty stderr. EXP1720 independently checked every output line, run ID,
archive/source hash and stderr receipt:
`.local/c15-composite-sweep1717/root-review1720.json`.

WHAT IS STILL UNKNOWN: this is a finite sweep only over the observed custom
64-KiB backing allocations for two fixtures. It does not inject failures in
ordinary `new/new[]`, allocator metadata vectors, huge/no-TLS allocation,
constructor/Block/Function state, converter output or project malloc; the
selected archives were not all ASan/UBSan-instrumented. No Windows ARM64
`/EHsc`/`/MD`, signed package, CORE_0010/SM6 or J313 validation follows.
The next distinct checkpoint is to prove that a **test-only**, job-scoped
ordinary-new hook actually intercepts selected library allocations without
capturing C++ runtime/observer operations; only then can an ordinary-new
fail-at-N sweep be designed. A global new handler is not a production
workaround or evidence of universal interposition.
