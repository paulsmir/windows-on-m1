# ARCHITECT_DECISION — variable native pipeline graph

Current first producer integration boundary, after typed capture/USC correction.
No new hardware verdict and no Full Graphics acceptance implied.

REFERENCE: pinned Mesa agx_state.c agx_build_pipeline allocates variable-sized
USC data through a pool per compiled stage. pool.c already has the with_bo variant
that returns exact BO provenance. Vertex and fragment pipelines are not defined
by a universal64-byte concatenation split.

OUR BEHAVIOR: render_dynamic_overlay.c defines COMPACT_SPLIT=0x40 and
NATIVE_SPLIT=0x1000 and remaps a single Draw.UscPipelineReference using that fixed
layout. It is an existing fixture/EXP208 integration contract, not a general
native Asahi batch format. Reusing it unconditionally would misplace a larger
vertex pipeline or independently allocated fragment pipeline.

DECISION: retain v1 for the existing proven path. Design and implement a separate
versioned native draw/pipeline description carrying explicit stage references
and offsets, with typed capture and the existing materializer as shared pieces.
No integer scanning, concatenation at a guessed boundary, persistent Windows
GPUVA, capability changes or wholesale backend replacement. Native pointers
must retain allocation/owner/subrange provenance at their actual emitters.

NEXT EXECUTABLE ACTION: inspect current Win32 wire validation and overlay consumers
for the single-pipeline assumption; specify the smallest versioned extension;
add a composition regression with distinct variable-sized VS/FS pipeline sources
and independent placements; implement this owning contract without changing v1.
Then wire the native BO/pool/USC emitter and Windows platform operations used by
real agx_screen/agx_context. All ordinary reversible decisions stay internal.

This is not permission to describe a synthetic encoder/callback stub as native
draw execution. Full mission and autonomous hardware gates remain in force.
