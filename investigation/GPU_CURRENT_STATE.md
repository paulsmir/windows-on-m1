# GPU current state — 2026-09-12

Worktree: integration/ad04-windows-compiler (linked persistent integration tree).
Compiled source checkpoint:044b946cf5cbb1ac20cca86bbb55282446e8f1f6.
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
Latest concrete owner decision: agent_tasks/AD04-NATIVE-BO-OWNER-DECISION.md.
Current review at59dd662: finish existing owner slice before native integration.
HasLiveSources lacks a close reservation; TestLock reentry only checks not-yet-
mapped state and does not discriminate Transition. OwnerCookie checks, no-wrap,
CopySource and create reservation remain absent. Earlier remote uploads placed
new tests in src/ instead of tests/; revalidate one immutable current-source build
including EnableMesaPipeFactoryTest before claiming new close/two-hold coverage.
Exact remaining steps: AD04-OWNER-SLICE-REVIEW.md follow-up. No new design gate.
Factory compilation probe after current source reached a builder-input blocker
before agx_d3d10_windows.cpp: pinned Mesa's u_formats.h requires generated
util/format/u_format_gen.h, absent from supplied MesaGeneratedRoot. This is not
a driver verdict. Preserve probe failure; next build input must use the matching
complete Mesa generated directory before evaluating pipe wrapper/close code.
Review AD04-OWNER-SLICE-REVIEW.md finds a9c56bf PARTIAL: acquisition and unmap
did not share a lock, release A twice could consume live hold B, and finalize
still clears busy owner storage. Commita8fc3e1 adds unique hold records and
unmap/deallocate transition reservation; WDK x64 test now covers independent
holds. Commit0d7cc75 adds busy finalization/CloseDevice preservation. Create/map
transition rollback, callback reentry, NativeBo association and ARM64 still
remain. No complete mapping-lifetime proof.

Latest Tier-A review: agent_tasks/AD04-ASTRA-V2-PROVENANCE-REVIEW.md.
Commit01a86f7 closes the first two required corrections: version selection is
explicit and legacy overlay/DMA consumers reject v2 before side effects. The
next correction has two parts. Commitb30bfaa closes exact identity acquisition:
the bridge uses owner-side RetainExact after matching the expected token/serial/
generation/size identity. The remaining required boundary is an authoritative
Windows device-owned native BO association and source-map lifetime; caller bases
still prove arithmetic only. Existing tests do not prove real native provenance
or v2 KMD composition.

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
Follow-up d2c2ad8 makes the existing typed capture/transport carry that v2
command when a distinct fragment USC reference is present. Its deterministic
test models two disjoint, differently-sized subranges retained from one BO;
the command validates as v2 and abort releases both holds. This proves the
wire/capture lifetime invariant only. Native `agx_pool_alloc_aligned_with_bo`
still needs an explicit Windows device-owner adapter before native emission
can feed these references.
Commit044b946 adds that narrow pool-to-capture bridge. It treats the native
pool result as a BO-bound slice, verifies exact CPU/GPU relative offsets and
the queried owner/token/serial/generation, then sends only token+offset+bytes
to typed capture. It explicitly does not export physical addresses, establish
VidMm residency, or allocate a template slot. Host suite PASS. Next: introduce
this bridge at the Windows-native `agx_build_pipeline` callsite through a
device-owned callback seam, retaining pinned Mesa source semantics and without
editing the reference tree.
Pinned FRYZZING validation then compiled the bridge with `/W4 /WX /O2` on x64
(including executable test) and ARM64 from input archive SHA256
`2ab542e5bedf0656309aca6316872e6ef312f77143b671b820fb1b5e04b130e1`.
Evidence: evidence/AD04-native-pool-bridge/windows/
reloc-capture-8d132f7432b84f39903f1746f553c2a4. The earlier input003 package
missed a transitive header and is preserved as an environment-only failure;
input004 is the valid result.

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
