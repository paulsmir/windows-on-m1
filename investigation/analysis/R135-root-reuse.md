# R135 — EXP854B root reuse (offline)

Spec: `/Users/pavel/public_windows/.local/tandem/NEXT_TASK_R135.md`.
Execute inline in the supplied worktree. Air and package builds prohibited.

## Source-first contract and implementation plan

Inspected saved EXP854B receipt/minidump and matching private PDB with CDB;
current Asahi `drivers/gpu/drm/asahi/{mmu,pgtable}.rs` (16-KiB UAT and VM
ownership); m1n1 `hv_agx_gpuva_v5.c` table_add/revoke/relocate_root and
`hv_agx_retained_platform.c` dispatch; Mu T810X MemoryInitPeiLib reservation
and J313AppleAgxAbiAdmission.asl.inc R64 resource; pinned WDK26100
`d3dkmddi.h` and Microsoft UpdatePageTable/GPUMMUCAPS/64KB-page documentation.
Saved EXP854B R110 full-owner and EXP377/392 recovery contracts remain fixed.
No new live measurement: this is attribution of the saved crash, not hardware.

VidMm owns table allocation/residency and logical format. KMD owns original
page to broker-shadow identity, graph lifecycle and DDI status. The broker
owns physical table validation, root/slot exclusion and UAT/TLB ordering.
Mu owns the reserved local RAM and ACPI resources; m1n1 owns stage-2 and
hardware access. Initialization, runtime jobs, interrupts, DMA, power and
recovery remain with their existing layers. Asahi has native 16-KiB tables;
Windows has logical 4K/64K leaf formats of different allocation sizes, translated
by KMD into separate 16-KiB broker tables. No external source is copied.

WHY CONTINUE COMPARISON: one current dump/source pass identifies exact
RootIpa equality at retire_table's unconditional root protection. No old
working-reference search or WDDM admission reconstruction is needed.

WHY THIS HYPOTHESIS: (1) dump Graph.RootIpa equals receipt BrokerTableIpa,
with table Level0; (2) Parents/Leaves are NULL, JobInFlight/LeaseToken/Slot
zero; (3) the failed GPU_PHYSICAL call initializes a new level1 table at
that original page, InitialUpdate1/count2048. Thus stale root designation,
not leaf-format alignment or a reachable incoming link, blocks retirement.

- [x] Add real outer-DDI + broker replay: populated root, switch 4K/64K leaf
  tables, clear graph, reuse old root as level1 InitialUpdate. Expect RED
  STATUS_INVALID_ADDRESS/TableGraph2 before production change.
- [x] In KMD, park only an empty, unleased former root on the existing empty
  bootstrap root when InitialUpdate reuses it at a non-root level. Preserve
  graph/broker real-conflict checks and status; never convert failure to success.
- [x] Verify both leaf formats/profiles, missing InitialUpdate, live links,
  job/lease/slot, bootstrap protection, allocation/rebind failures and retry;
  prove new-root rebind/translation and old-context root mismatch rejection.
- [ ] Run affected tests then full suite once; compare exact failure names
  with R134. Review, commit explicit paths, append CHANGES.csv EXP854B row
  with implementation hash in a separate bookkeeping commit.
- [x] Attribute VA3b0000 from saved process/PTE/allocation data where present;
  state capture limits instead of inferring imported/primary from an address.

Smallest falsifiable offline checkpoint: the exact lifecycle returns success,
old root becomes level1, bootstrap is current until SetRootPageTable, and an
old context cannot start a job. Nonempty/in-flight conflicts must still fail.
No hardware checkpoint is authorized. Any later hardware test must hash-bind
its package and retain ordinary EXP377/392 plus emergency hidden rollback.

## Tandem dispositions

REVIEW R113: ACCEPT — preserve cold full-owner after arm; no launch here.
REVIEW R111: ACCEPT — preserve Normal/WC R64 CPU mapping; no new RAM alias.
REVIEW R110: ACCEPT — preserve measured 64-bit ACPI range fix.
REVIEW R109: REJECT — preserve its recorded rejection; no new firmware hypothesis.
REVIEW R108: DEFER — historical firmware build/selector work is outside this boundary.
REVIEW R107: DEFER — prior 0x101 attribution cannot distinguish the current PTE failure.
REVIEW R106: ACCEPT — ordered transitions and firmware reserve ownership remain required.
REVIEW R105: ACCEPT — use EXP836 matrix verdict; no repeated matrix here.
REVIEW R104: REJECT — prior hypothesis was rejected; allocation union is not a blanket forcing knob.
REVIEW R103: DEFER — AllocateCb boundary is crossed; no live probe requested.
REVIEW R102: DEFER — old allocation rejection is not the present access failure.
REVIEW R100: REJECT — retain its recorded rejection, no archaeology pass.
REVIEW R99: DEFER — old CreateDevice issue is outside the measured boundary.
REVIEW R98: ACCEPT — future output tests include full primary dimensions, without shape whitelists.
REVIEW R97: ACCEPT — one current-source comparison; no old-working-image search.
REVIEW R96: ACCEPT — preserve existing TA/3D builder; no reconstruction.
REVIEW R95: ACCEPT — retain ownership, lifetime and first-failure gates.
REVIEW R94: ACCEPT — preserve per-process GPU versus firmware-object ownership.
REVIEW R91: DEFER — panel observation remains recorded; no new display claim.
REVIEW R90: DEFER — color/panel evidence is outside this translation decision.
REVIEW R88: ACCEPT — publication, execution/completion and DWM frame remain distinct.
REVIEW R86: ACCEPT — retain bounded grant capacity; cap support cannot imply unlimited residency.
REVIEW R85: ACCEPT — any later build/run must bind source, profile, ABI and artifact hashes.
REVIEW R74: DEFER — preserve existing context DMA contract; no context-placement change in A.
REVIEW R71: ACCEPT — test both leaf formats; the measured TVB page-list PFNs remain fragmented4K despite SysMem64KB. Do not infer a large-page placement guarantee or use an unavailable WDK field.
REVIEW R69: ACCEPT — no live bind; future cold start remains separately gated.
REVIEW R65: ACCEPT — preserve current G4 ABI/root/rights/lease/fence semantics.
REVIEW R64: ACCEPT — both entries: preserve firmware-owned reserve and current G4 ABI; prior permissions do not authorize hardware in this offline task.
REVIEW R63: ACCEPT — retain firmware carveout ownership; no StartDevice contiguous-allocation workaround.
REVIEW R57: ACCEPT — valid empty-root reuse now returns success; real conflicts and uncertain broker faults deliberately remain fatal/fail-closed, never acknowledged as completed paging.
REVIEW R55: DEFER — repeated StartDevice recovery is a separate defect.
REVIEW R54: DEFER — no experiment series starts in this task.
REVIEW R49: ACCEPT — pinned WDK and coherent caps/table declarations are explicit gates.
REVIEW R48: ACCEPT — no ports opened; launcher/port exclusion preserved.
REVIEW R47: ACCEPT — preserve exact firmware profile/ABI checks.
REVIEW R45: ACCEPT — retain grant/leaf/table/root teardown ordering in offline coverage.
REVIEW R40: DEFER — EL2 timing is not a cause of synchronous Unmapped with these PTEs.
REVIEW R37: DEFER — historical disarmed-start recovery does not justify a new run.

## Dump attribution

CDB 10.0.26100.6584 loaded matching private package854B PDB (SHA256
814bdbb59da1dc43e5f3a4c30d0782a1ff31a5700759ad902c3f4bd8b3ee5194).
`092726-13312-01.dmp` is a minidump; no MEMORY.DMP was collected in this
experiment directory. Crash UTC 2026-09-27T16:32:21.987, uptime 7:00.143,
CPU1, System process. Stack: CompleteBuildPagingBufferIteration ->
UpdatePageTable -> VIDMM_PAGE_DIRECTORY::CommitVirtualAddressRange ->
CVirtualAddressAllocator::CommitVirtualAddressRange ->
CommitVirtualAddressRangeSystemCommand -> VidMmWorkerThreadProc.
Stop 10E/B arguments match EXP854B, including returned C0000141.

At Args fffff509919792b8: UPDATE_PAGE_TABLE, level1, hAllocation NULL,
GPU_PHYSICAL/segment2/offset3784000, StartIndex0/count800, InitialUpdate1,
Repeat0, Use64KBPages0, FirstPteVirtualAddress0, pPageTableEntries64KB NULL.
First input PTE is Flags41/PageTableAddress36b8 (a 4K-format leaf child).
This is the failed level1 initialization, not a captured leaf-format switch.

Process ffffa30e6b17b680 has internal ProcessId F, generation1, RootIpa
9df240000, MappingGeneration27d68, Parents/Leaves/Backings NULL,
JobInFlight/LeaseToken/Slot/Uncertain0, Created1. Table node ffffa30e6ed86220
has Ipa9df240000 and Level0. Receipt BrokerTableIpa is exactly that root.
Thus `retire_table` rejects `table->Ipa == graph->RootIpa` before any broker
revoke/register call. GraphLastStatus0 is consistent but is not the evidence
for this conclusion: pointer identity and the captured table level establish
it directly. No reachable incoming-link conflict exists in the captured graph.
The preceding allocations/free order is inferred; the failing state is measured.

The original table IPA8e3784000 and broker IPA9df240000 are 16K-aligned.
WDK uses 16-byte DXGK_PTE input records: declared logical leaf4K has
8192 entries/128KiB, level1 2048/32KiB, root8/16KiB; alignment16KiB.
Leaf64K has 512 logical records (8KiB payload), with declared allocation16KiB,
a multiple of CPU4KiB and large enough for the native16KiB shadow. Both leaf
formats cover32MiB VA. The failed root-to-level1 reuse is not a table-size or
alignment refusal. There is no evidence that a 64K leaf previously occupied
this particular root page. The replay tests 4K->64K->4K as additional lifecycle
coverage, without claiming that sequence was recovered from the dump.

Official sources inspected:
- [UpdatePageTable flags](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_updatepagetableflags): InitialUpdate names new residency.
- [GPUMMUCAPS](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_gpummucaps): invalidation and fixed table allocation contract.
- [64KB pages](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/support-for-64kb-pages): single-PTE format and per-leaf selection.
- [UpdatePageTable](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_buildpagingbuffer_updatepagetable): logical entries and update-address semantics.
The locally pinned WDK header, rather than newer Learn-only fields, defines ABI.

## Correction and review

Only the KMD owns the lost VidMm-residency transition. Before zeroing or
registering a re-used page, it moves an empty, unleased current root onto the
already registered, empty private bootstrap root. This requires InitialUpdate,
a different non-root level, no live outgoing root links, and a valid parking
root. Broker relocation still checks actual slot ownership. The existing
GraphRegisterTable then retires/revokes/re-registers normally. Graph root
protection and live-link conflicts are unchanged. No failure is converted to
success; a genuine conflict or uncertain broker failure remains fail-closed.
This fixes the measured valid lifecycle's fatal DDI return, not every possible
BuildPagingBuffer fatal-return path.

Relocation increments MappingGeneration. A queued context retains its old
root and cannot BeginJob; real SetRootPageTable is required to admit a job.
A metadata allocation failure after parking leaves the former table registered
at its old level and the process parked safely, so a retry can finish retirement.
The old original page may be cleared during this failing retry, as in existing
level-reuse handling; it is no longer the broker root. No package is built.

Independent review found two test defects, both corrected: an empty environment
variable incorrectly enabled allocation failure in nominal cases, and an
unprepared BeginJob was not a discriminating root-identity test. The final test
has direct success for profiles16/64, a separate allocation-failure retry, actual
broker slot refusal, populated parking-root rejection and otherwise-admissible
BeginJob positive/stale-root-negative/restored-root-positive controls. Removing
the production root-equality check from generated replay now fails precisely at
the stale-root assertion. Reviewer found no remaining Critical/Important issue.
The existing output-view shim is used; this is not a new output-backend proof.

## First G4 range: allocation role and publication boundary

Receipt v2 has Access/Process/ordinal0/write64KiB at VA3b0000, internal owner
ProcessId6, root9ddcd8000, generation1/mapping407. This is a different process
and earlier boundary than the failing root-reuse processF.
`apple_agx_g4_submit.c` checks Process[] first, so ordinal0 identifies
Process[0]: **TVB page-list BO**, as defined in `apple_agx_g4_submit.h` and
constructed by `prepare_process_buffers` in `agx_win32_gpuva_batch.c`.
The source selected by the package's provenance manifest allocates/reuses its
own G4BufferManager[0] BO via agx_bo_create(flags0), CPU maps it, writes page
numbers, and passes the whole mapped allocation. This is not the primary or
presentation-import allocation path. KMD builder assigns it to Objects[41].

That source path is General-class native BO, rounded/aligned64KiB, CPU read/write
and GPU read/write; AdmissionUmdScreenCreateBuffer issues pfnAllocateCb with a
private STAGING_CPUVISIBLE/A8 descriptor, hResource NULL and zero pSystemMem.
Native mapping calls pfnLockCb LockEntire. The role/allocation creation path is
source-backed; it does not prove the captured VidMm section object or OS handle.

The four saved normalized logical PTEs have segment0/flags3 (valid+writable),
IPAs 97292e000, 97292f000, 972931000, 972932000. The first address is not
16KiB-aligned (remainder2000), and the third skips a4KiB page. This cannot be
one native16KiB leaf. AdmissionG3UpdateLeaf therefore keeps the logical mapping
and leaves the native leaf unpublished. A single Use64KBPages expansion would
produce contiguous4KiB addresses, so the captured group is not an intact64K
expansion; it reflects4K mapping/update state despite advertised system64 support.
The receipt stores normalized logical flags, not the original DXGK_PTE union or
Use64KBPages flag. SysMem64KB support is not proof that this CPU-mapped BO's
actual backing received large pages. Neither native VA/size alignment nor the
64KiB requested access changes the measured PFNs.

Exact pSectionObject/imported-backing flags require an allocation-correlated
record. Receipt v2 omits allocation identity and offset, the normalized shadow
holds them only in live memory, and this minidump does not contain the G4 root
value9ddcd8000 anywhere. Walking the captured process list encounters missing
memory before that process. A section-backed classification must not be invented
from size/address. ETL cannot supply this join: the saved file has 717 allocation-start and734
device-allocation-start events, but zero selected VA mapping/allocator events
(333/334/335/336). Selected allocation timestamps are16:25:05.1517772Z through
16:25:21.7952652Z, before the recorded EXP854B boot16:25:22Z; all717 adapter
allocations name the same adapter. Its sole64KiB allocation is alignment4096,
read-set0/write-set1 with no section object. This is not evidence identifying
the later TVB BO. There is no valid allocation-handle/VA/ProcessId6 correlation.
Verdict: own native TVB-page-list role and fragmented4K publication cause are
established; exact section-object/backing flags remain UNMEASURED. No change
to placement, caps or this BO's allocation path is made by R135.

## Verification and artifacts

Source, debugger transcripts, ETL selection, RED/GREEN logs, final suite log,
and hash manifests are under main-repository `.local/experiments/R135-offline/`.
`green-profiles.txt` preserves an early test compile failure; final passing
profile evidence is `final-green-profiles.txt`, not that superseded attempt.

Commands:
- RED: `CC=/tmp/agx-clang-wrapper G3_REPLAY_R135=1 python3 tests/g3_vidmm_replay.py` before the KMD edit; reproduced C0000141/TableGraph2/Level1/AddBranch2.
- Targeted: `CC=/tmp/agx-clang-wrapper python3 -m unittest tests.test_g3_vidmm_replay tests.test_gpuva_g3_caps_contract tests.test_gpuva_g3_contract tests.test_gpuva_g3_paging_bootstrap tests.test_change_ledger` —26 tests PASS.
- Final profiles after review: `CC=/tmp/agx-clang-wrapper python3 -m unittest tests.test_g3_vidmm_replay.G3VidMmReplayTests.test_exp854b_empty_current_root_reuse` —profiles16/64 and allocation-fault retry PASS with ASan/UBSan.
- Full suite: `CC=/tmp/agx-clang-wrapper PATH=/tmp/agx-cc:/opt/homebrew/bin:$PATH python3 -m unittest discover -s tests`. The first run was1130 tests/15 failures/41 errors/2 skips, all56 failure names identical to R133/R134. A second run verifies the final test corrections made during review; it does not represent a product-code iteration.

REVIEW.md was re-read before commit, SHA256
753bdfe638d6171954875a5fc9a58c09761999ffcab89b9d216f566c7e1300c5.
Pinned WDK d3dkmddi.h SHA256
c13cecb0ce73e7bbdb6bec8586d05eea31932a8c532bec49b3dae4a03054770e.
No Air access, firmware change, package build, install, or hardware validation.

Final full suite:1130 tests in100.599s,15 failures/41 errors/2 skipped; all56 names unchanged.
Source manifest SHA256 `19b9a650c7f7e187319b72bc1bb738e2b77c857a15f251f026f91741092a8ecd`.
Verification index SHA256 `492a8131848404cb3bde1f91f0e2ef599870bd07137d29bf82e077bf904c4a1a`.
Dump SHA256 `34b95fc9cdbd4eb831ae52e4caeb965bd3ef4e6ac6df54d148a2caea3960359f`.

Exact full-suite failure/error names (pre-existing, not fixed in R135):

- `test_budget_is_restored_when_final_nop_fails (test_agx_capture_bootstrap.CaptureBootstrapTests.test_budget_is_restored_when_final_nop_fails)`
- `test_capture_aligns_ta_padding_after_appended_helper_configuration (test_agx_capture_bootstrap.CaptureBootstrapTests.test_capture_aligns_ta_padding_after_appended_helper_configuration)`
- `test_capture_initializes_appended_tiling_helper_configuration (test_agx_capture_bootstrap.CaptureBootstrapTests.test_capture_initializes_appended_tiling_helper_configuration)`
- `test_capture_maps_historical_helper_field_without_changing_m1n1 (test_agx_capture_bootstrap.CaptureBootstrapTests.test_capture_maps_historical_helper_field_without_changing_m1n1)`
- `test_capture_persists_the_complete_pulled_attachment_page (test_agx_capture_bootstrap.CaptureBootstrapTests.test_capture_persists_the_complete_pulled_attachment_page)`
- `test_capture_rejects_an_incomplete_pulled_attachment_page (test_agx_capture_bootstrap.CaptureBootstrapTests.test_capture_rejects_an_incomplete_pulled_attachment_page)`
- `test_capture_subprocesses_cannot_mutate_parent_shim_fd_map (test_agx_capture_bootstrap.CaptureBootstrapTests.test_capture_subprocesses_cannot_mutate_parent_shim_fd_map)`
- `test_explicit_budget_covers_first_request_and_is_restored (test_agx_capture_bootstrap.CaptureBootstrapTests.test_explicit_budget_covers_first_request_and_is_restored)`
- `test_manifest_validation_and_native_descriptors (test_agx_firmware_io.FirmwareIoContract.test_manifest_validation_and_native_descriptors)`
- `unittest.case.FunctionTestCase (run_native_suite)`
- `test_real_graph_native_defaults (test_agx_initdata_defaults.NativeInitdataDefaults.test_real_graph_native_defaults)`
- `test_default_type_loader_uses_bundled_proxyclient (test_agx_m1n1_queue_backend.QueueSourceBoundaryTests.test_default_type_loader_uses_bundled_proxyclient)`
- `test_default_type_loader_installs_historical_renderer_schema_compatibility (test_agx_m1n1_render_backend.RenderBackendTests.test_default_type_loader_installs_historical_renderer_schema_compatibility)`
- `test_real_source_boundary_matches_adapter_expectations (test_agx_m1n1_render_backend.RenderBackendTests.test_real_source_boundary_matches_adapter_expectations)`
- `test_pinned_agx_compiler_is_deterministic_and_source_sensitive (test_apple_agx_ad03_compiler_fixture.AppleAgxAd03CompilerFixtureTests.test_pinned_agx_compiler_is_deterministic_and_source_sensitive)`
- `test_driver_lowered_vs_fs_are_deterministic_and_source_sensitive (test_apple_agx_ad03_vs_fs_fixture.AppleAgxAd03VsFsFixtureTests.test_driver_lowered_vs_fs_are_deterministic_and_source_sensitive)`
- `test_generated_triangle_composes_through_production_overlay_contract (test_apple_agx_ad03_vs_fs_fixture.AppleAgxAd03VsFsFixtureTests.test_generated_triangle_composes_through_production_overlay_contract)`
- `test_derived_source_is_reproducible_and_cannot_overwrite_control (test_apple_agx_d3d10_frontend_prepare.FrontendPrepareTests.test_derived_source_is_reproducible_and_cannot_overwrite_control)`
- `test_dynamic_job_is_carried_inside_the_exact_dma_submission (test_apple_agx_dynamic_dma.AppleAgxDynamicDmaTests.test_dynamic_job_is_carried_inside_the_exact_dma_submission)`
- `test_copy_once_relocation_and_rollback (test_apple_agx_dynamic_job.AppleAgxDynamicJobTests.test_copy_once_relocation_and_rollback)`
- `test_windows_renderkm_fails_before_dma_shadow_mutation (test_apple_agx_gdi.AppleAgxGdiContractTests.test_windows_renderkm_fails_before_dma_shadow_mutation)`
- `test_clear_builder_emits_exact_immutable_command (test_apple_agx_mesa_win32_transport.AppleAgxMesaWin32TransportTests.test_clear_builder_emits_exact_immutable_command)`
- `test_release_cannot_erase_completed_oracle (test_apple_agx_output_expectation_lifetime.OutputExpectationLifetimeTests.test_release_cannot_erase_completed_oracle)`
- `test_real_c_route (test_apple_agx_post_display_route.PostDisplayRouteTests.test_real_c_route)`
- `test_runtime_handle_resolution_and_rollback (test_apple_agx_render_open_allocation.OpenAllocationTests.test_runtime_handle_resolution_and_rollback)`
- `test_single_allocation_resource_uses_existing_object_owner (test_apple_agx_render_standard_allocation.StandardAllocationTests.test_single_allocation_resource_uses_existing_object_owner)`
- `test_size_phase_and_primary_shadow_translation (test_apple_agx_render_standard_allocation.StandardAllocationTests.test_size_phase_and_primary_shadow_translation)`
- `test_active_render_consecutive_copies_and_reentrant_completion (test_apple_agx_render_work_queue.WorkQueueTests.test_active_render_consecutive_copies_and_reentrant_completion)`
- `test_descriptor_preserves_format_and_rejects_rgba_primary (test_apple_agx_standard_rgba_backbuffer.RgbaBackbufferTests.test_descriptor_preserves_format_and_rejects_rgba_primary)`
- `test_submission_coordinator_contract (test_apple_agx_submission_coordinator.AppleAgxSubmissionCoordinatorTest.test_submission_coordinator_contract)`
- `test_device_query_classed_buffers_and_reset_are_fail_closed (test_apple_agx_win32_screen.AppleAgxWin32ScreenTests.test_device_query_classed_buffers_and_reset_are_fail_closed)`
- `test_bootargs_layout (unittest.loader._FailedTest.test_bootargs_layout)`
- `test_display_mode_contract (unittest.loader._FailedTest.test_display_mode_contract)`
- `test_portable_cpu_view_validator (test_gpuva_g3_cpu_visible_segment.G3CpuVisibleSegmentTests.test_portable_cpu_view_validator)`
- `test_gpuva_system_context_object_contract (test_gpuva_g3_createcontext_contract.G3CreateContextContractTests.test_gpuva_system_context_object_contract)`
- `test_admission_asl_has_one_edge_irq_and_no_physical_agx_irq (test_j313_agx_abi_admission_contract.J313AgxAbiAdmissionContractTests.test_admission_asl_has_one_edge_irq_and_no_physical_agx_irq)`
- `test_all_generated_outputs_are_checked_in_and_exact (test_j313_agx_abi_admission_contract.J313AgxAbiAdmissionContractTests.test_all_generated_outputs_are_checked_in_and_exact)`
- `test_contract_is_synthetic_only_and_bound_to_current_g2 (test_j313_agx_abi_admission_contract.J313AgxAbiAdmissionContractTests.test_contract_is_synthetic_only_and_bound_to_current_g2)`
- `test_kd_reboot (unittest.loader._FailedTest.test_kd_reboot)`
- `test_proxy_event_checksum (unittest.loader._FailedTest.test_proxy_event_checksum)`
- `test_standalone_monitor (unittest.loader._FailedTest.test_standalone_monitor)`
- `test_drm_owner_must_be_replaced_by_wddm (test_apple_agx_ad03_source_contract.AppleAgxAd03SourceContractTests.test_drm_owner_must_be_replaced_by_wddm)`
- `test_hash_or_source_signature_drift_fails_closed (test_apple_agx_ad03_source_contract.AppleAgxAd03SourceContractTests.test_hash_or_source_signature_drift_fails_closed)`
- `test_pinned_frontend_compiler_encoder_are_reused_but_targets_replaced (test_apple_agx_ad03_source_contract.AppleAgxAd03SourceContractTests.test_pinned_frontend_compiler_encoder_are_reused_but_targets_replaced)`
- `test_software_target_cannot_be_marked_reusable (test_apple_agx_ad03_source_contract.AppleAgxAd03SourceContractTests.test_software_target_cannot_be_marked_reusable)`
- `test_backend_runtime_is_registered_in_wdk_project (test_apple_agx_backend_runtime.BackendRuntimePackageTests.test_backend_runtime_is_registered_in_wdk_project)`
- `test_pinned_mesa_checkout_and_mit_sources (test_apple_agx_mesa_source.AppleAgxMesaSourceTests.test_pinned_mesa_checkout_and_mit_sources)`
- `test_wrong_commit_in_lock_is_rejected (test_apple_agx_mesa_source.AppleAgxMesaSourceTests.test_wrong_commit_in_lock_is_rejected)`
- `test_bounded_fault_snapshot_is_captured_without_blocking_flush (test_apple_agx_queue_fault_snapshot.QueueFaultSnapshotTests.test_bounded_fault_snapshot_is_captured_without_blocking_flush)`
- `test_capture_transport_materializer (test_apple_agx_reloc_capture.RelocCaptureTests.test_capture_transport_materializer)`
- `test_is_a_separate_full_render_wddm3_driver (test_apple_agx_render_admission.AppleAgxRenderAdmissionTests.test_is_a_separate_full_render_wddm3_driver)`
- `test_opt_in_profile_reuses_production_memory_start_and_stop (test_apple_agx_render_memory_qualification.AppleAgxRenderMemoryQualificationTests.test_opt_in_profile_reuses_production_memory_start_and_stop)`
- `test_completion_is_dual_event_driven_and_exact_fence_only (test_apple_agx_render_platform.AppleAgxRenderPlatformTests.test_completion_is_dual_event_driven_and_exact_fence_only)`
- `test_type1_writer_is_atomic_and_exact (test_apple_agx_render_type1_ready.AppleAgxRenderType1ReadyTests.test_type1_writer_is_atomic_and_exact)`
- `test_generated_m1n1_policy_header_is_exact_and_deterministic (test_j313_agx_g2_contract.J313AgxG2ContractTests.test_generated_m1n1_policy_header_is_exact_and_deterministic)`
- `test_public_tooling_is_english_and_location_independent (test_repository_hygiene.RepositoryHygieneTests.test_public_tooling_is_english_and_location_independent)`
