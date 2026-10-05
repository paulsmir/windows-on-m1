# Apple AGX Windows Full Graphics — Pre-Hardware Implementation Plan

**Goal:** Reach one hash-gated hardware candidate whose native Asahi producer
creates a complete Windows-owned typed draw request and reaches the existing
UMD→KMD materializer path without synthetic commands.

**Architecture:** Keep physical/patch-list WDDM, retained-root broker, current
firmware/backend and UMD composer. Port native Gallium runtime source to Windows
through explicit BO/provenance/typed relocation adapters; never import Linux DRM
submission or expose PA/GPUVA to the UMD. Hardware is deferred until all listed
offline gates and package gates have fresh evidence.

## Non-negotiable constraints

- No GPUVA migration, firmware/RTKit/UAT/m1n1/Mu/DCP/capability edits.
- Exact Windows owner Token/Serial/Generation identity; no copied private pages.
- Native source ranges are byte-exact; storage placement is separate.
- No fake renderer, hand-written native draw packet, or Linux DRM ioctl/syncobj
  substitute. The existing UMD composer is the only Windows submission route.
- One causal variable per hardware EXP; preserve recovery package and evidence.

### Task 1: Runtime compilation closure

**Files:** `mesa/scripts/build-asahi-runtime-closure.py`; derived source only.

1. Continue the source-preserving runtime probe in Meson file order.
2. For each first error, add only a pinned declaration/include portability
   overlay when it has no runtime behavior; compile again immediately.
3. When a Linux queue/resource/DRM operation is reached, stop that unit and
   route it to the existing Windows owner/composer abstraction instead of stub.
4. Require x64 object compilation for every runtime unit; ARM64 build/link after
   a coherent runtime group closes.

### Task 2: Initial batch and encoder ownership

**Files:** derived `agx_batch.c`; `agx_win32_asahi_bo.[ch]`; native owner tests.

1. Keep `AgxWin32AsahiEncoderCreate` only at original `agx_encoder_allocate`.
2. Verify Encoder class, map/unmap/destroy, current owner identity and cleanup.
3. Reject General-backed encoder rollover in `agx_ensure_cmdbuf_has_space` until
   an explicit stream-link ownership contract exists.

### Task 3: Native state graph capture

**Files:** derived `agx_state.c`; `agx_win32_asahi_pipeline.[ch]`.

1. Capture parent VDM interval only after `!ctx->dirty` no-op guard.
2. Capture nested vertex/fragment USC, linkage, PPP, scissor and depth-bias
   source ranges at their source-defined allocation sites.
3. Add typed relocation only where primary packed field decoding proves it.
4. Reject indexed/indirect/GS/tess/query/scratch/background/store edges until
   each has an explicit owner/range/role model.
5. Execute a real derived native batch fixture; verify parent/child scopes,
   exact references, source holds and no-op/retry/abort behavior.

### Task 4: Resource and output graph

1. Associate Windows render target, vertex/index and descriptor resources with
   native source addresses through current owner identity.
2. Add exact allocation access/read-write rules and reject stale/reused ranges.
3. Prove resource mapping, materializer destination placement and source byte
   immutability through ordered completion/retirement, not sleep.

### Task 5: Native capture → composer adapter

1. Build `AGX_WIN32_DRAW_REQUEST` only from complete captured roles/references.
2. Pass exact per-reference identity sidecar to `AdmissionUmdDrawSeal(v3)`.
3. Verify fresh request allocation indices, `pfnRenderCb` exactly once, accepted
   Render post-error behavior, fence marker and retirement holds.
4. Execute two requests with different placements; reject stale owner/generation,
   incomplete graph and duplicate/overlapping writable references.

### Task 6: KMD materializer and offline end-to-end proof

1. Route sealed native command through existing KMD validator/materializer.
2. Verify every reference resolves exact source bytes and relocation target;
   no untracked construction pointer remains.
3. Validate two physical placements, source hashes, materialized hashes, typed
   relocation encodings and cleanup.

### Task 7: Pre-hardware package gate

1. Freeze source archive/manifest, commit hashes and dependency versions.
2. Run x64 and ARM64 relevant tests/builds; full KMD/UMD build, analysis,
   Universal validation, Inf2Cat/signing/version/hash.
3. Write EXP preregistration: hypothesis, one changed variable, recovery path,
   package SHA, expected native→composer→KMD receipt and failure criteria.
4. Confirm clean GPU-visible baseline, SSH/proxy control planes, recovery artifact
   and no staged AppleAgx package before any install.

## Hardware plan after pre-hardware completion

1. Stage exact signed package and validate hashes on Windows.
2. One natural bind, collect UMD/KMD/native/materializer/fence receipts.
3. If Render reaches physical submission: require real TA/3D completion and exact
   Windows fence; otherwise fix only first named boundary.
4. Preserve raw evidence, clean exact package/recover ordinary baseline after a
   rejected candidate; never repeat a candidate without new causal change.
5. After first native draw/fence continue repeated submits, render correctness,
   Present/DCP, desktop, OpenGL and CS 1.6 acceptance as separate experiments.
