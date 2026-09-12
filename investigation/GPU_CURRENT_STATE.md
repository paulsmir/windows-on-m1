# GPU current state — 2026-09-12

Worktree: integration/ad04-windows-compiler (linked persistent integration tree).
Compiled source checkpoint:29f41e7c3a9fb4dbe71b2a2a6aac0750a1ccd039.
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

## First current boundary — physical relocation adapter
VidMm input-domain gate completed, current-target GPUVA migration NO.
Do not implement the proposed GPUVA slice or enable new caps.
Classic has4KiB logical updates and4K/64K page-table choices; the documented
16KiB hardware projection is not a negotiated16KiB-only input guarantee.
WDK26100 does contain DXGK_PAGESIZE_16KB underWDDM3_2, consumed by the new
page-based family. Public16K docs are prerelease; current driver is WDDM3.0,
with no established supported/negotiated replacement contract. No live Air
feature query was performed. This is current-target no-go, not a universal
claim that future/larger-page Windows implementations are impossible.
Decision/primary sources: agent_tasks/AD04-VIDMM-INPUT-DOMAIN-GATE.md,
commit eed8ea2f0a70c46ad59a6419ba4dbb7e67a0438d.
The238-check model remains a scoped PASS with explicit arbitrary4K
counterexamples; it never proved the VidMm input domain.
Next assessed direction: existing device owner + real Asahi BO/pool pointer
provenance -> typed relocation records -> existing physical Render/Patch/
SubmitCommand and exact fences. Initial offline direct-draw closure only;
no untracked pointer scanning, no fake GPUVA, no residency inferred from BO.
Latest user stop-policy: Tier-A decides reversible architecture internally and
continues. Hardware runs authorized after exact build/sign/hash/preflight/recovery
gates; do not ask at ordinary architecture checkpoints. GPUVA remains CLOSED/NO.

## Current implementation checkpoint
6725ae1b2da94a909c3978905235e00394c14c0f adds device/request-scoped typed capture
using existing wire ABI and KMD materializer. Host ASan/UBSan and Windows x64
build/link/execute + ARM64 build/link PASS; current source hashes verified.
Native USC pack/unpack revealed a real production defect: address relocation
discarded uniform size bits24/25. Mask now preserves26 low bits; native counts
1..64 pass at two placements, with unchanged non-address bytes and first image.
BO retention is not VidMm residency. Capture still must be wired into actual
native BO/pool/USC/encoder emission and the real agx_screen/context platform seam.
No standard D3D device or Full Graphics acceptance follows from this checkpoint.
Evidence/runner repairs/limits: agent_tasks/AD04-TYPED-RELOCATION-RESULT.md.
The first ABI slice for the source-backed pipeline mismatch is now implemented:
29f41e7c adds wire-command v2 for Draw only. It preserves the 128-byte v1 draw
payload and v1 behavior; v2 interprets Reserved[0] solely as a second, distinct
fragment USC pipeline reference and validates its typed reachability. Clear stays
v1. Host deterministic ABI/relocation/overlay/transport suites pass. This does
not assign arbitrary EXP208 template offsets to either native allocation and does
not yet wire the v2 bindings into dynamic DMA or a native pool. Next: an
owner-proven native pool/BO allocation adapter and v2 composition path, then
per-device Windows agx_screen/agx_context operations. Decision:
agent_tasks/AD04-NATIVE-PIPELINE-DECISION.md.

## Machine / hardware
No Air boot/install action this phase. User reports Running proxy; passive USB
confirmed m1n1 uartproxy and expected two serial endpoints on2026-09-12.
No current Windows SSH/package preflight yet. Last verified Windows health:
2026-09-09T13:13:40Z ordinary377/392,
Code28, no AppleAgx package/service/module; SSH8CPU/NVMe/USB/input healthy then.
Do not describe that old check as current live health.
EXP680–682 exact native-derived outputs/TA3D/fences remain closed.
No hardware or new installed package in compiler work. Native-ANS untouched.
Earlier state preserved in agent_tasks/AD04-PRE-MILESTONE-STATE.md.
