# Legacy PROGRAM to ephemeral owned NIR

WINDOWS CONTRACT: retain existing CORE0003, shader and PSO allocation/DDI,
model advertisement, root resolution and valid object lifetime. A live PSO
already owns its CS words and `cs_dwords`; no runtime input readability or private
capacity authority follows from this route. Existing PSO lock/cache and owner
association must remain intact when its caller is integrated.

AGX/ASAHI CONTRACT: project `agx_owned_program_nir_native.c` admits explicitly
framed legacy PROGRAM through kind1/stage5, existing DXBC translator and actual
Asahi NIR options. Success owns NIR/map/workgroup and GLSL reference; ordinary
reported failure releases the temporary state. Gallium consumes a cloned NIR on
callback entry. Actual Asahi early NULL consumption repair is separately under
GPU qualification EXP1510; no new backend, BO, firmware, KMD or caps behavior.

TRANSLATION: introduce a genuine opaque legacy compute constructor with an
explicit, uint32_t-aligned driver-owned span and initially empty output. Force
kind1/stage5; publish last; preserve nonempty output unchanged. Its empty root
means UNSPECIFIED translation metadata and is never a compatibility key. Move
existing common lifetime/getters/handoff and the exact private layout into their
own translation unit/header so linking this path does not pull the unrelated
owned-core/provider/library constructor. Preserve old Prepare/range bodies and
layout/destructor byte for byte. No fake provider or resolver is allowed.

Inspected primary files: `agx_d3d12_ddi.cpp` PSO storage/CreatePipelineState,
EnsureOwner, CompileCompute and RecordDispatch; `agx_owned_program_nir.cpp`,
`agx_owned_program_nir_native.c`, `agx_owned_nir_pipe.c`; owner prepared entry in
`agx_d3d12_windows.cpp`; existing owned/pipe host recipes. Accepted bounded source
plan `.local/c15-r0-pso-legacy-prepared-source-plan/PLAN.md` and private proposal
`.local/c15-r0-legacy-prepared1507-prep` are source-only evidence, not experiment1507
(the actual1507 ID belongs to cache qualification). External bodies are not copied.

WHAT IS STILL UNKNOWN: actual no-core/provider link and nominal ownership/map/NIR
parity under the new common unit, old API controls after the split, native ARM64
link closure and actual caller behavior. Adopt only factory/common split now;
leave proposed DDI CompileCompute route unapplied until a typed actual-function
fixture proves copied PSO input retirement, exact CS span, cache-once behavior,
owner creation's real existing-owner branch, callback failure and matching destroy.
No whole upstream OOM, generic bad-pointer or new cross-device lifetime guarantee.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A strong unselected core/provider
link dependency, lost GLSL lifetime, retained input storage, map/workgroup/NIR
semantic drift, wrong legacy profile admission, partial output publication or old
bridge regression after moving common definitions. Actual frontend reference
observer delegates to real functions; it never substitutes a successful compiler.

Checkpoint: host ASan/UBSan true no-core fixture plus old owned bridge and opaque
wrapper controls, fresh metadata/source pin checks; then separately preregistered
native builder link/CPU execution. No new GPU run until the actual caller gate and
full signed-source package provenance pass. EXP1510 runs concurrently on the
previous frozen package; normal exact-package visible recovery owns its rollback.
