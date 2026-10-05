# Actual native agx_build_pipeline capture — 2026-09-13

## Implemented and executed

The hash-checked Windows projection of the pinned Mesa agx_build_pipeline now
records typed references at its actual USC emission points: texture table,
sampler table including custom-border stride, push uniforms, shader rodata,
unlinked or linked shader code, and preshader code. The shared generated
agx_win32_pipeline.inc contains this one original function body plus capture
hooks; both agx_state.c and the focused contract TU compile that same body.
This avoids a replacement test producer and avoids MSVC resolving unrelated
Gallium/NIR symbols for the focused test. The full native state object is also
compiled. The immutable Mesa checkout is unchanged; its MIT notices retained.

The per-call CPU emission scope is separate from shared Mesa/GPU structures.
Activation requires a recording, caller-serialized capture on the native
backend. Begin verifies CPU pointer and construction range against the existing
Windows owner. Record verifies the actual emitted pointer and source range,
then uses existing RetainExact/typed capture. Finish bounds the USC reference
to emitted bytes. Errors abort source holds rather than returning a successful
pipeline. Active capture/emission prevents backend detach; no scope is a
residency pin or a render/display fence.

The test executes the original function with real Windows-backed native BO/pool
objects and owner-generated identities through actual UMD owner functions.
External Windows runtime callbacks, shader binary/info and descriptor contents
are controlled inputs. It is NOT an actual NIR draw, pfnRenderCb submission,
physical shader execution, image-correctness or hardware readiness proof.

## Causal corrections and review

Initial execution rejected overlapping shader/rodata references: the hook had
incorrectly taken entrypoint-to-binary-end as the code extent. Native compiler
agx_compile.c:3467 places rodata before main code and records main_size at3775.
The hook now uses main_size; the preshader stops at the next main/rodata/binary
boundary. Native linker uses a distinct linked code BO, which the fixture now
models. Existing overlap guards remain unchanged. Source022 execution evidence
retained; its first PowerShell stderr handling lost output, so the same offline
executable was rerun with separate stdout/stderr collection. This was not a
hardware experiment or a GPU verdict.

Astra High read-only review found linked scratch was not rejected when only
the prolog/epilog needed it. Independently confirmed in agx_linker.c:111-113,
191-195. Source025 negative test with cs scratch zero and linked spill_size1
returned a pipeline (RED). Source026 decodes linked REGISTERS and rejects that
case before accepting the pipeline (GREEN). Main/preamble scratch also remain
fail-closed until their graph is implemented; no successful scratch stub.

## Exact verification

Source026 SHA256:
5b3c99d1d9d4ef71b6c2cd4cc2d8359ff4f86a6b5c9dce48d61e1127de0e7a26.
Windows x64 test SHA256:
5712578340df35f794b702abf69bd0d77db1fc097b16dff9dac9fe51125844db.
Windows ARM64 test SHA256:
25ae835181228027cfad5af9a95bdee96b036207a211f1edd3660ab8786f8c5e.

x64 build/link/execution PASS. ARM64 build/link PASS; execution NOT_RUN.
Eight related host regression suites PASS. Test outputs:
Composer_failures=0; NATIVE_POOL_WINDOWS_OWNER errors=0;
ACTUAL_NATIVE_BUILD_PIPELINE errors=0.
Raw source/overlay hashes, commands, logs and results:
investigation/evidence/AD04-native-pipeline-capture/.

Positive tests cover unlinked shader and linked shader with texture/sampler/
custom-border/push tables and split rodata; exact offsets, kinds, source
Token/Serial and byte ranges are asserted. Negative tests cover mismatched
CPU/construction view, encoded pointer mismatch, absent capture, untracked push
table and linked-only scratch. Abort/deactivation and final allocation cleanup
are verified through the Windows owner. No package/install/Air action.

## First remaining integration

Native General pool descriptor tables conflict with the earlier Encoder-only
source-class validator. Resolve that distinction source-first; do not relabel
BOs or silently widen shader/USC executable roles. Nested descriptor/resource
edges, complete native draw graph and source-byte freezing remain required.
Legacy v1-only overlay, dynamic DMA job kinds/layout, actual supported runtime
composer dispatch and terminal retirement must be integrated coherently before
a native hardware candidate. Existing synthetic helper tests must not be
concatenated into a claim of full native Windows draw execution.
