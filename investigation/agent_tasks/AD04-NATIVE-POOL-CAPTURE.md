# Native pool / Windows owner / typed capture — 2026-09-13

## Implemented connection

The original pinned Mesa pool.c now calls a Windows BO platform backend that
allocates actual struct agx_bo storage. Its backing comes through the existing
AGX_WIN32_SCREEN -> UMD -> pfnAllocateCb/pfnLockCb contract. Windows ScreenBuffers
assign Token and Serial. Association of the real native BO pointer happens only
after allocation; a backend namespace identifies the native registry entries.
There is no second persistent BO registry.

The OS-private native agx_device header carries windows_private for its Windows
backend. Every Windows native unit uses that same derived header. Native va.addr
and agx_ptr.gpu carry construction coordinates here, never mapped WDDM GPUVAs or
hardware PAs. This is the approved construction-address architecture: final
addresses still require typed relocation and KMD physical materialization.

The caller-serialized backend provides BO creation/map/reference/unreference and
deferred collection. Native pool cleanup releases its native references; capture
has independent references. If Windows submission/source holds prevent release,
the existing Windows registry retains the native BO until collection succeeds.
An unpublished allocation whose rollback fails has one explicit provisional
owner and stops further native allocation; it is retried during collection.
The current backend supports 16 KiB allocation alignment and rejects stronger
alignment. Sharing/import/export and concurrent native operations are not enabled.

NativeBackendCount keeps the internal Windows owner alive until backend detach,
even with zero BOs. Native associations block generic allocation destruction.
Creation now reserves the Windows slot/token/serial before the Allocate callback
and commits or rolls back under the owner lock, with callbacks outside the lock.
This is internal owner lifetime, not permission to return from a runtime-owned
void DestroyDevice while outstanding native work still exists.

AGX_WIN32_ASAHI_CAPTURE has a request-scoped identity table. It queries metadata
from the real Windows owner, then assigns local indices and uses the existing
RetainExact/typed reference API. Unknown BO keys are checked for registry
membership before dereferencing. Capture retains native BO references after
pool cleanup. All backend and capture operations currently require caller
serialization; do not advertise a concurrent implementation.

## Executed proof

The Windows contract executable links original pool.c with the real UMD owner
implementation and controlled Windows runtime callbacks. It executes:

native pool init -> native BO creation -> Windows allocation/map -> two real
pool suballocations -> owner-derived identities -> two typed capture references
sharing one allocation index -> pool cleanup -> capture abort -> delayed BO
collection while a Windows hold is present -> deallocation failure/retry ->
exact final release. A second pool tests Windows allocation failure and null
result propagation. No synthetic draw packet or synthetic identity table is
used in this pool/capture test.

Native pool null-allocation/map paths were missing guards; the build-local
pool projection now returns a null agx_ptr and clears out_bo rather than
dereferencing a failed BO. Broader native draw caller failure propagation is
still required before production enablement.

Source archive015:
61c62cd9839004815efa449e8809d9497b78883d1e445a658fad42e382a37aae.
x64 executable SHA256:
1ad529b5a253c103d3fbbdd2a83891354ba26036f9f49fc159623176c56d1305.
Windows x64 execution PASS, ARM64 native-object + UMD test build/link PASS.
ARM64 execution NOT_RUN. Existing composer tests and five host regressions pass.
Raw evidence: investigation/evidence/AD04-native-pool-capture/.

This proves native pool -> Windows owner -> typed capture. The composer/KMD
materializer test remains a separate proof and must not be concatenated into
a claim that a full native draw has reached the kernel or hardware.

## First remaining native draw boundary

agx_build_pipeline is compiled but has not yet registered its complete graph.
The pinned cmdbuf.xml identifies missing exact field types:

- USC Preshader: 8-byte record, Code at bits32..63, unlike Shader's six-byte
  record with Code at bits16..47.
- USC Texture/Sampler: 36-bit buffer at bits27..62 with shr(3), unlike Uniform's
  38-bit field at bits26..63 with shr(2). Uniform relocation must not be reused:
  that would overwrite count bit26 and apply the wrong range/alignment.

Next implement source-defined typed contracts for these fields with explicit
protocol/version compatibility, then capture the original native state emitter's
BO/ranges and every nested state/resource edge. Mixed native Batch pool roles
must also be reconciled with the existing source-class validator rather than
silently relabelled. Only a complete owner-tracked native draw can be connected
to composer dispatch and the physical path. No hardware candidate is ready yet.
