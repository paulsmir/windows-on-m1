# R154 — accepted native packet stopped before firmware publication

Offline task on integration/ad04-windows-compiler, base f92e19dd. No Air access, package, staging, arm, firmware change, or hardware run. The existing EXP867 ordinary Code28 recovery remains the last hardware state. Reports: R154-native-state.md, R154-umd-scanout.md, R154-resubmission.md; reproducible raw audit and verification are indexed by investigation/evidence/R154/summary.json.

## Answer to the four task questions

1. R153 crossed the old boundary: persistent native sequence81, TA125/125, D3 162/162, matching final stamps and successful native fence36174 establish 81 joined native completions. Direct ring counts show44 InitBM publications (initial plus43 rebinding transactions). Completed manager owners8 and13 map to Explorer PID536 and M365Copilot PID5996. This proves Explorer work and more than five jobs across processes; no retained per-job owner ledger identifies the exact fifth job or counts process switches.
2. The stalled Windows packet is fence36357 (0x8e05), Explorer PID536/driver owner8, node0/engine0, G4 native render through SubmitCommandVirtual. Packet Active, DispatchedFence36357, prior scheduler/paging completed36356. CPU queue empty. Native backend and provider idle, queue pending0, completion contextNULL, sequence81, last native36174. Its private scene0xbd2 is Queued1/Started0/GpuDone0, no process lease/job-in-flight, image BoundFence36357/JobFence0/JobReady0/G4ManagerNULL. It was not an executed native job82. All162 retained firmware events are ordinary completion flags; no firmware timeout/fault/GrowTVB evidence selects a GPU execution stall. The shared owner13 manager is the retained state of completed36174, not stale materialization of Explorer36357.
3. Final UMD log has546 E_FAIL SetError records:335 DrawIndexedInstanced,210 ResourceCopyRegion,1 DrawIndexed. Exact package-generated lines identify failed preceding-batch retirement for indexed draws and texture-blit flush failure for copies. There are2601 accepted E_PENDING map results, excluded. No timestamp/fence linkage to36357; no denominator for per-DDI failure probability. Sticky terminal state can multiply later failures. Do not infer546 failed native submissions.
4. One black primary request seq2 at PA8e0130000 reached DCP, A408 swap10 and exact D589 latch; both snapshots of it remained zero. Active36357 targets another16KiB allocation, not that primary. No completed render-to-primary is established. Budget-limited successful UMD logs cannot prove Present never ran. Recovery-boot ETL is excluded. Evidence therefore does not choose a DCP fix or prove the primary was never rendered.

## Why no completion interrupt for36357

The real worker first activates the scheduler/RenderPacket, then heartbeats and calls BeginJob, then prepares the manager and submits to the native backend. A BeginJob failure sets SchedulerFaulted and exits through WorkerFinished without publishing a firmware job, arming a completion, or issuing DxgkCbNotifyInterrupt. Preemption then waits for an active boundary that this path can never produce. ResetEngine sees the active private fence, marks cancellation uncertain and returns failure before platform reset. That explains why an unpublished job can still end in0x116; it does not make a synthetic DMA_COMPLETED notification valid.

The dump shows context mapping generation0x2ba36 versus graph0x2ba37. Original BeginJob rejects that mismatch before GraphBeginJob. Exact EXP867 parser replay over the saved header, native bytes, logical envelope, and current graph returnsOk:18/18 access checks. All nine private ranges, USC/VDM/scissor/depth-bias, and output backing are present with required rights; context and graph root match9d5a38000. An unrelated mapping update is sufficient to cause the erroneous veto, and the real VidMm graph/broker replay reproduces it. There is no proof of which page-table update incremented the hardware run's epoch or when; post-TDR cancellation markers must not be treated as original inputs. The epoch veto is a confirmed deterministic software defect and the leading EXP867 causal hypothesis, not a hardware-validated verdict.

The first QUERYv3 predicate53/NoRoot receipt is also a different owner: process9, root0x9ded10000, context0xffff9b0acb094180, mapping generation35122, VA0x1100410000/64KiB. Active36357 is owner8/root0x9d5a38000/generation178742 at submission. It is not a missing-root observation for the stalled Explorer job. First-failure retention and absent timestamps prevent joining it to a particular UMD error.

## Correction 1: fresh validation owns admission

Remove the process-wide epoch equality veto. Keep native parse, exact scene/manager identity, root equality, logical-envelope validity, output physical identity, cancellation, process uncertainty and lease checks under the G3 mutex. Only after successful JOB_BEGIN record the current mapping generation. Graph mutation is already excluded by JobInFlight and the same lock. An unrelated change no longer strands a valid accepted packet; a relevant unmap, wrong root, changed cached output backing, or cancellation still cannot reach the GPU.

The regression replaces the mocked output validator with the actual production function. A second test uses real KMD paging, graph and C broker: accepted coordinates plus unrelated unmap fail old BeginJob C0000184, then pass corrected BeginJob; referenced unmap remains rejected, restored mapping/root is freshly revalidated, live-job map/destruction remain busy. No exact trace address, size or bind combination is used as an admission rule.

## Correction 2: queued-preemption resubmission ownership

EXP867's first G4 EnvelopeState rejection has flags0x80, a documented Resubmission flag. Its command VA1100b90000 differs from active36357 VA3d950000; the receipt does not retain a fence and is not evidence that it rejected36357. Blanket flags0 was nevertheless a deterministic WDDM contract violation. The independent correction must preserve the exact never-started scene while suspended, transfer it to Windows' new rendering fence only after complete revalidation, and distinguish final/uncertain cancellation. R154-resubmission.md records the full implementation and regression contract. It is a separate independently reviewable change, not evidence that EXP867's TDR was caused by resubmission.

## Proposal only: EXP868

WHY THIS HYPOTHESIS:
- Accepted36357 is active in KMD but has no begun private scene, VM lease, materialized job or firmware pending fence; this selects the worker's pre-publication boundary over GPU execution/TLB/completion polling.
- Its saved graph passes exact production range parsing while the sole coarse epoch comparison rejects it; real unrelated-map replay reproduces the defect deterministically.
- Native queues have81 joined completions and no terminal event, and R153's manager snapshots match the last completed owner; another BM/firmware change has weaker causal support.

Proposed single variable: Correction1 only on exact EXP86733503176, unchanged R143 firmware, signer, caps, UMD and recovery; deferred Flush remains excluded. Correction2 is separately implemented offline and should not be mislabeled inseparable from Correction1. If a later task authorizes bundling both verified fixes, list both and use dump/stack attribution under the existing bundle rule. Do not call the independent fixes an ATOMIC CONTRACT merely to avoid isolation.

Smallest checkpoint: the accepted packet passes fresh validation after unrelated graph mutation, acquires its process lease, materializes with a fresh native sequence and reaches joined TA+D3 completion plus the matching Windows fence notification. A new failure must preserve first worker/packet/scene/graph and original-boot evidence. Subsequent600s Code0/SSH/DWM and useful primary pixels remain distinct gates. Exact package/source/artifact/manifest hashes, pre-registration and explicit run authorization are still required. Collect evidence before exact package cleanup; restore ordinary Code28, use immutable hidden recovery for an unrecoverable guest, no automatic retry/rearm.

Scope limit: this removes a false validation failure and corrects queued resubmission; it does not invent successful completion for malformed/unpublished work or implement general recovery from genuine/uncertain GPU hangs. Existing fail-closed reset remains.

## Verification scope

Independent review found no Critical or Important issue. Two Minor coverage limits are retained: there is no single executable Windows PreemptCommand-to-completion test spanning all component harnesses, and failed-admission epoch preservation has no dedicated assertion. The actual shared scheduler/admission transfer and real private lifetime/completion are tested in complementary replays; the report does not call these one end-to-end Windows execution. No hardware fix is claimed.
