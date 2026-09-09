# GPU current state

Primary goal is now FULL GRAPHICS WINDOWS DESKTOP; OpenGL/CS1.6 are later.
Full acceptance and current unmet requirements: FULL_GRAPHICS_DESKTOP_ACCEPTANCE.md.
Goal remains active; all eleven final requirements need integrated hardware proof.

Updated 2026-09-09T13:19Z. Main process only; no agents.

## Machine
Ordinary377/392 restored after EXP682. Verified13:13:40Z: APPL0002 Code28,
null INF; no AppleAgx package/service/module/SYS/UMD; SSH,8CPU,NVMe2,USB5,
keyboard1; no fresh41/1001/129. Ordinary launcher76592.
Recovery: .local/experiments/EXP682-four-native-frames/recovery.log.

## Hardware proven
EXP680 gray raw output: independent tiled64 image,72 pixels80808080 and184
backgroundff112233; immutable1096-byte snapshot with1024 bytes; fence271.
EXP681 original native-red: raw1024 bytes equal immutable EXP659 attachment,
72 BGRA redffff0000 and184 background; poison0/guard0; physical TA3D/fence271.
NATIVE_FRAGMENT_RED_HW_PROVEN=YES.
FRAGMENT_OUTPUT_CORRECTNESS_HW_PROVEN=YES within the native16x16 workload.

EXP682 four distinct allocation lifetimes: red/gray/red/gray, all raw images
byte-exact against independent expectations; fences271/289/308/326;
generations1/2/3/4. GPU VA1500fa0000/1500fb0000/1500fc0000/1500fd0000;
PA97d190000/97d1a0000/97d1b0000/97d1c0000.
Physical TA done2/3/4/5 and D3 done2/4/6/8 match expectations.
HOLD15.030s: four producers alive, last snapshot unchanged, Code0,8CPU,
no41/1001/129. Repeated raw rendering and HOLD are proven.
Terminal output snapshots precede their Notify/DPC scalar update; those zeros
are not direct four-frame DPC evidence. Output follows the existing notified
transaction guard; the bounded two-call correlation is separate evidence.
No standard Present or desktop PASS.

Evidence:
- .local/experiments/EXP681-native-red-oracle/evidence/
- .local/experiments/EXP682-four-native-frames/evidence/frame1..4 and hold/
- Detailed source/launch/hash/results in EXPERIMENTS.md and CHANGES.csv.
- Earlier long state preserved at .local/experiments/EXP682-four-native-frames/evidence/GPU_CURRENT_STATE-before-compaction.md.

## Closed interpretations
Do not repeat EXP674–682. EXP674 collided with poison, not a positive write
proof. EXP678 did not establish a live-allocation race. Oracle now decodes
tiled64 and separates expected/observed. EXP679 erased runtime expected during
release; d146fdaf47ad580dcc7de73ae3c295d5fd9231f6 retains it in CompletedOutput.View.
EXP680/681/682 validate that correction. Old red FNV98b3446c1b0a8215 matches
native red; EXP681 raw evidence supersedes old zero-colour scalar claims.
Do not reopen VA/UAT/PBE/store/encoder/RTKit without contrary evidence.

## Next causal boundary
Standard runtime-managed Present/DWM remains unproven. Source review:
EXP682_STANDARD_PRESENT_BOUNDARY.md.
EXP644/647/648 remain valid: BLT occluded due to DWM device failure, exclusive
ownership denied, UMD loaded with pipeline0 and no CreateDevice. Current UMD
still has a partial WDDM1_3 table. Selected architecture remains Mesa D3D10_0/
FL10_0 reuse;121 mandatory implementation rows are incomplete.
Do not enable caps or repeat rejected admission/desktop/exclusive experiments.

Next implementation: selected Mesa D3D frontend with device-scoped Windows
allocator/context callbacks and existing winsys, including real shared-resource
ownership. Upstream software target, fake D3DKMT handles, flush_frontbuffer and
NO_REDIRECTION are not hardware presentation. Reconcile adapter-scoped screen
factory with device-scoped callbacks without a process-global device singleton.
Displayable resource geometry/layout is also required: a small tiled render
target cannot be relabelled as a full-size linear primary.
No additional BLT patch was made; it would not solve runtime device admission.
Keep pipeline mask0 until mandatory callbacks/backend invariants pass.
No next hardware candidate is prepared.

AD04 frontend compile control now passes x64 and ARM64 for15 pinned Mesa
translation units, excluding D3DKMT.cpp software shims and d3d10_gdi.c.
This is a static-library build, not a linked hardware UMD or production analysis
PASS. Upstream conversion warnings remain recorded. Script commit
b7094b1b6b00991806bc566ac1abf7eb215767bc; artifacts under
.local/experiments/AD04-d3d10-frontend-build/results/.
ARM64 library SHA256e957ea442643da32f0bec835edcc867a89e1d52b1dceb34dd8dbf8d3c99b8c7f.
Existing agx_win32_pipe_screen.c already owns a device-scoped Gallium bridge;
reuse it and its tested Windows transport rather than introducing a new screen.

2026-09-09T13:48:53Z: Windows runtime-device ownership is now extracted into
umd/src/umd_runtime_device.c, used by the existing UMD and independently
linkable without OpenAdapter exports. Source commit
a433262fbd037effedc0ef7afda62d8a870967bf. Initializer accepts typed D3D10_0 or
WDDM1_3 runtime callback views, retains a per-device error callback, creates
the existing Windows context/winsys, and shares existing finalization.
WDK x64 tests prove two distinct devices/contexts/buffers/generations, error
owner isolation, malformed/context-create/winsys-init rollback, unsupported
interface and missing callback rejection. Final test ExitCode0; ARM64 UMD
build/sign passes with0 production warnings/errors. No hardware run/install.
DLL SHA256e466d774530e25789524ed8a7dfb1eff70218157dcefda752e190e8a97995e46.
Evidence: .local/experiments/AD04-runtime-device-bridge/evidence/.
This does not complete Mesa CreateDevice/DDI tables or raise any caps.
Next: attach this runtime owner and existing pipe screen to the selected Mesa
per-device factory; implement missing backend callbacks before device admission.

Adapter metadata initialization is also shared, commit
23ff1622f6ac1253031011bea70756c8e2809fd6. It queries the exact runtime adapter
callback into a validated local candidate before publishing caller-owned state.
No adapter-global pipe screen, DDI table or caps are created by this initializer.
Existing UMD OpenAdapter uses it. WDK x64 tests prove two independent adapter
identities and malformed/missing-callback rejection; ARM64 UMD build/sign passes
with0 production warnings/errors. Evidence:
.local/experiments/AD04-runtime-adapter-bridge/evidence/.
Next concrete integration: selected Mesa adapter owns metadata only; each
CreateDevice owns its common Windows runtime plus existing pipe screen/context.
Check child resource/context retirement before releasing the runtime owner.
No new hardware EXP and no capability change from these offline components.

Checked Gallium pair release now implemented at
a4f0b0f5bf4d2a7333df5b27c6ec9f7a433a98d2:
AgxWin32PipeScreenReleaseDevice rejects live resources, extra contexts, foreign
context and extra screen references without mutation. Success releases the
owned screen/context pair and leaves the borrowed Windows winsys alive.
Caller serializes teardown. ASan/UBSan and pinned MSVC analysis/test PASS;
evidence .local/experiments/AD04-pipe-release/evidence/.
This is CPU object lifetime, not a physical residency guarantee.
The actual Mesa per-device factory attachment remains unfinished. Its guard
must use this checked release before Windows runtime finalization, and it must
not call CreateEmptyShader until required pipe shader/state callbacks exist.
The current pipe_context exposes mapping/resource operations, not a complete
D3D graphics pipeline. Keep caps0 and do not launch admission hardware yet.

## Preserved constraints
Retained root/broker, firmware/RTKit, context0 inventory, context63 memory,
physical TA3D and completion remain controls. EXP640/651 retain their private
full-frame scanout scope, not standard Present/DWM.
Native graph reference is EXP659/Mesa7a4f2406; generic frontend selection is
Mesa9aa1215f. Keep these distinct.
Latest tested package30.0.680.0; red producer metadata source
0fe2f489f94075cea7a0e733fc4a274efebf0c85.
Do not touch native-ANS or unrelated dirty work. Event129 remains telemetry
without causal evidence. Exact cleanup and ordinary recovery between tests.
Roadmap: ACCELERATED_DESKTOP_ROADMAP.md.
Selected contract: docs/superpowers/specs/accelerated-desktop-contract.json.
Mission remains accelerated desktop, then accelerated OpenGL and CS1.6.
Leave the final accepted working desktop package installed.
