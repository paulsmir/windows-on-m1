# C15: pinned CPU operations around the shader-input provider

## WINDOWS CONTRACT:

Device callbacks are not globally serialized by the Direct3D 12 threading
contract. The original provider935631c8 explicitly required caller serialization;
wrapping its complete Calculate/Copy call in a mutex would also hold that lock
across core/resolver/error-sink calls. This revision separates those phases.
It remains unlinked and establishes no Windows callback-duration readability,
Calc/Create capacity correlation, VOID-DDI error policy or teardown guarantee.

The bounded source plan is `.local/c15-concurrent-cpu-domain-design/design.md`,
SHA-256 `53ba6db381e2270ed6d46a5ab6025eb68db70092ffcf1be925cda13b9605a6de`.
Its companion source pins are
`5327310aabb4a3997ace33d97b176b1af2a9d401b9e8133798a6d2cd280d0369`.

## AGX/ASAHI CONTRACT:

This is a UMD CPU lifetime domain. It does not allocate GPU memory, synchronize
GPU execution or change Asahi/KMD/firmware behavior. The existing owned core,
parsers, zero-cache library and legacy fixtures remain authoritative and unchanged.
No second parser, registry or replacement core is introduced.

## TRANSLATION:

One registry mutex protects list/state/reference transitions. BeginOperation
pins immutable lease/root/device state and creates a one-use operation:
Ready -> Running -> Finished -> Ended. Actual core calls and error sinks execute
outside the mutex. Running lasts through sink completion and final publication;
End while Running and ReleaseInput with outstanding operations return Busy.
Borrowed PROGRAM/signature arrays stay stable until a successful ReleaseInput.

Root retirement closes new acquisitions while existing leases keep copied root
bytes. Device retirement closes new admissions; previously admitted CPU work may
finish. Known-lease retirement failures still report as the old serialized API
did. A separate BeginQuiesce closes sink admission. TryQuiesce succeeds only after
operations, scopes, preparing work and previously admitted sink tickets drain.
CPU clients retain sink/context until that point. This is not permission to wait
inside a Windows DestroyDevice callback.

The new pinned resolver requires an operation/report/device scope and returns
that pinned generation, even if a raw handle is reused. It has no unpinned
fallback. Explicit device scopes bracket new concurrent library callers and
require same-thread LIFO release. The old borrowed resolver remains a separate
legacy caller-serialized API; its returned pointer has no concurrent lifetime
promise. Same-domain mutation during core/sink reentry refuses before locking.

Raw provider entry lifetime remains external: final Destroy must not race an
unretained caller, including pre-admission registration. Busy checks do not make
a freed raw provider pointer safe. After quiescence, already copied CPU bytes may
be inspected; validity of a runtime shader token or owner identity after device
death is not established by these bytes.

## WHAT IS STILL UNKNOWN:

Production callback borrows, device/runtime context lifetime, stable private
capacity, runtime resolver bridging and failure reporting remain open. Legacy
library clients retain their existing serialized contract. No blanket promise
of concurrent safety for arbitrary uses of every API, native linking, allocation
failure recovery, DDI registration or SM6 capability follows from this checkpoint.

## Checkpoint and recovery

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Release/retirement invalidating
admitted work, handle-generation confusion, registry locks spanning outward
callbacks, premature sink/context release and early owned-output publication.
The original latch fixture calls the actual core and zero-cache implementation.
It demonstrates progress of another actual core while a resolver is paused,
parallel independent operations, root/device reuse, Busy behavior, retained
reporting until explicit quiescence, callback drain and owned-byte independence.
No timing sleeps, invalid-pointer probes or allocation fault injection are used.

EXP1443 private execution returned zero for all ten commands with empty stderr:
old core114467, continuity424029 and provider79 markers stayed unchanged; the
new fixture reported 121 checks. All 70 direct pins matched; this is not a full
transitive SDK-header inventory. Result:
`1807393c284b966cd0d6f51c56df8d36d1de7dceb4192032206d28cf9b92b7c0`.
CURRENT ROOT new fixture passed in 1.874 s; unchanged provider79 and root-key979
also passed in 1.988 s and 2.112 s. Default discovery skips before tools.
Native objects and one combined full-suite comparison are separately recorded
gates. No hardware experiment is justified for this CPU state machine.

## Combined source-suite checkpoint

The one full-suite run covered both independent unlinked NIR-map and provider
phase changes: 1611 tests in 221.143 s, exactly the prior 119 failures/errors
(68 failures, 51 errors), no added or removed failures, 15 skips, all 12 source
pins unchanged. Log SHA-256:
`2d9fa19eb299282741d0608a8d210f217271234839c6172dfaa4859fb46084ec`.
This is an unchanged-baseline comparison, not whole-suite GREEN. The two
implementations are committed separately; the suite refers to their combined
source tree rather than asserting two separate full-suite executions.

## Native object gate

Four exact CURRENT ROOT translation units compiled as ARM64 objects with MSVC
14.44, actual SDK/WDK 26100, W4/WX and NOMINMAX: provider, owned core, the new
scoped-API consumer and the unchanged old ABI consumer. All returned zero with
empty stderr and no timeout. Actual dependency closure and 4906 post-pins
matched; the controller independently verified all four fetched COFF machines
as AA64. The new consumer uses the same local C4201 guard around the WDK
include as existing consumers, added before its first native attempt.
Manifest SHA-256:
`53b6eb2f857cd5c13c6c2b5d72cbf29d03f03a601cbcaf077964fc248a068ca5`.
No linking or object execution occurred; native runtime ownership and concurrency
remain unproved. The earlier private syntax-only proposal is preserved separately.
