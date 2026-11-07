# Q22c activation evidence and implementation plan

Goal: make live-additive activation observable without changing a working
negotiation contract. Spec: the task handoff's Follow-up q22c.

Ruling: EXP2116's activation inference is contradicted by its own receipts.
At QWORD index 112 (byte offset896) in Wom1G3PagingProfile, command17 has one call and
command 19 has 25 calls. The failure ring has only CREATE/STALE for epoch
discovery. Wom1G3Node0 contains NodeOrdinal=1, EngineType=6 (COPY), GpuMmu=1:
AdmissionRecordGpuvaG3Node writes every ordinal under the same name.
The UMD's g4-create-paging-queue-cb second value is PhysicalAdapterIndex,
not a paging-node ordinal. Neither observation shows missing activation.

Source/specification inventory: gpuva_g3_windows.c (startup and process
creation); apple_agx_gpuva_broker_v5_client.c (epoch and capability checks);
hv_agx_gpuva_v5.c (QUERY_CAPS/NEGOTIATE_LIVE); hv_agx_gpuva_v5_mmio.c and
hv_agx_retained_platform.c (real retained dispatch and response epilogue);
hv_agx_power_mmio.c (window routing); lifecycle.c/callbacks.c (topology);
receipts.c (node and timing receipts); umd_runtime_device.c (trace fields);
WDK26100 d3dkmddi.h and d3dumddi.h. EXP2116 full.log lines 1876-1883
show arm consumption, retained activation and broker init=0/epoch=1.
Its packaged source matches the inspected KMD/client sources. Firmware build
info pins 7cce5a0e, ABI6, J313_GPUVA_FULL_OWNER, AGX_LOCAL_RESERVE_V2=1,
IOMFB_FULL_OWNER=1, and macho SHA256
936cbd0314be166d7482170298525b7e6164ace40a43f0d48b3d4359237bd4b5.

Ownership: KMD startup discovers capabilities and advertises topology; each
process negotiates with m1n1 before live publication. m1n1 owns UAT publication
and invalidation. Asahi's direct UAT ownership and Mu's ACPI/reserve contract
remain as documented in the Q22/Q22b design. This correction changes only
KMD diagnostics and client observation; no register, DMA, interrupt, power,
Asahi, firmware, Mu, or recovery behavior is designed or changed.

The WDDM 2.x physical-adapter caps interface is separate from the old
DXGK_VIDMMCAPS.PagingNode field. That omission is not established as this
failure's cause: the decoded node1 receipt and broker negotiations already
disprove absent activation. Do not make an unrelated topology change here.
See Microsoft's [physical adapter caps](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_physicaladaptercaps),
[VIDMM caps](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_vidmmcaps),
and [paging queue fields](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/ns-d3dumddi-d3dddicb_createpagingqueue).

## Implementation and verification

- [x] RED: execute the real node receipt writer for node0 then node1; require
  two separately named, correctly populated records. GREEN: select the name
  by supported node ordinal, preserving version/layout and existing node0.
- [x] RED: execute startup with the real broker client and retained dispatch;
  require a new Wom1LiveAdditiveState record of requested/query attempted/
  answered/status/flags/capabilities/extension ABI/accepted/paging node/epoch.
  Cache the actual discovery response in the client; no second broker query.
  Cover switch off, malformed values, old firmware, wrong ABI, taint, stale
  replies and successful startup followed by per-process negotiation.
  Name acceptance separately from per-process negotiation (which occurs later).
- [x] Run targeted tests, independent review, then the full baseline once;
  require BASELINE_OK without changing known_failures.txt. Make small commits
  without attribution; leave integration ledgers to the integration owner.
- [x] Append REPORT.md with evidence, exact commits/hashes, RED/GREEN logs,
  baseline, limitations and the next checkpoint. No hardware, guest or package build.

Smallest integration checkpoint: on the same pinned firmware, read the new
activation receipt and both node records before Q3PC. Require requested=1,
query answered/success/flags0, capability mask0x2 (bit1), ABI1, accepted=1 and pagingNode=1;
corroborate per-process negotiation with command19 and the failure ring.
The remaining overlap test still requires next_submit < head_complete and
exact outputs, followed by the existing D3D12/GravityMark gates and exact
package rollback. No hardware run is executed by this task.

Remaining cause: Wom1G3PagingWait records an UPDATE_PAGE_TABLE wait for
process21, active process21 and Q3PC head fence2242. Its start QPC1461130783
plus457864 ticks at24MHz gives19.0777ms, ending34.875us after head completion
QPC1461587810. This identifies a CPU BuildPagingBuffer wait despite activation.
The existing classifier still waits
for destructive/partial/system/staging/root changes; the receipt does not
identify which rejected condition was present. Do not bypass those guards
on the strength of the misleading activation diagnosis.

## Verification result

Diagnostics: e01b3df2; resubmission fixture correction: cf3391ff.
Targeted21 tests PASS, including the real startup/retained broker,
live publication, independent queue/worker, control/reset, legacy containment,
resubmission and no-paging-registry/no-user-CRT checks. Independent review
found no critical or important issues and independently decoded the receipts.
The required full command from tests/, with Q22_M1N1_SOURCE pointing to the
assigned m1n1 worktree, returned:

```
ran 857 tests in 341 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

Raw RED/GREEN, targeted and baseline logs, input hashes, decoded evidence,
review and report are in the authorized q22-paging-serialization handoff.
Native WDK and package/hardware checks were not run. Firmware source and
artifact remain unchanged; no m1n1 rebuild is needed.
