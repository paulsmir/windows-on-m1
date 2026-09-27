# R140 — EXP855C runtime EscapeCb identity

## Verdict and scope

Confirmed UMD ABI defect, corrected offline. No GPU package was built, staged,
installed or launched. Air was only read over pinned SSH in ordinary Code28:
exact d3d11.dll and an existing Explorer crash dump were copied; no registry,
WER, package, boot or profile state was changed. The builder compiled and ran
only CPU contract tests. m1n1/Mu pre-existing dirty state is untouched.

## Exact attribution

The input task's count needs correction: saved `Application-1000-full.txt`
contains 51 d3d11.dll/+0x8f240 AVs and one `bad_module_info` unknown-module/+0
record, not 52 proven identical faults. The one DWM event shares the 51-event
signature, but its own stack has not been captured here.

Air System32 d3d11.dll is 10.0.26100.9457, reproducible PE timestamp/hash
0x780ae53e, SHA256
`8bc2ce9df3b9ad010622bdd71547b49aa8c8445b03a7d6c183ecb8367cdc282f`.
Builder System32 is 10.0.26100.9278 and was not substituted. CDB x64 loaded
the copied ARM64 PE and Microsoft public PDB
`d3d11.pdb/F8B885078E56880C2392CC7F2C1CFAC01`, SHA256
`028dbf28339a5a6e28389e18e6268bde89dde60dc5777ce23baafb8834394a17`.
The fault is `NDXGI::CDevice::SetPriorityCB+0x30`, instruction
`ldr w8,[x20,#4]`; x20 comes from the first pointer in its second argument,
nominally D3DDDICB_SETPRIORITY.hResource. This is a runtime input callback,
not a CreateDevice output table or caps query.

The ordinary guest retained `C:\Users\pavel\AppData\Local\CrashDumps\explorer.exe.8428.dmp`.
Its saved session is 2026-09-27T21:18:38Z, uptime 6m10.626s, process uptime7s,
PID8428/TID6188. SHA256
`2f74008d966f6e5d37aca15edb2390cbbba3b495b1e36bfe163b9c34ab2fb403`.
Exception0xC0000005 reads address0x657061637349. x20 is0x657061637345,
the little-endian bytes of the ASCII string `Escape`; x19 points at that
string inside d3d11. Saved stack:

```
NDXGI::CDevice::SetPriorityCB+0x30
CallAndLogImpl+0x2c
NDXGI::CDevice::EscapeCB+0x8c
AppleAgxRenderAdmissionUmd!private_escape+0x88
AgxWin32AsahiBatchFinish [prepare_process_buffers]
agx_batch_submit -> agx_flush_batch -> agx_flush_all
ResourceCopyRegion -> d3d11 CopySubresourceRegion -> dcomp surface creation
```

`CallAndLogImpl` has folded template aliases; CDB prints a different KMT
operation in one alias. Its call site is demonstrably EscapeCB, and its
second argument is the `Escape` diagnostic label. It is not a multiplane
operation or an actual resource-priority request from the UMD.

The saved private_escape request at0xe9d680 contains hDevice0, Flags1,
payload0xe9d6d0, size0xe0 and hContext0x19d11e0. Package855C disassembly
loads `device->RuntimeDevice.handle` as EscapeCb's first argument. Runtime
EscapeCB loads its KMT Escape function from first_argument+0x80. With the
wrong runtime object this dispatch reaches SetPriorityCB; its interpretation
of the label as a resource explains the precise ASCII-derived bad address.
The minidump lacks the context object's heap page, so a direct dump of the
owner pointer is unavailable; source, matching compiled wrapper and captured
call chain establish the identity violation without that heap reconstruction.

The loaded UMD's PE timestamp0x6ab984e6, ImageSize0xbc8000 and matched PDB
identify package855C; signed DLL SHA256
`16ad5b8d2e0fb2e0df3ecea4f9dde7150384a346db88502d0dc59617cee44272`,
PDB SHA256
`ae4ec561284b15331495dacaf18e651670e2624c5fcd5999c000987bad95dd7c`.
The PDB has also been retained locally. CDB's checksum warning reflects the
zero PE checksum; symbols matched without forced mismatched-symbol loading.

## Primary contract and comparison

Pinned WDK26100 `um/d3dumddi.h:3620-3626,4323-4324,4500-4510`, SHA256
`099fad9e7e4faff6f7699ecd331edf4ff783396b2714280e219593bee3fb422c`,
agrees with Microsoft documentation:

- [PFND3DDDI_ESCAPECB](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_escapecb)
  takes the runtime adapter handle, despite living in DEVICECALLBACKS.
- [D3DDDICB_ESCAPE](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/ns-d3dumddi-_d3dddicb_escape)
  carries the runtime device handle; a non-null context requires its owning
  device to be supplied.
- [D3DDDICB_SETPRIORITY](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/ns-d3dumddi-_d3dddicb_setpriority)
  explains the pointer interpretation at the eventual erroneous target.

One 4dcf6aa9 (854B) ..56cb61c2 (855C) UMD source comparison: R137 appends
PrivateEscape to the internal GPUVA ops, replaces nine ordinary Process BOs
with private acquire/release and emits the v3 scene lease. Its new Windows
wrapper incorrectly treated EscapeCb like a device-scoped callback. The
D3D DDI table, caps and CreateResource output did not change in that diff;
R138/R139 change KMD, not this wrapper. The existing legitimate SetPriority
wrapper initializes hResource NULL and supplies allocation handles, matching
its contract. No layout-padding guess or table-slot patch is needed.
Absence of private-storage log lines was not proof of no attempted escape:
the dump proves it ran and crashed inside runtime before reaching KMD.

Ownership and source-first applicability are recorded in
[the plan](../../docs/superpowers/plans/2026-09-27-r140-escape-callback.md).
This boundary has no new hardware protocol or firmware behavior. KMD retains
initialization, private storage, DMA, completion and lifetime; runtime retains
handle translation. The user-mode owner is fixed directly.

## Change and regression

`private_escape` now checks adapter magic/runtime handle and runtime device,
sets request.hDevice, and calls EscapeCb with RuntimeAdapter.handle. Existing
context, HardwareAccess, payload and fail-closed behavior remain. No caps,
AGX4 v3 layout, memory partition, KMD or firmware change.

The existing `GpuvaContractMode` Windows executable calls the real production
wrapper through AdmissionUmdGpuvaOperations. A strict runtime callback uses
distinct adapter/device/context identities, verifies complete escape arguments,
returns output through the original payload and tests failing HRESULT. Acquire,
release, NULL/malformed owners, missing callbacks and closing device are covered;
invalid input must not dispatch. NDEBUG is explicitly undefined in this mode.
Old source: build succeeds, test exits1 with `R140 escape adapter/device/context
contract violation`. Fixed source: same x64 target exits0; existing GPUVA
reserve/map/resident/submit/signal/evict/free flow remains PASS. ARM64 target
compiles and links with no reported warnings/errors; ARM64 was not executed.

Builder uses the existing `AD04-persistent-dwm-next` tree, synchronizing only
the two changed C files. Exact commands are recorded below and in test logs:

```
C:\VS2022Community\MSBuild\Current\Bin\amd64\MSBuild.exe   C:\Users\pauls\AD04-persistent-dwm-next\drivers\apple-agx\render-admission\umd\tests\UmdContractTest.vcxproj   /m /t:Build /p:Configuration=Release /p:Platform=x64 /p:GpuvaContractMode=true   /p:IntDir=C:\Users\pauls\R140-tests\x64-obj\ /p:OutDir=C:\Users\pauls\R140-tests\x64\ /verbosity:minimal
C:\Users\pauls\R140-tests\x64\UmdContractTest.exe
```

ARM64 uses Platform=ARM64, arm64-obj and arm64 output paths. This builds the
wrapper contract executable, not the entire production UMD or a GPU package.
x64 executable SHA256
`7ca85f457b9db23285619aad52d67596b557f9808721c0bab3dd7f7ec8f9a944`;
ARM64 executable SHA256
`afcbd613f7cc8c35c738a51042f767351a961ae5c004ff70703a748a68f946c4`.

Host targeted commands: `CC=clang python3 -m unittest discover -s tests -p
 'test_g3_*.py' -v` and corresponding `test_g4_*.py`:55/55 and30/30 PASS,
including G3 profiles16/64, private escape/producer/completion and G4 envelope.
Independent read-only review: no findings; source and saved RED/GREEN/ARM64
evidence inspected. Full suite `CC=clang python3 -m unittest discover -s tests -v`:
1140 tests in110.984s,15 failures/41 errors/2 skips. Exact56 failure/error
names equal R139 (new0,removed0); this is not a green full suite. Names and
comparison are retained in `failure-comparison.json`; the unchanged names are
also enumerated in [R138 baseline](R138-scanout-pool.md#unchanged-baseline-failureserrors-exact-names).

## Evidence and next checkpoint

Evidence is worktree `.local/experiments/R140-offline/`: exact DLL, saved dump,
UMD PDB, symbolization/stack/argument logs, WDK extracts, input hashes, Windows
RED/GREEN/ARM64 outputs and host results. Saved EXP855C evidence remains in
main repository `.local/experiments/EXP855C-r139-reserved-va/`.
Base root85b0edfb67a2eaad2f27a1f931e24b465e58aeaf.
No new hardware verdict: Code0, KMD private acquire success, TA+3D completion,
fence notification and DWM recovery are not inferred from this test.
Next separately authorized discriminator is private acquire crossing runtime
EscapeCB into KMD, then attribute its return/any later boundary from fresh
receipts and crash evidence. No additional diagnostic is needed to attribute
the present AV. Ordinary EXP377/392 recovered by EXP855C remains accepted.

## Tandem review disposition

REVIEW R113: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R111: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R110: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R109: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R108: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R107: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R106: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R105: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R104: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R103: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R102: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R100: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R99: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R98: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R97: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R96: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R95: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R94: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R91: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R90: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R88: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R86: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R85: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R74: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R71: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R69: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R65: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R64: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R63: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R57: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R55: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R54: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R49: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R48: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R47: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R45: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R40: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R37: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.
REVIEW R64: DEFER — historical item outside the EXP855C user-mode EscapeCb identity boundary; no hardware, firmware, caps or package change in R140.

Tandem REVIEW SHA256 `753bdfe638d6171954875a5fc9a58c09761999ffcab89b9d216f566c7e1300c5`.
Final driver source manifest SHA256 `a65ab4f242515570c1bb425475ff8f804993077969e81198648b9e0cb56bd543`.

Evidence manifest SHA256 `5941b217270b86ac848fdd18bf62f1f5a141225186aa36a287701b95189f446c`; `git diff --check` passes.
