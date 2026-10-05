# R141 UMD attachment envelope implementation plan

> **For agentic workers:** execute inline with superpowers:executing-plans; user explicitly requests execution, no Air or package.

**Goal:** reproduce and correct EXP855D's Parse/Invalid in its owning producer; classify the saved Explorer AV and shutdown delay offline.
**Architecture:** keep AGX4 v3/private leases and strict KMD validation. Exercise real Mesa serialization instead of a handwritten attachment packet.
**Tech stack:** C, Python unittest/ASan/UBSan, matching ARM64 PDB/CDB on Windows builder.
**Spec:** `/Users/pavel/public_windows/.local/tandem/NEXT_TASK_R141.md`.

## Source contract and ownership

Inspected saved EXP855D receipt/state, source/provenance manifests and current compact state; no live-state query is needed or authorized. Asahi primary `include/drm-uapi/asahi_drm.h:625-688,745-778`, `agx_batch.c:825-860`, Linux `queue/mod.rs:879` specify synthetic attachment barriers NONE (0xffff), attachment Pad/Flags MBZ. Current UMD append_attachments emits zero barriers and leaves MBZ members uninitialized. Shared parser validates the correct contract. R137 joined replay calls only production prepare_process_buffers and hand-authors a correct attachment packet, hiding serialization defects.

Current m1n1 `src/hv_agx_gpuva_v5.c` owns validated process roots, leases and GPU access after KMD admission. Mu generated `J313AppleAgxAbiAdmission.asl.inc` exposes APPL0002/reserve/resources. Neither interprets this CPU envelope. No firmware/power/IRQ/DMA-map change is indicated. Windows SubmitCommandVirtual passes UMD byte count separately from KMD capacity (Learn DXGKARG_SUBMITCOMMANDVIRTUAL); 480=200+280 fits capacity331776. UMD owns serialization; KMD owns validation/private storage/scheduling/completion and recovery; runtime owns callback delivery. EXP855D full-owner Code0 and recovered ordinary EXP377/392 remain reference contracts.

## Steps

- [x] Decode all receipt fields and preserve hashes; symbolize saved full dump with exact 855D PDB.
- [x] Add production append_attachments/append_native to joined replay (same KMD escape/parser/builder); exact 0x3b0000/280/480/331776 regression plus alternate geometry/VA, negative barriers/MBZ, profiles16/64. Demonstrate RED before changing UMD. Test would fail on missing NONE or missing zero initialization.
- [x] Initialize attachment array and assign both barriers DRM_ASAHI_BARRIER_NONE; retain render barriers and parser rejection. GREEN and affected G3/G4 tests.
- [x] Trace stop/rejection ordering. Fix an additional issue only after deterministic RED in the same owner; otherwise classify limitations.
- [x] Full suite once; compare exact failure names with R140. Read tandem review, independent review, explicit-path implementation commit, then CHANGES.csv row with resulting full hash (implemented; EXP855D is evidence, not validation).

## Review focus and checkpoint

Test poison-initialized stack (MBZ), alternate color geometry/VA, depth/stencil serialization, malformed attachment rejection and private-scene cancellation after parse rejection. No trace-shape admission whitelist. Smallest falsifiable checkpoint is real producer→KMD parser pass with deterministic bad-wire rejection and no leaked job/lease. Recovery is CPU test teardown; no Air, package/signing/caps or hardware checkpoint in this task. Explorer dump classification and unbounded wait audit must distinguish possible source risks from proven observed causes.
