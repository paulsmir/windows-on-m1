# EXP973 — remove production BeginJob upload rehash

## Evidence and source contract

EXP972 full-owner Code0/CPU8/SSH reduced paired DWM BeginJob median to
19.13 ms, still above the <5 ms gate. Its exact package was removed; the
ordinary GPU-visible guest is Code28/PackageAbsent. The supervisor's 16:30Z
source finding identifies `AdmissionG3VerifyUploads` at
`gpuva_g3_windows.c:585` and its call under the G3 lock in
`AdmissionGpuvaG3BeginJob`. The verifier re-hashes up to 64 traced USC-window
uploads via the local CPU view and updates diagnostic counters only. It does
not gate BeginJob success or perform required per-page mapping validation.

Sources inspected: KMD `gpuva_g3_windows.c`, `gpuva_g3_private.h`,
`backend_platform_windows.c` timing ring, G3 replay extraction and copy/private
tests; EXP971 timing plan/verdict and EXP972 evidence; m1n1 retained platform
and broker boundary and Mu J313 AGX ACPI/launch profile as recorded in the
EXP971 plan. Microsoft's `DXGKDDI_SUBMITCOMMANDVIRTUAL` and
`DXGKARG_SUBMITCOMMANDVIRTUAL` documentation confirm that command submission
remains the KMD's responsibility. Asahi's GPU render queue and m1n1's
broker/AGX initialization, interrupt, DMA, power and recovery ownership are
unchanged. Mu's ACPI exposure is unchanged. The Windows KMD still owns all
runtime mapping validation and completion; only its evidence-only rehash is
disabled in the production profile.

## One variable and checkpoint

Compile `AdmissionG3VerifyUploads` and its BeginJob call only when
`ADMISSION_G3_VERIFY_UPLOADS_ON_BEGIN_JOB=1`; default is 0. Keep upload trace
capture, every G4/graph page check, JOB_BEGIN, timing ring, UMD, firmware,
caps, signer and recovery unchanged. A host preprocessor invariant must show
the real BeginJob has no verifier call in production and retains it in an
explicit diagnostic build. G3 copy/private/frame-arm replay must pass.

WHY THIS HYPOTHESIS: (1) EXP972 DWM's paired BeginJob median 19.13 ms leaves a
pre-kick CPU cost after indexed lookup; (2) the current BeginJob calls the
byte-wise verifier on every job under the G3 lock; (3) verification changes
only receipts and cannot explain a required admission or GPU command step.
Expected hardware checkpoint: same full-owner profile, DWM BeginJob median
<5 ms and operator-observed updating physical desktop. If either fails, freeze
timing ring, ETW, UMD and host log, record an explicit verdict, then exact
package cleanup from Code43 and durable Code28. Production versus diagnostic
comparison changes one feature switch; no diagnostic hardware run is planned.

WINDOWS: submit/validation and fence contract unchanged. AGX: same GPU job,
mapping, power and IRQ paths. TRANSLATION: no graph or PTE semantics change.
UNKNOWN: hardware latency reduction and current visual response.
