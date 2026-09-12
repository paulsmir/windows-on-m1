# GPU current state — 2026-09-12

Worktree: integration/ad04-windows-compiler (linked persistent integration tree).
Compiled source checkpoint:427f0062f86da31bef0c38c5cebd29c8387a2741.
Evidence/workflow checkpoint:3d912c03b87d35f6ef36c2ccdfa4ccf7bff96d52.
Architect feature/j313-gpu-acceleration remains f90402c; no merge or push.

## Operating policy
Latest user decision: direct Tier-A implementation; deterministic build/tests
and first-error extraction. NO default Devstral, reviewer or subagent.
Use Devstral only for genuinely useful large mechanical work, GPU-resident.
Before a decision generate AD04-current-blocker.json from bounded context:
HEAD, last PASS, first error, chain,30–100 source lines, definitions, overlays,
constraints. Read raw evidence only when this packet is insufficient.
Policy: agent_tasks/AD04-LEAN-WORKFLOW.md.

## Latest proven compiler milestone
Full NIR (not stub) and all compiler dependencies build on x64 and ARM64.
Forty Asahi units + two generated units + disassembler + generated libagx
+ existing compute fixture =45 compilation units; both architectures link.
x64 executable variants0/1 PASS, each160 bytes exactly matching native control.
ARM64 executable cross-linked; ARM64 execution NOT_RUN.
No claim of full shader-stage conformance, real agx_screen/context, standard
D3D device, DXGI Present, DWM or accelerated desktop.
Proof: evidence/AD04-fullcompiler-001/verified.json.
Recheck: drivers/apple-agx/mesa/scripts/verify-fullcompiler-evidence.py.
Details/raw paths: agent_tasks/AD04-FULL-COMPILER-RESULT.md.
Closed portability: atomics, agx_index, internal off_t, LUT, math constants,
assembly forced-include guard, Windows null device, FPCR, alloca include.
Do not reopen without a new failing input.

## First current boundary — frontend ownership
Existing Windows device owner and project pipe wrapper are retained.
Native agx_bo needs GPU VA and a defined validity/lifetime; Windows buffer API
currently exposes token/bytes/flags/generation and CPU mapping, not GPU binding.
User approved explicit Windows/VidMm-owned binding separately from residency,
conditional on current virtual-mode proof; mandatory STOP if physical/patch-list.
SOURCE VERDICT: physical/patch-list. ContextInfo has allocation/patch lists;
DRIVERCAPS has no GPU-MMU opt-in; SubmitCommandVirtual/CreateProcess fail closed;
dynamic Render resolves segment PhysicalAddress to internal AGX mapping.
Internal UAT addresses are not persistent VidMm process GPUVA.
STOP: no binding ABI, lifetime tests or frontend implementation authorized past
this gate. Next decision: separate GPUVA migration design versus retained physical
relocation contract. Details: agent_tasks/AD04-GPUVA-MODE-GATE.md.
79-line source packet: agent_tasks/AD04-current-blocker.json.

## Machine / hardware
No Air action this phase. Last verified Air:2026-09-09T13:13:40Z ordinary377/392,
Code28, no AppleAgx package/service/module; SSH8CPU/NVMe/USB/input healthy then.
Do not describe that old check as current live health.
EXP680–682 exact native-derived outputs/TA3D/fences remain closed.
No hardware or new installed package in compiler work. Native-ANS untouched.
Earlier state preserved in agent_tasks/AD04-PRE-MILESTONE-STATE.md.
