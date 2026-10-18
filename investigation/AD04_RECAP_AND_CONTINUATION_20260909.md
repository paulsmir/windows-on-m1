# Apple AGX Full Graphics checkpoint — 2026-09-09

User requested a deliberate stop, complete recap, commit/push and continuation
instructions to preserve remaining account usage. This is NOT mission completion.
Work is in `/Users/pavel/public_windows`, branch `feature/j313-gpu-acceleration`.
Last implementation/ledger HEAD before this checkpoint:
`853e9365b88d29fd865c12d0c0c8e143be7632cd`.

## Goal, not a smaller substitute

Primary acceptance is a usable **hardware-accelerated ordinary Windows desktop**.
OpenGL/WGL and CS 1.6 follow this milestone. Read
`FULL_GRAPHICS_DESKTOP_ACCEPTANCE.md` and the preserved full mission in
`FULL_GRAPHICS_MISSION.md`. All requirements remain mandatory: normal installed
boot, real runtime hardware D3D device, complete truthful selected feature level,
dynamic application rendering, normal WDDM resource ownership, standard DXGI
Present, DWM hardware correlation, usable desktop, >=1000 standard Presents,
>=100 resource/window lifecycle cycles, >=30 minutes stability, supported
reset/re-entry. No software fallback, replay-only renderer or private flip PASS.

## Authoritative machine state

Last actual Air health verification: 2026-09-09T13:13:40Z, after EXP682 cleanup.
Ordinary377/392; APPL0002 Code28/null INF; no experimental AppleAgx package,
service/module/SYS/UMD; SSH, 8 CPUs, NVMe2, USB5, keyboard1;
no fresh Events41/1001/129. This is historical verified state, not a current poll.
No hardware launch or installation occurred during subsequent AD04 work.
No next installable hardware candidate is prepared.

Air SSH: `pavel@192.168.1.37`, identity `/Users/pavel/.ssh/air`;
use `-o BatchMode=yes -o ConnectTimeout=8` and
`-o UserKnownHostsFile=.local/experiments/EXP641-standard-present/air_known_hosts`.
Default known_hosts has a conflicting old host identity; do not disable checking.
Builder: `pauls@192.168.1.24` (FRYZZING),
identity `/Users/pavel/.ssh/windows_builder`, same bounded SSH options.

## Hardware proven — do not repeat

- Retained-root/broker/context0, firmware/RTKit and physical TA/3D/fence foundations.
- EXP680: independent tiled64 oracle and immutable raw snapshot; gray72 + bg184.
- EXP681: original native-red output matches EXP659 immutable raw1024 bytes,
  72 red BGRA words `0xffff0000`,184 background `0xff112233`, guard/poison0.
- EXP682: red/gray/red/gray in four different process/device/context/allocation
  lifetimes; exact raw images; fences271/289/308/326; generations1/2/3/4;
  TA done2/3/4/5 and D3 done2/4/6/8. HOLD15.030s healthy.
- Four-frame terminal snapshots precede Notify/DPC scalar updates; do not claim
  their zero fields directly prove four-frame DPC receipts.
- No standard runtime Present/DWM/desktop PASS. Earlier private full-frame
  scanout keeps its narrow proven scope.

Evidence remains in `.local/experiments/EXP681-native-red-oracle/` and
`.local/experiments/EXP682-four-native-frames/`. These large local artifacts are
NOT Git-hosted merely because this handoff is committed.

## Implemented and verified offline

| Commit | Change / proof |
| --- | --- |
| a433262fbd037effedc0ef7afda62d8a870967bf | Common runtime-device init/finalize, typed D3D10_0/WDDM1_3 callbacks, per-device error owner. x64 executable tests, ARM64 UMD build/sign. |
| 23ff1622f6ac1253031011bea70756c8e2809fd6 | Shared adapter metadata initialization; validate before publish, no global pipe screen. |
| a4f0b0f5bf4d2a7333df5b27c6ec9f7a433a98d2 | Checked pipe-screen/context release; busy children remain alive. ASan/UBSan and MSVC tests. |
| b66c1a8aa06c75d48b2ac063ae47bbb0ff28a469 | Per-device pipe factory consumes existing Windows winsys; generation/owner checks and checked teardown. |
| 724d30f8f5c6faf546e91db830c1b12bd5d68d4d | Actual selected Mesa Adapter/Device entry points derive from pinned source and use Windows owners; fail before missing shader callback; owner composition tests. |

Pinned frontend Mesa is `9aa1215f878b504f66159dd2ead4c7973142126e` in
`.local/reference/mesa`. Native EXP659 graph reference uses a different commit
`7a4f24061fa56ef7eff12132dd7b1461d5a890d8`; never mix identities.
Source/license contract: `drivers/apple-agx/mesa/mesa-ad03-source-contract.json`.

Actual frontend derivation tool: `tools/prepare_apple_agx_d3d10_frontend.py`.
Owner bridge: `drivers/apple-agx/mesa/winsys/agx_d3d10_windows.{h,cpp}`.
Common runtime: `drivers/apple-agx/render-admission/umd/src/umd_runtime_device.c`.
Current selected frontend compiled15 units x64/ARM64 into static libraries:

- x64 SHA256 `e7f4cdcc337330cf07b10f9a030d12e1e81a49f357e3a667da26786d6b57d7c9`.
- ARM64 SHA256 `c523b9b9f23804a1ded244c31c577ab767af4d446cd92276f9079d2a4c5417c3`.
- `.local/experiments/AD04-selected-frontend/evidence/{x64-b4,ARM64-b4}/`.
- Existing upstream conversion warnings remain; this is not full UMD analysis.
- Owner x64 executable SHA256
  `d707a13865c40a5fbf3e007f480d210089bcbfb12f52367425707e1636324417`;
  ARM64 compile-only `005193f896cf7d6281de0a906307d795a7523bfef47ac1fe7efa511a7f90e4bf`.
  Evidence `.local/experiments/AD04-frontend-owner/evidence/`.

Known limitations, not hidden successes:

1. `AGX_WIN32_PIPE_CONTEXT` is only a resource/mapping control context, NOT
   `struct agx_context`. Native `agx_create_shader_state` casts to full Asahi
   context/screen/device. Do not graft callbacks onto the small structure.
2. Selected frontend still contains software-oriented Resource/DXGI behavior,
   `flush_frontbuffer` / `DXGI_STATUS_NO_REDIRECTION`; not production Present.
3. Shared runtime finalizer currently ignores DestroyContext HRESULT and clears
   state. This inherited policy is not proof of safe failed runtime teardown.
4. Pipeline caps remain0; selected FL10_0's121 mandatory rows are incomplete.
5. Full production DLL/backend is not linked, installed or hardware qualified.

## Exact final executed work: compiler Windows compile control

No hardware EXP number was consumed. Local and builder directories:
`.local/experiments/AD04-asahi-windows-compiler/` and
`C:\Users\pauls\AD04-asahi-windows-compiler`.
Small scripts/logs are preserved in `investigation/evidence/AD04-asahi-windows-compiler/`.

An actual MSVC14.44.35207 x64 compile of pinned `agx_compile.c` was attempted:

- b1 failed: missing HAVE_STRUCT_TIMESPEC and DRM fourcc transitively included
  BSD `sys/ioccom.h` through `asahi/layout/layout.h`.
- b2 explicitly uses Windows SDK timespec and a separate derived fourcc header
  preserving all modifier values/notices, replacing only DRM ioctl include with
  uint64_t typedef. No GPU semantics changed. Those two errors disappeared.
- b2 then fails native `static_assert(sizeof(agx_index)==8,"packed")` under MSVC
  mixed-type bitfield layout, and missing `off_t` declaration; subsequent syntax
  errors cascade. Do not delete the assertion or silently change packing.
- Generated C/headers came from local pinned Mesa build; these are inputs only,
  not proof that macOS feature macros apply to Windows. Current script is an
  investigative compile control, NOT the final reproducible production build.

Next compiler path is clang-cl with explicit internal layout verification and
proper Windows feature detection; Mesa documents clang-cl use in an MSVC shell.
Do not apply a global layout flag across Windows ABI structures blindly.
First isolate/prove internal Asahi layout versus WDK structs. Missing `off_t`
must be traced to its actual use and correct width, not guessed.

Official LLVM20.1.8 portable extraction was started, not a global installer:
`prepare-clang.ps1` downloads LLVM-20.1.8-win64.exe, verifies official release
SHA256 `3197846a2b19063687dd56e93e34cd941e3548d907f23a6131571321bdf9fe7b`,
then 7-Zip extracts under builder directory `llvm20`, then runs version check.
Preparation completed ExitCode0; `clang-cl --version` returned20.1.8,
target x86_64-pc-windows-msvc. No compiler invocation with clang occurred yet.
Revalidate executable availability when resuming.
Never restart a live operation simply because a prior tool session was lost.

## Next executable development actions

1. Read compact state and this handoff, check HEAD/diff and any new evidence.
   Confirm LLVM preparation terminal result. No Air reboot needed for this work.
2. Finish executable Windows Asahi compiler test, not merely one object compile:
   build compiler+NIR/util dependencies, link/run dynamic VS/FS fixture on x64,
   compile ARM64; independently check generated instructions/output metadata.
   Reuse pinned production compiler, not a second materializer.
3. Integrate real Asahi screen/context as a coherent backend with Windows BO,
   submit and fence operations. Inspect current source actual dependencies:
   `agx_pipe.c:agx_screen_create/agx_create_context`, `agx_device.c`,
   `agx_device.h:agx_device_ops_t`, `agx_batch.c`, `agx_bo.c`.
   Device ops alone do NOT abstract direct syncobj/queue/VM calls elsewhere.
   Existing Win winsys operations have create/map/unmap/destroy/clear/wait/retire;
   BuildDraw exists but is not a complete generic Gallium backend submit API.
4. Complete runtime resource/shared ownership and mandatory selected frontend
   matrix, then linked UMD tests/analysis/Universal/sign/hash. No premature caps.
5. Hardware: normal runtime hardware D3D device -> dynamic draw -> physical
   TA/3D -> exact fence -> full output oracle. Only then standard Present/DWM.
6. Desktop lifecycle/stability/reset acceptance; leave final accepted package
   installed. CS1.6/OpenGL follow, not a substitute for desktop.

## Preservation and restart prompt

Unrelated dirty root files, Mu changes and m1n1 untracked state were NOT mixed
into production commits. Recovery copies are under
`.local/checkpoints/20260909-ad04-pause/`: root/index/submodule binary diffs,
untracked tar archives and status. Originals remain in place. These local
backups are not automatically remote-backed and not hardware candidates.
Never `git reset --hard`, `git clean`, or apply the whole dirty overlay to a build.

Continuation prompt:

> Continue /Users/pavel/public_windows, current feature/j313-gpu-acceleration.
> Read AGENTS.md, GPU_CURRENT_STATE.md, AD04_RECAP_AND_CONTINUATION_20260909.md,
> GPU_ENGINEERING_PROCEDURE.md and FULL_GRAPHICS_MISSION.md. Main process only.
> Preserve EXP680–682 and earlier proven hardware. Resume actual Windows Asahi
> compiler/backend integration from AD04 compiler b2 and LLVM preparation;
> do not repeat source-only adapter/helper milestones or launch an incomplete
> hardware UMD. Execute and test the real production chain, then standard
> D3D/Present/DWM and complete desktop acceptance. Keep unrelated dirty/native-ANS
> state isolated. No new human approval gate for routine authorized work.
