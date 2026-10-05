# EXP936: rotate directly imported GPU-local backbuffers

WHY THIS HYPOTHESIS:
1. EXP935 same windowed SDK now completes Clear/Flush without device error, and the UMD Present callback succeeds. The next exact failure is _RotateResourceIdentities -> 0x800700aa (ERROR_BUSY), followed by runtime device error887a0020.
2. attach_presentation_render_resource selects Direct for CpuVisible=0. AdmissionUmdScreenAdoptAllocation's Direct branch sets KernelAllocation to the original handle, Borrowed=TRUE and Direct=TRUE; StagingAllocation remains zero.
3. PresentationRotate currently compares StagingAllocation unconditionally in GPUVA builds. That rejects every valid direct primary, independently of outstanding-work guards. Existing split-allocation test missed this supported import mode; a new actual-body direct case fails while legacy and split cases pass.

WINDOWS CONTRACT:
FULL GRAPHICS resource presentation: RotateResourceIdentitiesDXGI rotates kernel allocation identities without changing runtime-visible resource handles, and keeps corresponding resource/view identities coherent. Existing flush/retirement and RTV/SRV rebinding remain required.
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dxgiddi/ns-dxgiddi-dxgi_ddi_base_functions
Pinned WDK26100 dxgiddi.h and current actual generated _RotateResourceIdentities inspected.

AGX/ASAHI CONTRACT:
Current agx_d3d10_windows.cpp and umd_win32_screen.c distinguish direct borrowed native BOs from split canonical/staging imports. Asahi resource identity is retained by RenderResource and RenderBuffer, which the existing rotation moves together. No AGX queue, UAT, power, IRQ, DMA, m1n1 or Mu change. Existing same-boot935 evidence proves actual submit/fence completion and successful private HVC; immutable fullbdcf/MuR143 and recovery377/392 remain the launch contracts. Native batch/framebuffer source inspected in935 continues to govern pending references.

TRANSLATION:
Select the borrowed identity according to the registered ownership mode: Direct -> KernelAllocation; split GPUVA -> StagingAllocation; legacy -> KernelAllocation. Compare it with Resource.KernelAllocation. Preserve Borrowed, Transition, SubmissionHolds and SourceHolds checks, flush/retire, lock, and all existing simultaneous allocation/native-buffer/view rotation. This is one identity-selection correction, not removal of ERROR_BUSY checks.

WHAT IS STILL UNKNOWN:
Whether removing this deterministic refusal permits the same SDK's Present to retain a healthy device and lets DWM compose a correct physical desktop. 935 Present1 returnedDXGI_STATUS_OCCLUDED; occlusion/physical output still need measurement, and success of the UMD callback alone is insufficient.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Actual native rotation replay rejects a valid direct primary with no staging allocation. Add direct, split and legacy fixtures, verify both rotations preserve matching allocation/native-resource/RTV/SRV identities, and verify actual ownership-busy flags plus wrong identity still reject without partial mutation. RED original -> GREEN corrected; no speculative capability changes.

Build936 only after relevant tests; preserve935 evidence then exact-package cleanup through ordinary GPU-visible recovery. Same ce544e78 --windowed probe afterCode0, short12s budget. Collect Clear, Present, Rotate, device status and physical evidence. No stability wait or working-screen claim before actual image. Recovery377/392, hidden385 emergency only.
