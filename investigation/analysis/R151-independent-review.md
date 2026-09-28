# R151 independent review — queue sequence lifetime

Reviewed the uncommitted `render_backend_image.c` correction and associated image/codec regression against HEAD `387bbb01`, plus the production active-memory publication path. This is an offline source review, not a hardware verdict. No package, Air access, production edit, build, or commit was performed by this reviewer. Parent owns test/build execution.

## Assessment

No blocking production defect found in the scoped correction. Preserve `Sequence` across successful G4 Bind/Release template reconstruction: the image is reconstructed while the firmware queues, event-control object and their completion epoch remain alive. Reset remains explicit at the real queue-lifetime boundary. This fixes a deterministic state-lifetime error; it does not establish that the error caused EXP865's TA stall.

## Production path checked

- `render_backend_image.c:40–86`: Prepare creates a zeroed candidate, materializes/rebases the template, then replaces the image. Previously both successful G4 call sites discarded the live sequence.
- `render_backend_image.c:319–332,487–498`: both changed call sites save/restore the same sequence. G4 Build/relocation/header failure disables Ready; it cannot admit a new job with reset identity. Early validation failure leaves the existing image untouched. Release failure does not falsely report success. The explicit RestartQueueLifetime and initial Prepare retain existing reset semantics.
- `backend_platform_windows.c:1930–1955`: the real build path uses StageJob then BuildActiveG4Job with the same IncludeInitBm plan; it does not submit the arena-only addresses from the image test directly.
- `apple_agx_render_shared_memory.c:362–431,565–664`: active refresh copies objects9/10 (firmware stamps),12 (event_count),14/15/17/18/19 (barrier/microsequences/work),26/27 (CPU stamps); object16 InitBM is first-job-only. Object11 EventControl, queue info/rings/pointers, job list and buffer-manager state persist. The generated object map confirms object12 is separate from object11.
- `apple_agx_exp208_dynamic.c:88–154`: sequence2 emits previous stamps+0x100, current stamps+0x200, event_count4, barrier/finalizer/work stamps and prior Start3D queue count consistently. Active refresh copies these exact fields; preserving EventControl does not freeze event_count. Its firmware-owned current count should persist while the driver-owned target advances.
- `backend_platform_windows.c:2026–2034`: every shared-memory object is flushed before successful job publication. This correction does not introduce a new flush/coherency path.
- `backend_platform_windows.c:3838–3865`: existing restart follows backend stop/provider destruction and precedes rebuilding the owned firmware graph. No reset/recovery contract was broadened.

## Primary-source comparison

Inspected local Asahi `77cb8f24c2381a8abb7272d7bbdec548d6426a8a` at `.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi/{event.rs,workqueue.rs,queue/render.rs}`. `event.rs:5–10,55–80` defines monotonically advancing completion identities with a0x100 step; `workqueue.rs:300–312` advances the job's event value separately from command/event counts. Render barrier and finalize references use the next event value. Asahi handles wrap with modular ordering; this repository deliberately refuses its existing stamp overflow rather than adding wrap support.

Inspected current `m1n1_windows/proxyclient/m1n1/agx/render.py:158–207,293–298,451–452,819–822,891,1250,1279`. Renderer construction initializes persistent stamps/event control; each submit advances both stamps by0x100 and event_count by2. Barrier and finalize use the current stamp, Start3D uses the previous stamp's shifted count. Scene reconstruction in the KMD must not behave like renderer construction. No external implementation code was copied.

## Findings and test coverage (initial review)

**Minor, resolved by follow-up tests — mixed image lifecycle coverage was absent in the initial reviewed patch.** New tests exercise G4→G4 and explicit restart; existing legacy tests exercise legacy→legacy. The plan also requested legacy→G4→legacy in one unchanged queue lifetime. Recommend a small interleaved test proving stamps advance across both transitions and no InitBM is accepted after sequence1. This is a coverage gap, not evidence of a production failure.

**Minor, resolved by follow-up tests — active-memory epoch coverage can be more direct.** Initial new assertions inspect the arena plus real completion predicate. Existing shared-memory tests already preserve ring/BM/list/pointer sentinels and exercise BuildActiveG4Job, but did not explicitly check event_count advancement against persistent EventControl. Recommend seeding a firmware-owned EventControl sentinel, refreshing a second active G4 job, and asserting sentinel unchanged, active object12=4, active stamps=previous+0x100, and current barrier/finalizer values. Static source tracing above confirms the current implementation does this.

Existing new negative checks cover wrong-fence Release, restart while bound, terminal sequence refusal, and stale stamp paired deliberately with the new done pointer and firing event. `AppleAgxG13CompletionSatisfied` rejects those prior stamps through its modular comparison, so the stale event/pointer pair alone cannot qualify second completion. The max-u32 test protects preservation through Bind; the existing dynamic test separately covers derived stamp overflow. General fault/retry recovery and mid-publication cancellation remain outside this change.

No claim is made about seventy GPU renders, present execution, visible pixels, or a cured TDR. The smallest hardware discriminator remains the second actual private render showing a new completion identity and both real TA/3D completions, with unchanged firmware and failure remaining fail-closed.

## Follow-up review

Reviewed the added G4→legacy→G4 assertions (sequence3 then4, no restart) and the real `AppleAgxExp208PatchDynamic`→`AppleAgxRenderSharedMemoryBuildActiveG4Job` second-epoch checks. They close both Minor coverage findings. The active test checks event_count4, all four previous stamp words, current TA barrier and TA/3D finalizers, persistent event-control sentinel and queue pointers; the Python harness correctly adds the dynamic source dependency. Production diff is unchanged. Parent reports both targeted tests passing; this reviewer did not rerun them. `git diff --check` passes. No remaining blocking or Important finding.

The event-control sentinel is byte0, part of its address field, so it proves the object is not reinitialized rather than emulating firmware cur_count specifically. The unchanged per-submission copy policy preserves the whole object, including cur_count; source inspection supplies that ownership proof. This is not a production correctness concern.
