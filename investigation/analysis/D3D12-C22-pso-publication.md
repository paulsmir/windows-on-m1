# C22: publish one compiled native result per PSO

WINDOWS CONTRACT: FULL GRAPHICS. Microsoft permits concurrent recording of
*different* command lists. The runtime serializes queue submission, which does
not serialize the recording callbacks:
https://learn.microsoft.com/en-us/windows/win32/direct3d12/design-philosophy-of-command-queues-and-command-lists
and https://microsoft.github.io/DirectX-Specs/d3d/CPUEfficiency.html .
RecordDispatch/RecordDraw invoke actual CompileCompute/CompileGraphics before
submission. Different lists may reference the same immutable PSO.

AGX/ASAHI CONTRACT: the owner returns native state and accompanying bindings,
SRV dimensions and workgroup geometry. These are one result: publishing a status
separately from this tuple can expose inconsistent state or lose native objects.
This is a CPU publication defect owned by the D3D12 DDI; no KMD, firmware,
interrupt, DMA, power, shader model or capability change is required.

TRANSLATION: a per-PSO SRW lock is initialized with the private header. Both
compile helpers acquire it before examining compiled, keep the existing terminal
success/failure cache and release it on both cached and new-result paths. The
native result and metadata are immutable after terminal publication. Destruction
follows normal application/runtime lifetime synchronization. There is no global,
device-wide or context-wide lock. Private-size calculations continue to derive
storage from offsetof rather than a hardcoded byte count.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? EXP1409 actual functions,
using original external constructor/destructor stand-ins, reproduce two native
constructions for one PSO: compute loses one state and graphics loses five.
Another controlled schedule replaces a cached E_FAIL with S_OK. Both initial
reads precede publication; A is joined before B writes, so the counterexample
does not require C++ undefined data races. The GREEN schedule observes a lock
attempt before blocking; it needs neither arbitrary sleeps nor a second
constructor entry. Six unchanged cases check whole tuples, cached failure,
fresh-PSO success and destructor counts.

Verification: RED compile0 plus compute0/1/1 and graphics0/1/1; GREEN sixPASS,
constructor1 and exact retirement. Durable fixture/source renderer/opt-in unittest
and approval pins are in tests/d3d12_pso_publication*. The first durable GREEN
attempt failed at compiler setup (direct Xcode clang lacked SDK sysroot), not
at a functional assertion. Its receipts remain intact. Retry1 explicitly uses
the pinned MacOSX27.0 SDK and passes all six cases under ASan/UBSan. Thirty
ABI/owner tests and allthree Windows syntax configurations pass. Full1581 tests
retain exactly the same119 failure/error signatures as the C21 baseline:
68failures,51errors,7skips; no full-suite GREEN claim.

Receipts: .local/c15-pso-publication-1409/output-manifest.json
SHA391704945a99460e2abf3843d75b712284f2824cdec162779c1f615a9f7f2b3b;
.local/c15-pso-publication-1409-green-retry1/green/output-manifest.json;
.local/c15-pso-publication-1409-fullsuite/comparison.json;
.local/syncheck1409/syncheck.log. Native/GPU checkpoint remains separate.

WHAT IS STILL UNKNOWN: different PSOs still share the owner's pipe_context;
this change does not establish its concurrency contract. No direct source edge
recursively compiling the same PSO was found, and recording error callbacks run
after Compile returns/unlocks. However EnsureOwner/native allocation may invoke
outward runtime/kernel callbacks while the lock is held. Pinned SAL alone does
not establish absence of runtime reentry. This remaining boundary is explicit,
not classified as invalid application behavior. No complete C15 threading or
production-admission claim follows from these tests.

Checkpoint: new fresh-device C22A compute/C22B graphics steps should record two
lists concurrently against one shared PSO, then submit and verify independent
outputs/fences. Existing52 controls remain. Package must differ from accepted
C21 only by this publication fix; native build/sign/hash/stack/BEFORE gates and
normal GPU-visible exact-package rollback are required. No C22 hardware package
or probe is claimed built by this note.
