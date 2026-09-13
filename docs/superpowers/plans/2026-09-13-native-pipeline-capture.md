# Native pipeline capture implementation plan

Execution: inline, existing isolated integration worktree. User authorizes
reversible architectural decisions without intermediate approval gates.

Goal: connect the actual pinned agx_build_pipeline producer to owner-derived
typed references. Not another fabricated draw producer or hardware claim.

Architecture decision: use the existing Windows owner BO registry as the only
authority for construction-address lookup. Reject unknown/stale ranges before
reading native BO fields. Record typed edges at source-defined native emission
points, before returning the pipeline for command publication. Do not alter
shared agx_usc_builder layout or invent persistent WDDM GPUVA semantics.

Primary input already inspected: Mesa 9aa1215 cmdbuf.xml, agx_usc.h,
libagx_dgc.h USC macros, agx_state.c:2873 agx_build_pipeline; our Windows native
BO/capture, UMD owner, class validator and v3 materializer. Windows retains
allocation ownership; native BO retains construction provenance; capture holds
sources; KMD resolves physical facts. Firmware/broker/MMIO/display unchanged.

## 1. Exact address-to-owner bridge

- [x] Extend agx_win32_asahi_bo.[ch] with AgxWin32AsahiFindAddress(backend,
  owner, generation, address, bytes, bo_out, offset_out). Enumerate NextBo,
  validate owner registry membership/namespace/generation first, check live
  native identity and bounded construction range, reject ambiguous matches.
- [x] Extend agx_win32_asahi_capture.[ch] with CaptureAddress using this resolver
  and existing CaptureReference/RetainExact. No second persistent registry.
- [x] Extend real pool/Windows-owner executable tests: exact base/subrange,
  cross-end/overflow/unknown address, wrong owner/generation, retired BO, and
  unchanged capture counts/holds after rejected lookup. Run RED then GREEN.
- [x] x64 execute and ARM64 compile/link; commit with raw evidence and ledger.

## 2. Actual native pipeline emitter

- [x] Connect bounded per-call capture scope to original agx_build_pipeline in
  the hash-checked build-local source projection, not the immutable reference.
  Capture texture/sampler, push uniforms, shader rodata, linked/unlinked shader,
  preshader edges with exact native record offsets and native source extents.
- [x] Full source-pointer ownership must survive original pool cleanup. Scope
  failure aborts the request; unknown references cannot become successful draw.
- [x] Exercise original function with actual Windows-backed native allocations.
  Keep incomplete descriptor/resource/scratch graph and legacy overlay disabled.

No hardware before complete native graph, source-class/placement contract,
runtime-thread composer dispatch/retirement and normal package gates. Future
checkpoint is one complete native Windows draw; established GPU rollback remains.
