# C15 exception-safety checkpoints — external candidates only

WINDOWS CONTRACT: CPU-only compiler jobs must return a checked status and owned
output without exceptions crossing project C frames. These checkpoints do not
admit SM6 or add a Windows DDI. The signed driver and its current nine compiler
archives remain unchanged.

AGX/ASAHI CONTRACT: no GPU access in these tests. Shader publication, Gallium
context concurrency, firmware, allocations in KMD and recovery remain separate
contracts. DDI publication was independently corrected in C22.

TRANSLATION: original harnesses exercise pinned8434 source in separate external
sandboxes. Existing MIT/BSD notices are retained. No external implementation or
patch is copied into Git. Every candidate has its own source/tool/header hashes,
BEFORE, raw return codes and fresh output roots. Host exceptions and sanitizers
apply only to the compiled checkpoint units. They do not relabel the previous
83-TU/nine-archive builds as exception-enabled.

EXP1413 ALLOCATOR_ONLY: actual allocator first64KiB backing failure returned NULL
and retained a failed Block, functionalRED42. Candidate A throws bad_alloc before
vector commit; checks alignment/remaining-space/multiplication, normalizes zero,
and defines TLS/no-TLS/custom-new failure semantics. GREEN verifies reuse/write
in the same arena, huge/direct failure and retry, alignment16, zero, overflow,
new/new-array, reset and fresh context. Returncodes0,42,0,0. Manifest
.local/c15-allocator-candidate-a/EXP1413-artifact-hashes.json
SHA5f1590944fd6e7ed1e2fd82beb1267075b8aab8b9e13938d82608fa7d37b5cf5.

EXP1415 CTX_ONLY: actual LLVMContext creates T before allocating its destructor
registration. The no-TLS target fails the actual first typed pointer-vector growth
after T is constructed: constructed1/destroyed0, functionalRED43 without old UB.
Candidate rollback destroys the current unregistered object while keeping prior
registered objects owned; raw-chain adoption uses a guard and checked arithmetic.
GREEN includes array-prefix registration failure, constructor throw, raw-chain
push failure, alignment64, overflow and fresh context. Returncodes0,43,0,0.
Manifest.local/c15-context-ownership/EXP1415-artifact-hashes.json
SHA92653ec12e3895bb41eb5c9ef70372901df08f6de91408c1cabe18191a87949e.
The tracker counts custom no-TLS allocations only, not all ordinary new calls.

EXP1416 OWNERSHIP_ONLY: exact parse_dxil function extraction with original
controlled parser/metadata stand-ins proves a lifetime/publication defect,
not parser algorithms. Old function orphans parsed on a controlled parse throw:
constructed1/destroyed0/outputNULL, functionalRED44. A test observer frees that
otherwise unreachable object AFTER recording RED; callers cannot use this
observer as recovery. Candidate unique_ptr publishes only on complete success,
clears a valid output slot, retains existing returned error codes and cleans
controlled parse/names/list exceptions. Returncodes0,44,0,0. Manifest
.local/c15-parse-api-ownership/EXP1416-artifact-hashes.json
SHA282b373cb714cd73894fa17ce1196214ceaf306f7acd92dd064d8c6403372ff6.
Actual parser, CandidateA and Context are not linked in this extraction gate.

WHAT IS STILL UNKNOWN: selected glslang builder has additional raw-index versus
owned-object adoption and early Function registration paths. A coherent policy
must close those before a full compiler fault sweep. Then rebuild all selected
83 TUs with compatible unwind flags on host and MSVC ARM64, implement checked
noexcept C++ facade calls for every used C API method (including allocating void
setters and allocator begin), and validate partial destruction plus next-job
success under fail-at-N. Current outward C API exceptions are not caught by the
existing C adapter. Candidate A plus a generic catch is insufficient. No SEH,
longjmp, input-size cap, preallocation or global new-handler substitute is used.

The separate early-R1 runtime contract note establishes ordinary shader/PSO
field forwarding and DXIL PROGRAM framing. Serialization, repeated-index policy,
owned-library lifetime, shared pipe_context/reentry and truthful model/capability
advertisement remain independent gates. Current CORE0003/SM5.1 is unchanged.
