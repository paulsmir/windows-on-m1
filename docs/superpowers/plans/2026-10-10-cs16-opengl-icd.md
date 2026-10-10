# Counter-Strike 1.6 on AGX: OpenGL ICD over the validated G4 stack

Date: 2026-10-10. Status: design. Parent spec:
`docs/superpowers/specs/2026-09-08-windows-mesa-agx-design.md` (acceptance:
CS 1.6 in OpenGL mode on the AGX renderer; software rendering is a control,
never a PASS).

## Why now

- The desktop goal is met on the validated stack (exp/1143-build 5d8fe283,
  integration f0d1c5af): all loads 57-60 fps, 94-99 % one-period pacing,
  EXP1144 24-minute soak clean.
- The guest already has Steam, Half-Life (appid 70) and Counter-Strike
  (appid 10) installed (`steamapps\common\Half-Life\hl.exe`, `cstrike\`).
- `hl.exe` is a 32-bit x86 program. On Windows 11 ARM64 it runs under x86
  emulation, so every in-process graphics DLL it loads must be x86.

## Sources inspected

- This repository (two read-only surveys, 2026-10-10):
  - UMD runtime-callback inventory: every D3D runtime callback on the
    G4/GPUVA path is called from `umd_gpuva_windows.c`,
    `umd_win32_screen.c`, `umd_runtime_device.c` through
    `ADMISSION_UMD_DEVICE.KernelCallbacks`; the Mesa winsys only calls the
    `AGX_WIN32_GPUVA_OPS`, `AGX_WIN32_SCREEN_OPERATIONS`,
    `AGX_WIN32_WINSYS_OPERATIONS` and `AGX_WIN32_ASAHI_OWNER_OPS` tables.
  - The legacy D3DKMT bridge `windows/one-shot/agx_kmt_native_bridge.c`
    already implements `D3DDDI_DEVICECALLBACKS` on D3DKMT for the patch-list
    path (CreateContext, Allocate, Lock, Render, ...), with APIENTRY
    callbacks; it fails the GPUVA device check
    (`umd_runtime_device.c:400-429`).
  - KMD private ABI audit for 32-bit callers: every user->kernel blob
    (escapes incl. G3 private/copy and frame-arm, allocation and context
    create data, G4 submit headers/native render, UMDRIVERPRIVATE device
    info) uses fixed-width fields only, no pointers/HANDLE/SIZE_T, no
    `#pragma pack`; exact size/magic/version checks precede use; the KMD
    never dereferences a user VA from a blob. With the MS x86 ABI (/Zp8)
    layouts equal ARM64. Two hidden pads (`ADMISSION_WIN32_ALLOCATION_CREATE`
    at offset 20, tail of `AGX_WIN32_BUFFER_CLASS_INFO`) and no header-level
    size asserts.
- Mesa 9aa1215f WGL frontend (`src/gallium/frontends/wgl/stw_winsys.h`,
  `stw_device.c`, `stw_framebuffer.c`, `targets/wgl/wgl.c`): an ICD target
  supplies `struct stw_winsys` with `create_screen(HDC)`,
  `present(screen, ctx, res, HDC)`, optional `get_adapter_luid`,
  `create_framebuffer`, `shared_surface_*`; `wgl.c` selects the gallium
  driver (llvmpipe/softpipe present through the GDI software winsys).
  `targets/libgl-gdi` builds a private `opengl32.dll` that loads
  `libgallium_wgl.dll` without ICD registration.
- Earlier controls (CHANGES 2026-09-12): x64 and x86 WGL softpipe builds on
  the builder and an x86 isolated loader control (`wglCreateContext`,
  `wglMakeCurrent`, `glClear` exports) passed; no AGX renderer.
- Microsoft OpenGL ICD loading contract (cited by the parent spec):
  OpenGL driver name/version/flags come from `KMTQAITYPE_UMOPENGLINFO`,
  separate from the D3D `UserModeDriverName`.

## Contracts

WINDOWS CONTRACT:
- An OpenGL ICD (or a private `opengl32.dll` in the application directory)
  is loaded into the application process and has no D3D runtime: it opens
  the adapter (`D3DKMTOpenAdapterFromHdc`/`FromLuid`), creates a device
  (`D3DKMTCreateDevice`), a GPUVA context (`D3DKMTCreateContextVirtual`,
  ClientHint OpenGL), paging queue, monitored fences, allocations, VA
  reservations/maps, residency, submissions (`D3DKMTSubmitCommand`), CPU
  waits/signals, locks and private escapes through the D3DKMT thunks.
- A 32-bit process calls the same thunks through WOW64; driver-private
  blobs reach the KMD byte-for-byte.
- Windowed presentation on the composed desktop may go through GDI: the ICD
  writes the finished frame into the window DC; DWM composes the
  redirection surface.

AGX/ASAHI CONTRACT: unchanged. The ICD submits the same G4 jobs, private
scenes, copy escapes and residency as the D3D10 UMD; the KMD and firmware
path are the validated EXP1143 ones.

TRANSLATION:
- A GPUVA D3DKMT bridge implements the `D3DDDI_DEVICECALLBACKS` subset the
  umd_* code uses (QueryAdapterInfo, CreateContextVirtual, DestroyContext,
  Create/DestroyPagingQueue, Create/DestroySynchronizationObject2,
  Allocate/Deallocate, Reserve/Map/FreeGpuVirtualAddress,
  MakeResident/Evict, WaitForSynchronizationObjectFromCpu, SubmitCommand,
  SignalSynchronizationObjectFromGpu2, SignalSynchronizationObject2,
  Lock/Unlock, Escape, SetError) on the corresponding D3DKMT thunks, so the
  validated staging/residency/submit code is reused unchanged. Runtime
  handles become bridge-owned values; `HANDLE` <-> `D3DKMT_HANDLE`
  conversions are explicit.
- A WGL target `agx` provides `stw_winsys`: one process-wide pipe_screen
  per adapter (the D3D10 path creates one per device), created through the
  bridge and `AgxWin32AsahiScreenCreateForWindows`; `present` maps the
  colour buffer for read (existing download path) and blits it to the DC
  with GDI. One lock per device serialises bridge calls (the umd_* code
  assumes serialised DDI calls).
- Architecture order: ARM64 first (existing arm64 native runtime closure,
  ARM64 GL test program), then x86 (new x86 closure, x86 test program,
  then `hl.exe`).

WHAT IS STILL UNKNOWN:
- Whether Mesa's GL state tracker on the Asahi gallium driver needs
  features the G4 native batch path does not yet encode (GoldSrc uses GL
  1.x immediate mode, fixed function, texture env, alpha test, fog).
  Answered by the ARM64 test program and then the game, one feature group
  at a time.
- Whether the Asahi driver and AGX compiler build and run correctly on a
  32-bit host (pointer-sized assumptions in upstream Mesa). Answered by the
  x86 closure build and host tests.
- WOW64 address-space pressure from CPU staging shadows (the CPU-visible
  footprint roughly equals GPU memory use). Measured in the x86 phase.

## Ownership

| Concern | Owner |
|---|---|
| Adapter/device/context/allocation lifetime | GPUVA KMT bridge (per process) |
| Staging, residency, submit, private scenes | existing umd_* code, unchanged |
| GL state, shaders, draws | Mesa st/mesa + Asahi gallium driver |
| Present | WGL target: readback + GDI blit |
| Everything kernel-side | validated KMD (no change) |

## Phases (one observable variable each)

1. GPUVA KMT bridge (offline). Deterministic translation of each callback
   to its D3DKMT thunk; Windows x64 and x86 builds against the pinned WDK;
   a fake-thunk test exe run on the builder (argument mapping, handle
   conversion, failure rollback). Header-level size asserts for the
   KMD-bound structs (G3 private 224, copy 65600, allocation create 72,
   context create 16, allocation description 48, resource data 28, device
   info 104, G4 V3 200, frame-arm 32) and explicit pads.
2. WGL target `agx` for ARM64 (offline build): `libgallium_wgl.dll` +
   private `opengl32.dll` from the arm64 closure.
3. Hardware: ARM64 GL test program (clear, one triangle, SwapBuffers in a
   window) with the EXP1143 package. Checkpoint: expected pixels in the
   window, frames submitted as G4 jobs, no rejects/TDR. Recovery: standard
   recNNNNh.
4. x86 closure (offline): Mesa/Asahi/compiler/winsys/bridge for x86;
   torn 64-bit fence read fixed (`umd_gpuva_windows.c:1310`).
5. Hardware: x86 GL test program, then `hl.exe -gl -window` with the private
   `opengl32.dll`/`libgallium_wgl.dll` in the Half-Life directory.
6. CS 1.6 correctness/performance work driven by what phase 5 shows.

## Recovery

Each hardware phase uses the GPU-visible baseline and rollback
(`recNNNNh/recover.sh`). The private GL DLLs are experiment-local files in
a test directory (phase 3) or the Half-Life directory (phase 5) and are
removed in rollback together with the package; no ICD registry value is
written before phase 6.
