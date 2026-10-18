# ARCHITECT_DECISION — native table source allocation classes

Accepted after bounded Astra High source review and primary-source inspection.
This is an offline reversible integration change, not a production/hardware
enablement. Keep physical/patch-list architecture and all existing caps.

## Contract for implementation

Only for Draw command version exactly
APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_USC (3), permit these reference roles
to use either their existing Encoder source class or a General source class:
Descriptor, Scissor, DepthBias.

The General alternative requires Reference.Access exactly Read and the existing
GpuRead allocation permission. General backing may itself allow GpuWrite; that
does not make this source reference writable. Do not widen Shader, ShaderRodata,
UscPipeline or Encoder roles. Do not change their source classes, final placement,
materializer, GPU MMU, capabilities or overlay enablement.

Pass the command version into the private AdmissionWin32ReferenceClass helper
from its Draw caller in render_win32_transport.c. Use equality with v3, not >=3.
Keep all generation, identity, range, permissions, overlap and display-active
checks. Shared ABI validation remains required before using this transport view.

## Source basis

Pinned Mesa: agx_state.c:2699 texture table and2803 sampler upload (including
custom-border stride); agx_pipe.c:1586 scissor and depth-bias upload;
agx_batch.c:101 batch.pool flags0. Our native BO adapter maps those flags to
General. These are source bytes, not materialized executable destinations.
apple_agx_dynamic_job.c copies according to reference role and exact token/range.
render_dynamic_windows.c verifies the captured source ClassId before resolving
the overlay plan. No destination policy change follows from a source alternative.

## Required tests

For each affected role: v3 General+Read+GpuRead succeeds; Encoder still succeeds.
General remains rejected for v1/v2/unsupported versions, non-Read access and
missing GpuRead. Shader-class table sources and General shader/ShaderRodata/
USC/Encoder sources remain rejected. Exercise disjoint roles within one General
BO with exact identity/range plus stale/range/writable-alias rejection. Preserve
an explicit v3 overlay rejection test. Run current host suites and pinned x64/
ARM64 owner contract builds; no Air action belongs to this change.

One SEPARATE next invariant is already observed: the transport's universal
reference byte-count multiple4 gate rejects native USC38/62 and rodata2. Derive
its native byte-range contract separately; do not silently pad ranges or bundle
it into the source-class patch. Native graph/runtime wiring remains incomplete.
