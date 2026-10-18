# EXP908 display-source and POST audit; candidate EXP909

Classification: FULL GRAPHICS. KMDOD scheduler/admission assumptions are not applied.

## Proven boundary

Source HEAD8406bdc7, exact package908 source104c4b83: successful AdmissionDdiStartDevice returns NumberOfVideoPresentSources=1 and NumberOfChildren=1. Early ETL event139 independently records Apple miniport0xffffe40ceca02000 SUCCESS/1/1. Event24 names NbVidPnSources=1 for adapter0xffffe40cecde3000. Thus the prior NumOfSources=0 observation is NOT evidence that StartDevice provided no source. The EnumAdapters2 probe executes in SSH session0; fresh ordinary process session0 and desktop session1 are independently recorded. Session dependence is a hypothesis, not yet measured in the console session. Current ordinary BasicDisplay reports DisplaySupported while EnumAdapters2 also reports sources0. No capability/source-count change is justified.

Microsoft assigns source IDs from StartDevice and target IDs from ChildUid: [enumeration](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/enumerating-child-devices-of-a-display-adapter). Exact KMD QueryChildRelations emits one TypeVideoOutput/internal ChildUid0/AcpiUid0 plus terminating descriptor; QueryChildStatus returns connected; descriptor query declines EDID. RecommendFunctionalVidPn returns the documented no-recommendation status, allowing OS fallback. IsSupportedVidPn accepts empty or one0->0 path; EnumVidPnCofuncModality offers2560x1600/BGRA32/stride10240 and inherited panel signal. CommitVidPn validates source0, one path,target0,pinned source and actual scanout commit. All these source declarations coexist with an actual successful initial Commit/source address in908.

Actual EXP908 AML SHA3bdb28edb9def16f147f00947d9a2368f1f99036d601c86ead8b0ff46ccbab7f disassembled under .local/analysis/EXP908-display/acpi: AGX0/APPL0002 has no _DOD or _ADR child. The KMD child is not ACPI-enumerated (AcpiUid0), and absence of an ACPI child is not proof of missing WDDM target; [child descriptor contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/ns-dispmprt-_dxgk_child_descriptor). No Mu change selected.

PostDevice is a D3DKMT_ADAPTERTYPE/OS adapter property, not a DXGK_DRIVERCAPS field in pinned26100. Exact POST receipt: AcquireStatus0,Route2(adopt),Decision0,2560x1600,pitch10240,A8R8G8B8,TargetIdFFFFFFFF,AcpiId0,physical85f000000. Uninitialized target after boot is documented: [POST callback](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/nc-dispmprt-dxgkcb_acquire_post_display_ownership). Current driver registers StopDeviceAndReleasePostDisplayOwnership and returns stored information for target0. Mu AdtParser copies boot_args video into framebuffer PCDs; MemoryInit reserves it; MsPlatformDevices retains GOP console identity. Apple subsequently has PostDevice in event24. Ownership was acquired; Basic Render cannot own physical POST because it has RenderSupported only.

## Other adapter and trace correlation

DpiReportAdapter110 maps Apple3ee44->cecde3000/ACPI/APPL0002; BasicRender61e2->cea21b000/root/1414:008C; BasicDisplay61a8->cea1d1000/root/1414:008D. Device27+Context30 lifetime/order binds all72 early DWM1228 Present184 to BasicRender, before Apple start. Later DWM Apple device/context creation is visible, but the early window has no post-start DWM184. These pre-start Presents do not prove post-start composition on BasicRender.

Full frozen final ETL SHA5fb89af82d109ecd087644906e882b9f5f36428cc2b3ad604d980181b84723b0 decoded offline on builder:780152 events,6164 VSyncDPC at Apple/address1500110000, all NO_DEVICE; three Present184 from4652/6476, none1228. NoDevice describes missing flip device for these events; it is not proof of zero supported sources. Raw trace clock bias remains documented; no cross-boot UTC matching. Guest26200 event metadata now saved, header26100 compatibility/version limit retained.

UMD lines have no per-line boot/session identity. PID1220 callback successes cannot be attributed to original908 DWM1228. Exact1228 rows contain no UMD Present entry and have COPY_QUERY failures matching frame entries5/6/7, absent/nonvalid logical PTEs56/57 plus submit branch7/9. Do not conflate their fences with adapter-global119039 or with UMD monitored fences.

All read-only guest and parsed evidence: repository root .local/analysis/EXP908-display/{inventory.json,query-adapters.txt,metadata.json,provider-display.json,display-init-events.json,post-display.json,dwm-present-binding.json,final-analysis.json,dwm1228-umd.txt}. Ordinary boot04:28:38.580572Z retained, SSH alive,CPU8,Cfree5151719424; BasicDisplay/BasicRender Running. No launcher/guest/package mutation.

## Deterministic owning defect and minimal candidate

WHY THIS HYPOTHESIS:
1. Wom1G4SubmitFailure v2 records branch9/subsite2(Output)/kind3(Attachment),VA1680000/bytes4000/write1,logicalIPA8e4040000..8e4043000,segment2/flags3. GraphPresent=1 independently proves the complete writable graph range; Pid4 is System. This is a valid local placement beyond DCP56MiB, not proof that this first-global failure belongs to DWM.
2. Actual AdmissionG4SubmitVirtualEnvelope calls AdmissionMemoryRuntimeScanoutView; that helper forcibly limits LocalView.Bytes to3800000. ResolveOutput rejects an otherwise valid local render output beyond this bound. Real extracted Envelope+real ScanoutView ASan/UBSan replay reproduces C000000D/branch9/subsite2.
3. Non-displayable render outputs and direct DCP surfaces have different owners; backend G4 binds color GPUVA/PA/bytes independently of DCP registration. Conflating their bounds deterministically rejects ordinary valid rendering.

WINDOWS CONTRACT: [SubmitCommandVirtual](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_submitcommandvirtual) validates GPUVA work; C000000D puts the device into error. [GpuMmu](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-virtual-memory-in-wddm-2-0) supports local/system mappings, with UMD residency responsibility. A nondisplayable resident render output need not lie in a scanout-registration window.

AGX/ASAHI CONTRACT: pinned77cb8f24 drivers/gpu/drm/asahi/queue/render.rs carries fragment attachments in process GPUVA; apple/iomfb_template.c handles display swaps separately. Current m1n1 hv_agx_gpuva_v5.c bounds public grants by complete local reserve; hv_agx_scanout_broker.c independently validates its registered DCP pool. Mu generated resources/local reserve are unchanged. No external code copied.

TRANSLATION: use existing AdmissionMemoryRuntimeLocalView ONLY for G4 native non-Present render output admission. Keep graph/residency/contiguity/overflow guards; keep ScanoutView and DCP registration/Present bounds unchanged. No broker ABI, caps, scheduler, renderer, shader, RTKit, UAT or firmware change. Owner KMD G4 admission. Regression must execute actual submit/resolve and assert high output accepted while direct scanout remains refused.

WHAT IS STILL UNKNOWN: whether this correction removes a DWM-relevant early rejection and permits ordinary desktop progression. DWM-specific branch9 may have another parse cause; query56/57 may remain. One EXP909 discriminator uses unchanged908 renderer/VSync/firmware and tests actual Present->DCP, same boot>=750s and matching+120/+300/+600. No promise of sufficiency. Preserve immutable ordinary377/392 and hidden385 emergency artifacts. Failure requires independently verified evidence and exact gated cleanup/ordinary Code28 recovery.

Verification: real replay RED branch9/subsite2 -> GREEN queued high output; ASan/UBSan. Full1190 baseline16FAIL40ERROR2SKIP -> final16FAIL39ERROR2SKIP. Exact IDs unchanged except the intentional new RED test becomes GREEN; active-launcher guard two IDs are present on both sides. Existing unrelated failures retained. Logs and exact-ID diff in .local/analysis/EXP908-display.
