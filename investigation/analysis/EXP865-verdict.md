# EXP865 — owner admission confirmed; later TA completion fails

One package865/source26cbd7e6 cold armed boot on unchanged R143. Exact diff is
the source/test portion of6b7f8c79 on EXP864a9ecd3ea; Flush0f51f3f5 excluded.
The ten-minute stability and visible-frame checkpoints failed. No retry/rearm.

## Measured result

Live141 at39.160s CPU3 waits for preemption completion; kernel116 at39.288s
CPU1 reports parameters(ffffd00f21f42030,fffff800d1fa6cb0,ffffffffc0000483,3).
Matching865 private PDB resolves ResetFromTimeout. Active-private reset remains
fail-closed, so this stack is recovery failure rather than the initial cause.
StartStage12/status0 and arm consumption survived; all8 CPUs entered. Original
Code0/SSH was never sampled (three connection failures). No DWM/memory trend
or physical display/input health was observed in the original boot.

## Proven advance and current boundary

Kernel dump03d421e1c00558795220a10f0ff8e790a6d3dea868a31520dfb0bb34a3193b20
and matching type layouts identify adapterffffd00f20b02000; its PDO matches
ACPI APPL0002ffffd00f179f48f0. Backend and pending submission both carry63,
and backend is Submitted for fence220. This directly excludes the prior
owner1-versus63 refusal.

G3LastCompletedFence149 and queue Completion149/Success agree. The real
AdmissionBackendComplete path requires both TA and D3 completion before
AdmissionGpuvaG3CompleteJob can publish LastCompletedFence. Scheduler is
Completed219/Active220;219 includes CPU work and is not219 GPU renders.
Thus the first private completion checkpoint advanced beyond EXP864.

For current220, provider/runtime are Faulted. Last saved TA done pointer3
matches expected3, but stamp7a000000 differs from7a000100; event0 unseen,
TaComplete0. D3 pointer4/stamp3d000100 match their expectations; event1 seen,
D3Complete1. This is the saved software observation, not a claim that all
second-job work or pixels are correct; completion freshness is part of the
next offline boundary. Scene220 is Queued+Started/GpuDone0/Reported0 and
quarantined. Graph6/root9d6490000/gen895 holds lease2/JobInFlight1.

QUERYv3predicate24 is processPoisoned on that same graph, before context/range
lookup. It does not diagnose an absent mapping. No G4 rejection receipt was
persisted; backend Submitted supplies the separate acceptance evidence. No
queue-fault receipt is present; SUBMIT-only fault telemetry is excluded from
this G3 profile, and SGX MMIO pages are absent from the dump. Firmware/UAT
fault provenance remains unknown. Do not infer no fault from absent pages.

Inspected source contracts: render-admission/src/backend_platform_windows.c
AdmissionBackendComplete and worker; gpuva_g3_windows.c CompleteJob/private
reset; shared/src/apple_agx_backend_runtime.c completion gating. KMD owns
submission/completion/reset; unchanged broker/firmware and Mu contracts remain
owned by their existing layers. No code or timing/capability changes follow.

## Evidence and recovery

Exactly one original scanout snapshot(seq2/offset130000/PA8e0130000) has
nonzero0. The earlier ledger phrase “both snapshots” is corrected append-only.
UMD445 lines44416B has zero reject-seterror records and no render completion
timeline. Original ETL could not be copied before the spontaneous reset because
SSH never became reachable. Recovery ETL is distinctly named and excluded;
no original DMA timeline claim is made. Old14000 minidump is also excluded.

All27 guest evidence files754887182B were copied and SHA-verified before
cleanup, including full kernel/current minidump/watchdog and durable receipts.
The watchdog collector's final console byte metric failed on ordered-dictionary
Measure-Object; writes were complete and host verification independently
verified the whole manifest without repeating evidence collection.

HiddenCode45 exactoem5/package865 removal completed. OrdinaryCode28 durable
12:23:35.3742925Z: present1/package0/arms0/files0/service0/signer0/diagnostics0,
CPU8/disks2OK/USB5/RDP, intentional autologon retained. Later read-only boot
query still had SSH at12:28:41Z/uptime387s. Hidden+122.914s and ordinary+115.500s
clock corrections explain absolute-time offsets; failure duration uses uptime.

Result main .local/experiments/EXP865-r150-owner/hardware-result.json
SHA256 a84a247f33998570b640b47546daf8627f375bd21cf1c5c4ccad84a341489542. Raw artifacts, matching PDBs and debugger outputs remain
in that directory; tracked index and decoded evidence are investigation/evidence/EXP865.

WHY CONTINUE COMPARISON: owner correction now crosses submission and first
TA/D3 completion. A later accepted220 lacks TA event/stamp while D3 is marked
complete. Next thread investigates that exact boundary offline; no new package,
firmware edit, rearm or hardware experiment is authorized by this verdict.
