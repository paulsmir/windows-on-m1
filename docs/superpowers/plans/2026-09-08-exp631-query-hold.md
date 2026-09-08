# EXP631 Query and Hold Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Publish and strictly validate two immutable, fully verified render-frame records, then hold both Windows allocations alive for 15 seconds without cleanup to separate two-frame stability from retirement.

**Architecture:** Keep hardware completion, PASSIVE output verification, and DCP latch as separate phases. Build the query record from completed-output identity and terminal scan metrics before lease transfer, publish only after exact latch and transfer, and make post-publication failures retain ownership. A generation-locked output queue makes idle mean no queued or active worker.

**Tech Stack:** C11 host tests, WDK 26100 ARM64 KMD/producer, WDDM D3DKMT qualification producer, existing retained-root/AGX/DCP runtime.

**Spec:** `investigation/GPU_CURRENT_STATE.md`

## Global Constraints

- Work inline in the current process; no agents.
- Do not change AGX ABI, PBE, RTKit, UAT, scheduler capabilities, colors, or DCP protocol.
- One hardware variable: corrected query/acceptance plus explicit FRAME1/FRAME2/HOLD phases; no retirement in EXP631.
- Preserve unknown published display ownership; never convert timeout to success.

---

### Task 1: Immutable query record and strict acceptance

**Files:**
- Modify: `drivers/apple-agx/render-admission/include/render_qualification.h`
- Create: `drivers/apple-agx/render-admission/src/render_qualification.c`
- Create: `drivers/apple-agx/render-admission/tests/render_qualification_test.c`
- Modify: `drivers/apple-agx/render-admission/src/scanout_windows.c`
- Modify: `drivers/apple-agx/windows/one-shot/apple_agx_d3dkmt_render.c`
- Modify: both KMD and producer `.vcxproj` files.

**Interfaces:**
- Produces: `AdmissionPresentQueryBuild` from completed identity plus measured scan metrics, and `AdmissionPresentQueryAccept` returning Completed/TimedOut/QueryFailed/InvalidRecord semantics to the producer.

- [ ] Add RED tests proving transfer clears `Completed` but the prebuilt record retains build/boot/frame/fence/allocation/color/format/full pixel/hash identity; invalid zero pixels, stale frame/fence/allocation, duplicate sequence and wrong purpose are rejected.
- [ ] Run the focused host test and confirm the expected failures.
- [ ] Implement the shared record builder/validator and wire KMD history publication before any read of cleared `Completed`.
- [ ] Replace `NT_SUCCESS(STATUS_TIMEOUT)` handshake with the explicit wait result and verify both focused and existing output tests GREEN.
- [ ] Commit the coherent query/acceptance contract.

### Task 2: Published ownership and exact output-idle state

**Files:**
- Modify: `drivers/apple-agx/render-admission/include/render_completed_output.h`
- Modify: `drivers/apple-agx/render-admission/src/render_completed_output.c`
- Modify: `drivers/apple-agx/render-admission/tests/render_completed_output_test.c`
- Create: `drivers/apple-agx/render-admission/include/render_output_queue.h`
- Create: `drivers/apple-agx/render-admission/src/render_output_queue.c`
- Create: `drivers/apple-agx/render-admission/tests/render_output_queue_test.c`
- Modify: `drivers/apple-agx/render-admission/src/backend_platform_windows.c`

**Interfaces:**
- Produces: explicit NotPublished/PublishedPending/Latched/OwnershipUnknown transitions and a lock-owned queue state where idle requires scheduled=0 and active=0 for the current generation.

- [ ] Add RED state-machine tests: pre-publication failure releases; post-publication timeout retains; latch transfers; replacement releases only old active owner; a new scheduled generation prevents stale idle signal.
- [ ] Run focused tests and confirm the expected failures.
- [ ] Implement the minimal states and output-queue helper; wire the existing work item without changing render/present commands.
- [ ] Run focused and full AppleAgx test suites GREEN.
- [ ] Commit the lifecycle correction.

### Task 3: Two-frame hold producer and EXP631

**Files:**
- Modify: `drivers/apple-agx/windows/one-shot/apple_agx_d3dkmt_render.c`
- Create experiment-local build/workflow/launcher/manifest files under `.local/experiments/EXP631-query-hold/`.
- Update: `investigation/EXPERIMENTS.md`, `investigation/CHANGES.csv`, `investigation/GPU_CURRENT_STATE.md`.

**Interfaces:**
- Produces: `--hold-no-cleanup` mode with immediately flushed FRAME1, FRAME2, HOLD_BEGIN and HOLD_PASS markers; after HOLD_PASS it keeps resources alive until controlled whole-guest recovery.

- [ ] Add executable producer-contract tests proving timeout/invalid query never starts frame2 or cleanup, every phase marker flushes, and hold revalidates immutable records after 15 seconds.
- [ ] Implement the mode and run focused/full tests.
- [ ] Freeze exact overlay, build/sign/analyze on FRYZZING, record every hash and preregister FRAME1/FRAME2/HOLD recovery.
- [ ] Restore ordinary Code28 baseline, stage exact EXP631, run once, preserve stdout/query/correlation/host/events and recover by controlled guest shutdown without allocation destruction.
- [ ] Classify TWO_FRAME_RENDER, TWO_FRAME_OUTPUT, TWO_FRAME_PRESENT and HOLD_STABILITY independently; do not start retirement in this experiment.
