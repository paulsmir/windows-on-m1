# GPU current state — integration candidate, 2026-09-12

Authoritative integration branch: integration/ad04-windows-compiler.
Architect checkout feature/j313-gpu-acceleration remains f90402c.
No push, hardware action, or architect branch integration authorized/performed.
Historical long state: agent_tasks/PHASE3-CURRENT-STATE-BEFORE.md.
Mission: FULL_GRAPHICS_MISSION.md; accelerated normal desktop remains unproven.

## Machine / closed hardware
Last Air verification remains 2026-09-09T13:13:40Z ordinary377/392 Code28,
no AppleAgx package/service/module; SSH8CPU/NVMe/USB/input healthy then.
No fresh machine-health claim. EXP680–682 remain closed: exact native-derived
16x16 outputs, repeated TA/3D and fences; private scanout evidence is separate.
No standard Present/DWM/desktop PASS. No next hardware package.

## Worker-chain integration
f90402c -> ea0e0d8 -> 4a0b6f1 -> 21d51e5 -> d7be0d8 ->
d49e6ad -> a39fc61 -> 3416c77 -> ec2b22d -> 8416715 -> 9eb0cc4.
All worker work was one chain, integrated once without duplicate cherry-picks.
Normalization source/evidence:56cae41b465d6262b5e108fdf1292828162d650a.
Evidence: investigation/evidence/PHASE3-normalize/result.md.
u_atomic now avoids signed-overflow UB with tested wrap-boundary returns.
Actual pinned/derived LUT header executable semantics + exhaustive inversion
and extracted upstream agx_index constructor bytes match native controls.
off_t storage mapping remains accepted; inherited int32 branch-displacement
range limitation is recorded, not claimed fixed. Pinned Mesa unchanged.
Unsafe recursive result-root deletion replaced by refusal to overwrite.
Fresh x64/ARM64 controls first fail at M_LOG2E, M_PI, M_1_PI.

## Local worker / runtime
Old CPU-only Ollama0.21.2 was running as Ubuntu WSL ollama.service and occupied
11434, blocking native Windows0.34.0. CPU task cancelled; WSL service stopped
(not disabled); native installed runtime started on127.0.0.1:11434.
RX7900XT ROCm detected20GiB; Devstral exact Q4_K_M digest unchanged.
Warm preflight size_total=size_vram14919579729,ratio1.0,context4096.
LOCAL_DEVSTRAL_GPU=PASS at last check. Every new session rechecks residency.
SHELL_AUTHORITY=NONE; exclusive client lock, separate reviewer context.
keep_alive10m; no CPU fallback. Details: agent_tasks/LOCAL_GPU_POLICY.md.

## Phase3 patch lane
Reviewed deterministic validator in scripts/agent/patch_proposal_lane.py.
Structured exact replacement is converted to diff only with one exact old-text
match in locked HEAD; standard diff goes through same scope/apply gates.
Only fixed approved command IDs execute. This is a policy validator, not an OS
sandbox. Trusted scripts/helper definitions are not supplied by the model.
Real harmless GPU proposal -> apply -> RUN_HARMLESS_CHECK exit0 -> fresh local
review PASS. Demo branch3e89b83 retained; disposable worktree removed cleanly.
Raw evidence: .local/phase3/demo-proposal-gpu-003, demo-applied-002,
demo-review-gpu-001. Early rejected/time-out proposals remain evidence only.

## Current task
AD04-MATH-CONSTANTS-PORTABILITY-006:
source owner proven from pinned Mesa meson Windows pre_args and SDK26100
math.h gate for _USE_MATH_DEFINES -> corecrt_math_defines.h.
Devstral proposed one shared compiler-arguments change in
run-phase3-normalized.ps1. Exact-replacement validator applied it in isolated
task-AD04-MATH-CONSTANTS-006, pending fixed-command validation and local review.
No math values or Mesa algorithms changed. Do not claim boundary closed yet.

## Scope / next gates
No capabilities changed; chosen D3D10_0/FL10_0 mandatory contract incomplete.
After compiler executable: full Asahi backend with Windows resource/submit/
fence, real runtime hardware device, dynamic draws, standard DXGI Present,
DWM, interactive desktop and full stress/reset acceptance.
Keep native-ANS isolated. Event129 remains telemetry absent causal evidence.
