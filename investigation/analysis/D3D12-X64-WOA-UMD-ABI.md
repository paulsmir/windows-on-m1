# D3D12 x64 applications on Windows on Arm: UMD ABI boundary

Status 2026-10-20: EXP1800 confirmed the selected UMD name and the package's
missing x64-compatible image. The correction is planned in the UMD/package
layer; no new driver binary is accepted yet. The first-release target is
SM5.1 with games. A native ARM64 benchmark cannot substitute for x64 games.

## Primary sources and observed evidence

- Microsoft: [Arm64EC interoperability](https://learn.microsoft.com/en-us/windows/arm/arm64ec),
  [Arm64X PE files](https://learn.microsoft.com/en-us/windows/arm/arm64x-pe),
  [building Arm64X](https://learn.microsoft.com/en-us/windows/arm/arm64x-build),
  [WDDM 64-bit UMD installation](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/microsoft-windows-vista-display-driver-64-bit-issues),
  [WDDM UMD loading](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/user-mode-display-drivers),
  and [DXGI error meanings](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-error).
- Current owner sources: `drivers/apple-agx/render-admission/AppleAgxRenderAdmission.inf`,
  `umd/AppleAgxRenderAdmissionUmd.vcxproj`, `umd/AppleAgxRenderAdmissionUmd.def`,
  `umd/build-umd-x86.ps1`, `umd/src/umd.c`,
  `drivers/apple-agx/mesa/scripts/build-native-asahi-state.py`,
  `build-asahi-runtime-closure.py`, and
  `drivers/apple-agx/windows/icd/agx_kmt_umdname.c`. The existing native
  project is gated to Release ARM64. The separately built x86 UMD proves the
  frontend can be built for another CPU ABI; the Mesa/runtime scripts explicitly
  support x64 but require an architecture-matched closure.
- EXP1795: pinned Microsoft nBody x64 PE `0x8664` called
  `D3D12CreateDevice(adapter, FL11_0)` and received `0x887A0004`
  (`DXGI_ERROR_UNSUPPORTED`) before any frame. WER did not list AppleAgx UMD
  at crash. The same exact package passed 60 named native D3D12 checks, with
  one known C21E skip; thus no general KMD/AGX rendering failure follows.
- EXP1798 normal Code28 control: x64 KMT diagnostic ran successfully and
  reported only system `d3d10warp.dll` UMD names.
- EXP1800 exact signed package: x64 and ARM64 same-source KMT diagnostics
  returned byte-identical outputs. Both see AGX LUID low `0x4952f` and
  `AppleAgxRenderAdmissionUmd.dll` for DX9/10/11/12. The named package DLL
  is ordinary ARM64 PE `0xAA64` (SHA
  `6f001b13341b2489103761897c5649dfcbaf05ac1094442ac4d2b1be0c3958c8`);
  the WOW DLL is x86 PE `0x014c`. There is no x64 or Arm64EC image in the
  six-file signed package. Root verdict and 51-file evidence manifest are in
  `.local/experiments/EXP1800-kmt-abi-pair`.

## WINDOWS CONTRACT:

The Direct3D runtime loads a user-mode display driver DLL into the calling
process. An x64/Arm64EC process on Windows on Arm can load x64 or Arm64EC
binaries, and an ARM64 process can load ARM64 binaries; ordinary ARM64 code
cannot be loaded into the x64/Arm64EC process. The current KMT result gives
both 64-bit processes the same UMD filename. Microsoft documents Arm64X as a
single PE that presents the appropriate architecture to both processes.
Arm64X can be built as a merged binary, or as a small architecture-selecting
forwarder to separate ARM64 and x64/Arm64EC DLLs. The latter is the smallest
candidate because the existing ARM64 UMD is already qualified.

The INF's `UserModeDriverName` currently names the ARM64-only DLL four times;
`UserModeDriverNameWow` names the x86 DLL for 32-bit apps. EXP1800 measured
that an x64 caller receives the native name, so simply adding a differently
named x64 DLL without changing the registered entry point would not make the
runtime load it. The exact x64 loader error is still unmeasured, but the ABI
mismatch is deterministic from the returned name, PE machines and Microsoft
process compatibility rule.

## AGX/ASAHI CONTRACT:

The Asahi/Mesa winsys and the AppleAgx KMD retain the EXP1765/1795 qualified
AGX queue, memory, power and recovery contract. This task changes the CPU ABI
of the user-mode entry point and its dependency closure, not AGX register
values, RTKit, UAT, interrupts, DART, paging or submission semantics. The
existing x86 UMD uses the same Gallium/native frontend source with an x86
Mesa and runtime closure. The x64 counterpart must be built from one pinned
source snapshot and architecture-matched x64 Mesa, Asahi and C runtime inputs;
mixing ARM64 or old x64 archives is forbidden.

Current m1n1 commit `5b8576031eaae5958c5494d4f22f5a6c3a25d191` and Mu commit
`361a2134b2f742f331f8d75ae5f60ad4f7f413f1` expose the same APPL0002,
firmware, GPU power and ACPI state as EXP1765/1800. Neither owns UMD
architecture selection or should change to conceal this failure. Recovery
stays the normal GPU-visible FactoryV2 exact-package path.

## TRANSLATION:

1. Produce a *separate* x64 UMD from the exact package source snapshot,
   using the current x64 Mesa/NIR/Asahi runtime closure and the same exported
   `OpenAdapter10_2` and `OpenAdapter12` functions. The existing
   `build-umd-x86.ps1` supplies the minimal source/object/link inventory;
   adapt the target, libraries and output name without altering DDI behavior.
2. Keep the qualified ARM64 UMD byte-identical under an architecture-specific
   filename. Create an Arm64X pure forwarder as the registered
   `AppleAgxRenderAdmissionUmd.dll`, dispatching those two exports to the
   ARM64 or x64 backing DLL according to the loader's process view. Preserve
   the x86 `UserModeDriverNameWow` and its package location.
3. Before packaging, check x64/ARM64/Arm64X PE views, exact exports, imports,
   stack frames, source manifest, license notices and both architecture
   dependency sets. Stage a loose forwarder/backing-DLL set in the normal
   Code28 guest and prove x64 and ARM64 `LoadLibrary`/`GetProcAddress`
   independently. These are loader checks, not GPU claims.
4. Add the three files to INF/CAT with exact names and hashes, package-sign
   once, then run one J313 D3D12 hardware checkpoint: both native probe and
   x64 app CreateDevice/UMD receipts, full regressions, and FactoryV2 exact
   rollback to Code28/PackageAbsent/8CPU. Do not advertise additional caps.

## WHAT IS STILL UNKNOWN:

- Whether the current pinned builder x64 Mesa/runtime closure is at the same
  source revision as the qualified ARM64 package or must be rebuilt. Audit its
  result.json, generated headers and archive hashes before linking.
- Whether the existing UMD source compiles unchanged for x64 and whether the
  x64 backing DLL passes export/stack/import checks.
- Whether the Windows loader accepts an Arm64X pure forwarder for the exact
  `UserModeDriverName` path, and whether `OpenAdapter12` is reached from the
  x64 app. A loose-DLL CPU-only load is the smallest prepackage discriminator.
- Whether the x64 game-like workload exposes a *second* D3D12 contract gap
  after device creation. Keep SM6 fail-closed; Night Raid FL11_0 and a real
  game remain separate acceptance steps.

The first implementation checkpoint is offline: one fresh x64 UMD with source
and dependency receipts, no J313 mutation. If it fails, fix only the x64
build closure. The first hardware checkpoint is a loose Arm64X loader test on
normal Code28; only after that should a new signed package be installed.
