# R138 — EXP855 scanout memory contract (offline)

Task: `/Users/pavel/public_windows/.local/tandem/NEXT_TASK_R138.md`.
No Air access, package, firmware edit, launch or installation is authorized.

## Evidence and source-first inspection

Saved EXP855 hardware-evidence/devnode.reg and run-summary.json under the main
repository .local show StartStage10, C0000141, platform14/status0, backend0,
ARM_CONSUMED, Code43 and pinned SSH. The 64-MiB reserve receipt survived.
No fresh machine observation is claimed. EXP854B crossed StartStage12 with
same R110 firmware; EXP835 proved the borrowed-reserve path. Ordinary
EXP377/392 recovery from EXP855 remains accepted; assisted/standalone launch
contracts are unchanged. No old-reference archaeology is required.

Sources inspected: Asahi `.local/reference/asahi-linux-asahi` buffer.rs
ensure_blocks/scene ownership and t8103-j313.dts DCP route; current m1n1
hv_agx_scanout_broker.{c,h} REGISTER_POOL/surface validation and
hv_agx_scanout_service.c validate_pages/reserve_iova/map; Mu
MemoryInitPeiLib.c reserve HOB and generated J313AppleAgxAbiAdmission.asl.inc
64-bit resource; KMD lifecycle.c, memory_runtime_windows.c, render_memory.c,
scanout_windows.c, apple_agx_scanout.c and apple_agx_fixed_panel.c.
Official Microsoft definitions inspected:
[segment size](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_segmentdescriptor4),
[primary segment-relative address](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_setvidpnsourceaddress).
No external implementation is copied.

Exact rejecting expression: AdmissionScanoutStart requires memory.Bytes ==
APPLE_AGX_SCANOUT_J313_POOL_SIZE (56 MiB), while the successful production
AdmissionMemoryRuntimeScanoutView returns LocalAllocationBytes (40 MiB).
The return is STATUS_INVALID_ADDRESS before allocation or any broker MMIO.
Existing replay silently supplied its own obsolete 56-MiB constant and never
called ScanoutStart. R138 imports production constants and view declarations.
RED: existing test name test_exp833_exact_eleven_descriptors_and_fail_closed_cases
now executes the real memory partition, borrowed ScanoutView, ScanoutStart,
fixed-panel client and m1n1 broker: `c0000141 (MMIO 0)`.
Log: `.local/experiments/R138-offline/red.log` in this worktree.

## Plan and ownership

1. Preserve 40/16/8 partition, segment sizes, private ownership and backend
   aliases; add a distinct validated PoolBytes to the scanout memory view.
2. Prove the full fixed 56-MiB DMA window against allocation size, offset,
   address overflow, local-object length and backend boundary. Start checks
   PoolBytes; Bytes remains 40 MiB for every surface/diagnostic consumer.
3. Replay real Start/Stop against the existing broker; test truncated window,
   boundary mismatch, legacy ABI and private/backend surface exclusion.
   Run affected/profile tests and one full suite, compare exact failure names.
4. Independent review; explicit-path implementation commit, then CHANGES.csv
   row referencing its full hash with status=implemented and related EXP855.

Mu excludes all 64 MiB from OS RAM; m1n1 validates/maps the fixed 56-MiB
scanout DMA window and owns DCP/DART, IRQ/latch, power and hardware quiescence.
KMD owns the 40-MiB VidMm/primary subrange, 16-MiB private storage and 8-MiB
backend plus lifecycle/recovery ordering. Windows manages only advertised
40-MiB allocations. Asahi private kernel allocation is not Windows VidMm.
A DART mapping is reachability, not permission to present private storage:
all KMD surface bounds stay 40 MiB. Mapping [40,56) in DCP already happened
with the unchanged firmware ABI; it never authorizes a KMD surface there.
Smallest future hardware checkpoint (not run here): unchanged full-owner
firmware with a separately approved package crosses StartStage10; collect
receipt before evidence-first exact cleanup to ordinary EXP377/392.

## Tandem review dispositions

REVIEW R113: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R111: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R110: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R109: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R108: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R107: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R106: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R105: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R104: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R103: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R102: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R100: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R99: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R98: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R97: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R96: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R95: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R94: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R91: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R90: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R88: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R86: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R85: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R74: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R71: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R69: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R65: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R64: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R63: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R57: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R55: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R54: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R49: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R48: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R47: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R45: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R40: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.
REVIEW R37: DEFER — historical item outside EXP855 Stage10 memory-view mismatch; no firmware/hardware or other lifecycle change in R138.

## Result and verification

Implemented in the KMD memory-view owner; no broker constant or validation
was relaxed. PoolBytes is exactly 56 MiB; Bytes remains 40 MiB. View validation
now covers the full broker span, private/backend boundaries, local-object
length, borrowed receipt/ADL ownership, PA identity and IPA/PA end overflow.
The existing replay imports production constants and view type and exercises
real partition, borrowed memory, ScanoutView, BackendView, ScanoutStart/Stop,
fixed-panel/scanout client and m1n1 broker. Only OS services and hardware
service completion are modeled. This is a reached-boundary replay, not a
complete Windows PnP or physical DART/DCP run.

Checks: fixed 56-MiB registration, 40-MiB surface bound, all backend addresses
at +56 MiB with size8, private/backend surface exclusion, truncated allocation
and local object, insufficient suffix after CPU offset, mismatched partition,
IPA/PA overflow, bad reserve/device PA, contiguous ADL compatibility, ABI-v1
refusal before REGISTER, release and restart. Production/probe consumers retain
Bytes, so diagnostics do not gain permission to use private storage.

- RED: `python3 -m unittest discover -s tests -p test_g3_start_resource_replay.py -v`
  returned the EXP855 C0000141 with MMIO0 before the fix.
- GREEN: same command passes, including final added overflow/backend assertions.
- Focused: CC=clang unittest modules test_g3_start_resource_replay,
  test_g3_kmd_local_reserve, test_apple_agx_render_memory,
  test_apple_agx_render_scanout, test_apple_agx_scanout, test_apple_agx_fixed_panel,
  test_gpuva_g3_caps_contract, test_g3_vidmm_replay, test_g3_private_pool,
  test_g3_private_storage, test_g4*replay, test_change_ledger: **53 PASS**.
  G3 replay includes both profiles16/64. Existing test names are unchanged.
- Full: `CC=clang python3 -m unittest discover -s tests -v`: **1137 tests,
  15 failures, 41 errors, 2 skips**. All 56 failure/error names match R137
  baseline exactly; new0/removed0. This is not a green full suite.
  The final additional backend-address/overflow assertions also passed in the
  targeted replay after that full-suite test had executed.
- Independent reviewer: no Critical/Important/Minor findings; reviewed source
  ownership and tests, no separate test execution. `git diff --check` passes.
- No Windows build, package, staging, Air access or hardware run was performed.
  Code0, G4 admission, native completion and DWM remain unvalidated.

Logs and failure-comparison.json are under `.local/experiments/R138-offline/`
in this worktree. The saved source manifest covers all tracked driver files.
Driver source manifest SHA256: `2dca0d3fcef3e5e01844a42a00a45ba0618ae7975624a40e96478fd3af61aa7a`.
Reviewed tandem file SHA256: `753bdfe638d6171954875a5fc9a58c09761999ffcab89b9d216f566c7e1300c5`.

### Unchanged baseline failures/errors (exact names)

```text
ERROR: test_active_render_consecutive_copies_and_reentrant_completion (test_apple_agx_render_work_queue.WorkQueueTests.test_active_render_consecutive_copies_and_reentrant_completion)
ERROR: test_admission_asl_has_one_edge_irq_and_no_physical_agx_irq (test_j313_agx_abi_admission_contract.J313AgxAbiAdmissionContractTests.test_admission_asl_has_one_edge_irq_and_no_physical_agx_irq)
ERROR: test_all_generated_outputs_are_checked_in_and_exact (test_j313_agx_abi_admission_contract.J313AgxAbiAdmissionContractTests.test_all_generated_outputs_are_checked_in_and_exact)
ERROR: test_bootargs_layout (unittest.loader._FailedTest.test_bootargs_layout)
ERROR: test_budget_is_restored_when_final_nop_fails (test_agx_capture_bootstrap.CaptureBootstrapTests.test_budget_is_restored_when_final_nop_fails)
ERROR: test_capture_aligns_ta_padding_after_appended_helper_configuration (test_agx_capture_bootstrap.CaptureBootstrapTests.test_capture_aligns_ta_padding_after_appended_helper_configuration)
ERROR: test_capture_initializes_appended_tiling_helper_configuration (test_agx_capture_bootstrap.CaptureBootstrapTests.test_capture_initializes_appended_tiling_helper_configuration)
ERROR: test_capture_maps_historical_helper_field_without_changing_m1n1 (test_agx_capture_bootstrap.CaptureBootstrapTests.test_capture_maps_historical_helper_field_without_changing_m1n1)
ERROR: test_capture_persists_the_complete_pulled_attachment_page (test_agx_capture_bootstrap.CaptureBootstrapTests.test_capture_persists_the_complete_pulled_attachment_page)
ERROR: test_capture_rejects_an_incomplete_pulled_attachment_page (test_agx_capture_bootstrap.CaptureBootstrapTests.test_capture_rejects_an_incomplete_pulled_attachment_page)
ERROR: test_capture_subprocesses_cannot_mutate_parent_shim_fd_map (test_agx_capture_bootstrap.CaptureBootstrapTests.test_capture_subprocesses_cannot_mutate_parent_shim_fd_map)
ERROR: test_clear_builder_emits_exact_immutable_command (test_apple_agx_mesa_win32_transport.AppleAgxMesaWin32TransportTests.test_clear_builder_emits_exact_immutable_command)
ERROR: test_contract_is_synthetic_only_and_bound_to_current_g2 (test_j313_agx_abi_admission_contract.J313AgxAbiAdmissionContractTests.test_contract_is_synthetic_only_and_bound_to_current_g2)
ERROR: test_copy_once_relocation_and_rollback (test_apple_agx_dynamic_job.AppleAgxDynamicJobTests.test_copy_once_relocation_and_rollback)
ERROR: test_default_type_loader_installs_historical_renderer_schema_compatibility (test_agx_m1n1_render_backend.RenderBackendTests.test_default_type_loader_installs_historical_renderer_schema_compatibility)
ERROR: test_default_type_loader_uses_bundled_proxyclient (test_agx_m1n1_queue_backend.QueueSourceBoundaryTests.test_default_type_loader_uses_bundled_proxyclient)
ERROR: test_derived_source_is_reproducible_and_cannot_overwrite_control (test_apple_agx_d3d10_frontend_prepare.FrontendPrepareTests.test_derived_source_is_reproducible_and_cannot_overwrite_control)
ERROR: test_descriptor_preserves_format_and_rejects_rgba_primary (test_apple_agx_standard_rgba_backbuffer.RgbaBackbufferTests.test_descriptor_preserves_format_and_rejects_rgba_primary)
ERROR: test_device_query_classed_buffers_and_reset_are_fail_closed (test_apple_agx_win32_screen.AppleAgxWin32ScreenTests.test_device_query_classed_buffers_and_reset_are_fail_closed)
ERROR: test_display_mode_contract (unittest.loader._FailedTest.test_display_mode_contract)
ERROR: test_driver_lowered_vs_fs_are_deterministic_and_source_sensitive (test_apple_agx_ad03_vs_fs_fixture.AppleAgxAd03VsFsFixtureTests.test_driver_lowered_vs_fs_are_deterministic_and_source_sensitive)
ERROR: test_dynamic_job_is_carried_inside_the_exact_dma_submission (test_apple_agx_dynamic_dma.AppleAgxDynamicDmaTests.test_dynamic_job_is_carried_inside_the_exact_dma_submission)
ERROR: test_explicit_budget_covers_first_request_and_is_restored (test_agx_capture_bootstrap.CaptureBootstrapTests.test_explicit_budget_covers_first_request_and_is_restored)
ERROR: test_generated_triangle_composes_through_production_overlay_contract (test_apple_agx_ad03_vs_fs_fixture.AppleAgxAd03VsFsFixtureTests.test_generated_triangle_composes_through_production_overlay_contract)
ERROR: test_gpuva_system_context_object_contract (test_gpuva_g3_createcontext_contract.G3CreateContextContractTests.test_gpuva_system_context_object_contract)
ERROR: test_kd_reboot (unittest.loader._FailedTest.test_kd_reboot)
ERROR: test_manifest_validation_and_native_descriptors (test_agx_firmware_io.FirmwareIoContract.test_manifest_validation_and_native_descriptors)
ERROR: test_pinned_agx_compiler_is_deterministic_and_source_sensitive (test_apple_agx_ad03_compiler_fixture.AppleAgxAd03CompilerFixtureTests.test_pinned_agx_compiler_is_deterministic_and_source_sensitive)
ERROR: test_portable_cpu_view_validator (test_gpuva_g3_cpu_visible_segment.G3CpuVisibleSegmentTests.test_portable_cpu_view_validator)
ERROR: test_proxy_event_checksum (unittest.loader._FailedTest.test_proxy_event_checksum)
ERROR: test_real_c_route (test_apple_agx_post_display_route.PostDisplayRouteTests.test_real_c_route)
ERROR: test_real_graph_native_defaults (test_agx_initdata_defaults.NativeInitdataDefaults.test_real_graph_native_defaults)
ERROR: test_real_source_boundary_matches_adapter_expectations (test_agx_m1n1_render_backend.RenderBackendTests.test_real_source_boundary_matches_adapter_expectations)
ERROR: test_release_cannot_erase_completed_oracle (test_apple_agx_output_expectation_lifetime.OutputExpectationLifetimeTests.test_release_cannot_erase_completed_oracle)
ERROR: test_runtime_handle_resolution_and_rollback (test_apple_agx_render_open_allocation.OpenAllocationTests.test_runtime_handle_resolution_and_rollback)
ERROR: test_single_allocation_resource_uses_existing_object_owner (test_apple_agx_render_standard_allocation.StandardAllocationTests.test_single_allocation_resource_uses_existing_object_owner)
ERROR: test_size_phase_and_primary_shadow_translation (test_apple_agx_render_standard_allocation.StandardAllocationTests.test_size_phase_and_primary_shadow_translation)
ERROR: test_standalone_monitor (unittest.loader._FailedTest.test_standalone_monitor)
ERROR: test_submission_coordinator_contract (test_apple_agx_submission_coordinator.AppleAgxSubmissionCoordinatorTest.test_submission_coordinator_contract)
ERROR: test_windows_renderkm_fails_before_dma_shadow_mutation (test_apple_agx_gdi.AppleAgxGdiContractTests.test_windows_renderkm_fails_before_dma_shadow_mutation)
ERROR: unittest.case.FunctionTestCase (run_native_suite)
FAIL: test_backend_runtime_is_registered_in_wdk_project (test_apple_agx_backend_runtime.BackendRuntimePackageTests.test_backend_runtime_is_registered_in_wdk_project)
FAIL: test_bounded_fault_snapshot_is_captured_without_blocking_flush (test_apple_agx_queue_fault_snapshot.QueueFaultSnapshotTests.test_bounded_fault_snapshot_is_captured_without_blocking_flush)
FAIL: test_capture_transport_materializer (test_apple_agx_reloc_capture.RelocCaptureTests.test_capture_transport_materializer)
FAIL: test_completion_is_dual_event_driven_and_exact_fence_only (test_apple_agx_render_platform.AppleAgxRenderPlatformTests.test_completion_is_dual_event_driven_and_exact_fence_only)
FAIL: test_drm_owner_must_be_replaced_by_wddm (test_apple_agx_ad03_source_contract.AppleAgxAd03SourceContractTests.test_drm_owner_must_be_replaced_by_wddm)
FAIL: test_generated_m1n1_policy_header_is_exact_and_deterministic (test_j313_agx_g2_contract.J313AgxG2ContractTests.test_generated_m1n1_policy_header_is_exact_and_deterministic)
FAIL: test_hash_or_source_signature_drift_fails_closed (test_apple_agx_ad03_source_contract.AppleAgxAd03SourceContractTests.test_hash_or_source_signature_drift_fails_closed)
FAIL: test_is_a_separate_full_render_wddm3_driver (test_apple_agx_render_admission.AppleAgxRenderAdmissionTests.test_is_a_separate_full_render_wddm3_driver)
FAIL: test_opt_in_profile_reuses_production_memory_start_and_stop (test_apple_agx_render_memory_qualification.AppleAgxRenderMemoryQualificationTests.test_opt_in_profile_reuses_production_memory_start_and_stop)
FAIL: test_pinned_frontend_compiler_encoder_are_reused_but_targets_replaced (test_apple_agx_ad03_source_contract.AppleAgxAd03SourceContractTests.test_pinned_frontend_compiler_encoder_are_reused_but_targets_replaced)
FAIL: test_pinned_mesa_checkout_and_mit_sources (test_apple_agx_mesa_source.AppleAgxMesaSourceTests.test_pinned_mesa_checkout_and_mit_sources)
FAIL: test_public_tooling_is_english_and_location_independent (test_repository_hygiene.RepositoryHygieneTests.test_public_tooling_is_english_and_location_independent)
FAIL: test_software_target_cannot_be_marked_reusable (test_apple_agx_ad03_source_contract.AppleAgxAd03SourceContractTests.test_software_target_cannot_be_marked_reusable)
FAIL: test_type1_writer_is_atomic_and_exact (test_apple_agx_render_type1_ready.AppleAgxRenderType1ReadyTests.test_type1_writer_is_atomic_and_exact)
FAIL: test_wrong_commit_in_lock_is_rejected (test_apple_agx_mesa_source.AppleAgxMesaSourceTests.test_wrong_commit_in_lock_is_rejected)
```
