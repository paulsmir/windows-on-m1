# OpenAllocation runtime-handle translation plan

**Goal:** fix EXP497 invalid dereference of Windows handle0x400001c0.
**Architecture:** existing WDDM3.0 allocation object/open-count owner. Resolve
the runtime token with the adapter's DxgkCbGetHandleData; no private handle table.
**Spec:** current497 dump and Microsoft common FULL GRAPHICS allocation DDIs.
**Execution:** inline under standing authorization, no new architecture gate.

## Inspected evidence / ownership

- Dump090626-14296-01.dmp SHA7327f4cfecf74048fa223864f688254be164fb7d6afbdf5061ea5700ef7909f7.
  Recovered context pc=AppleAgxRenderAdmission!AdmissionAllocationDescriptionValid+10,
  x12=400001c8, OpenAllocation+0xc0. Arg2 alone is not the recovered faulting PC.
  CreateStandardAllocation+714/CreateAllocation+10f8/OpenAllocations+214 proves
  the previous size/materialization boundary advanced. No render/fence proof.
- allocation_windows.c casts info->hAllocation directly to KMD pointer.
- DXGK_OPENALLOCATIONINFO.hAllocation is D3DKMT_HANDLE assigned by dxgkrnl;
  DXGKARGCB_GETHANDLEDATA with Type=DXGK_HANDLE_ALLOCATION, Flags.Value=0 resolves
  the non-device-specific object. NULL -> STATUS_INVALID_HANDLE.
  https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_openallocationinfo
  https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkcb_gethandledata
- Pinned26100 shared/d3dkmddi.h1363..1390 confirms fields and PASSIVE_LEVEL.
  Microsoft RosKmdDevice.cpp OpenAllocation read fully as common allocation DDI
  reference only; independently derive code, no reference code copied.
- render_objects.h and callbacks.c retain adapter owner pointer in Device.Object.
  ADMISSION_CONTEXT.ObjectAdapter is recovered using existing CONTAINING_RECORD.
  Existing Open/Close increments/decrements retained object count and per-device
  count; preserve exact failure rollback. Runtime handle remains stored as token.
- Asahi/m1n1/Mu/memory/paging/scanout owners unchanged; no new platform behavior
  or source archaeology needed for this entirely Windows-facing handle defect.

## Task: resolve runtime token before dereference

Files: allocation_windows.c; tests/test_apple_agx_render_open_allocation.py.

- [ ] RED real production OpenAllocation/CloseAllocation with runtime0x400001c0
  and a distinct valid backing object returned only by callback. Assert correct
  open/close counts, exact runtime token retention, ReadOnly, and callback args.
  Assert callback NULL -> INVALID_HANDLE with rollback of prior successful open;
  missing interface -> fail closed; description mismatch -> no ownership change.
- [ ] Recover adapter from device.Object.Adapter after NULL validation; require
  valid interface and GetHandleData callback. Zero query; set hObject and Type;
  resolve allocation. Reject NULL or invalid object magic as INVALID_HANDLE.
  Keep existing description checks/copy/open lifetime and rollback; carry status
  through rollback so failed handle resolution has its documented return code.
- [ ] Execute focused tests and complete render/feature suites, review diff,
  commit only correction/test/plan, append CHANGES exact commit.
- [ ] Freeze497 plus committed files; build pinnedFRYZZING498 full package,
  sign/Universal/Inf2Cat/analysis/hash gates. Verify clean ordinary before staging.
- [ ] Preregister498: one natural bind tests primary OpenAllocation acceptance
  and identifies next Windows callback. Collect exact receipts/dump/ETL first,
  then exact cleanup and ordinary restoration. No repeated497 or AGX claims.

Self-review: no second memory owner, private handle table or copied ABI; callback
is the supported resolver at this DDI IRQL. The single changed contract is
runtime-handle identity, not allocation format/resource architecture. Recovery
artifacts remain377/392;385 emergency only if candidate prevents recovery.
