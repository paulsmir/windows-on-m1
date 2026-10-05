# Request-scoped UMD draw composer

Baseline: 83e276992fe0d5d00f76d59263fc012a07deadae. Implementation and controlled
Windows tests only. The Full Graphics / standard D3D / DXGI / DWM objective
remains unchanged. No software-renderer milestone replaces that objective.

## Architecture decision implemented

Identity is (monotonic nonzero device OwnerCookie, Generation, Token, Serial).
The existing UMD ScreenBuffers remain the resource authority. Expected size
must match the live slot. Conflicting serials and duplicate kernel handles for
different logical identities reject. Each request assigns dense indices in
first-reference order; typed reference indices and relocation graph edges stay
unchanged. Per-allocation WriteOperation is the OR of typed Write uses. All
other allocation-list fields are zero. Wire access is explicitly translated to
owner GPU permissions; Execute also requires shader class.

Seal owns bounded command bytes and a D3DDDI_ALLOCATIONLIST. It validates with
BuildDrawVersion and the existing KMD AdmissionWin32ValidateReferences before
publishing the transaction. It preserves inputs, including original allocation
indices. Caller serializes source mutation during Seal; afterwards the input
arrays can change without changing sealed command bytes.

SubmissionHolds are separate from CPU SourceHolds, inside the existing owner
records. Acquisition occurs under ScreenBufferLock after all validation passes,
so failed preparation acquires no partial holds. One current transaction per
device/context is allowed. Runtime callbacks run outside ScreenBufferLock.
Map/unmap/destroy/detach and close/finalize consult live submission ownership.
The context transaction also prevents callback reentry into dispatch/retirement.

Dispatch revalidates current owner/context/handles and command graph, copies
exact bytes/list into runtime buffers and invokes pfnRenderCb once. It adopts
returned runtime buffers on success. A rejected Render blocks further draw on
that context, releases temporary allocation holds, and preserves HRESULT.
An accepted Render followed by malformed returned buffers or failed completion
event enqueue is PostError, never replayed. PostError keeps allocation holds.
Retire may enqueue the missing completion event, but never calls Render again.
Timeout retains holds; the signalled in-order context event permits exact release.
Invalid returned buffers leave new draw admission terminal even after retirement.

This conservatively retains allocation identities until completion rather than
assuming late KMD reads have ended at pfnRenderCb return. Holds are not residency
and do not freeze bytes against arbitrary writes through an already-held CPU
pointer. The future native batch producer must serialize those writes and keep
its source bytes stable through this lifecycle. No PA/GPUVA is sent to UMD.

## Verification

Pinned WDK 26100 / MSVC 14.44 on FRYZZING. Final source archive 008:
`c72062c45ddd6153826bb9a8063953ade15ca5eb02cb8267cf68c9f83a99297d`.

- x64 UmdContractTest: build/link + execution exit 0; includes 20 composer
  scenarios, earlier UMD tests, and the real KMD reference validator/materializer.
- ARM64 UmdContractTest: build/link PASS, execution NOT_RUN.
- Optional x64 Mesa factory configuration: build/link + execution exit 0.
- ARM64 UMD DLL: build/link with RunCodeAnalysis=true succeeds; 27 SDK header
  SAL warnings remain. This is not a warning-free analysis or signed package gate.
- Five host suites pass: typed capture/materializer, KMD transport, winsys
  transport, pipe screen, native device.

Executable tests cover nine typed references deduplicated into eight handles;
disjoint vertex/fragment shader subranges in one allocation; a second request
with swapped allocation order and a new fence; exact OR of read/write uses;
stale token/serial/generation/owner; handle alias conflict; range, access, class,
transition and counter exhaustion; invalid draw/relocation; pre-dispatch capacity
failure; explicit abort; callback failure; malformed returned buffers; failed
signal then successful recovery; timeout with retained ownership; duplicate
dispatch/retire rejection; callback reentry and blocked resource operations.
The controlled Render callback materializes the composed command twice, with
placements differing by 1 MiB, and verifies relocated addresses differ exactly.
Kernel facts are expanded per typed reference by the real KMD validator from
the ephemeral allocation list, as the production materializer requires.

Evidence: investigation/evidence/AD04-umd-draw-composer/{x64,arm64,umd-dll,factory}.
Source archive and test binaries are preserved locally/at the builder; logs and
result manifests are committed. Failed fixture 003 used a NULL callback context
and allocation-count facts instead of reference-count facts; fixed in the test
composition without changing the KMD contract. Earlier builds 001/002 preserve
WDK header duplicate-symbol and fixture-name compilation failures on FRYZZING.

## Scope and next integration

The module is compiled in the UMD DLL but is an internal API. No exported DDI
table or optional SubmitDraw provider is activated. Existing AGX_WIN32_DRAW_REQUEST
does not contain the identity sidecar, so it must not be wired by guessing tokens
from old AllocationIndex fields. The next caller must pass actual native-capture
identities to Seal, preserve native BO/source lifetime, Dispatch on the supported
runtime thread, and Retire on the existing completion protocol.

Remaining before that caller can be enabled: actual Mesa agx_bo association and
complete typed native address capture; source-byte mutation discipline; supported
terminal context teardown when completion cannot be recovered; serialization
with any other producer sharing runtime command buffers. A controlled callback
and an in-memory materializer are not native Asahi execution, D3D acceptance,
physical TA/3D, standard Present, DWM, or hardware stability proof.
