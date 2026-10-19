# C15 prerequisite: device-scoped fail-closed error routing

## Evidence and sources

The current `agx_d3d12_ddi.cpp` copied each runtime callback table and HRT
device into `AgxD3d12Device` in `CreateDevice`, but its generic rejected-DDI
thunk reported `E_NOTIMPL` through globals overwritten by the next device.
The current-source two-device CPU regression observed A/B/A rejected calls
as A=0, B=3 after creating A then B. This is a deterministic device-identity
bug, independently of DXIL conversion and GPU behavior.

The pinned WDK 26100 `d3d12umddi.h` (SHA-256
`d338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28`)
defines `PFND3D12DDI_SETERROR_CB(HRTDEVICE, HRESULT)`, the per-device
`D3D12DDI_CORELAYER_DEVICECALLBACKS_0003`, and
`PFND3D12DDI_GETIMPLICITPHYSICALADAPTERMASK(HDEVICE)` in
`D3D12DDI_DEVICE_FUNCS_CORE_0003`. Microsoft's corresponding device-callback
table documentation identifies the callback table as runtime-supplied.
`GetImplicitPhysicalAdapterMask` remains an existing rejected operation; this
change does not implement or advertise physical-adapter-mask support.

## WINDOWS CONTRACT:

The runtime supplies a device-specific callback table and HRT device when
creating each driver device. The typed DEVICE_CORE DDI starts with the driver
`HDEVICE`. A rejected call must use the callback/HRT association of that
invoked device. The existing `E_NOTIMPL` diagnostic, DDI return value, and
table/member-offset receipt are preserved here; public error-policy legality
is a separate gate.

## AGX/ASAHI CONTRACT:

No Asahi queue, AGX resource, UAT mapping, RTKit message, GPU power or
interrupt participates in this CPU DDI routing. The last hardware-validated
EXP1530 package was removed during normal visible recovery; current J313
state is one inert APPL0002, Code 28, Package Absent, no AppleAgx service,
eight CPUs. m1n1 and Mu initialize no state for this callback selection.

## TRANSLATION:

For rejected DEVICE_CORE calls, read the first typed `HDEVICE`, resolve its
existing `AgxD3d12Device`, and report through its stored callback table and
HRT device after the unchanged diagnostic receipt. Leave the command-list and
command-queue fallback untouched because those handle types have different
ownership. Existing ordinary create-error call sites using the globals are
finite follow-up work; do not remove the globals in this change.

## Shader creation follow-up (EXP1549)

The pinned WDK defines both `PFND3D12DDI_CREATE_SHADER_0003` and
`PFND3D12DDI_CREATE_COMPUTE_SHADER_0003` with `HDEVICE` first. Current
`StoreShader` had dropped it and used the last-created globals for invalid
code or private shader storage. A current-source ASAN/UBSAN RED after creating
A then B observed invalid compute shader A sent to B. The same per-device
helper now receives the existing `HDEVICE` from both typed entrypoints, with
the original `E_INVALIDARG` and success copy unchanged. Two-device CPU cases
cover compute and graphics under reversed creation; the builder compiled
both DDI profiles with the pinned WDK. State and element-layout invalid-arg
routes remain separate follow-up work; no GPU or public SM6 claim follows.

## State and element-layout follow-up (EXP1550)

WDK 26100's state and element-layout create signatures likewise begin with
`HDEVICE`. Their existing invalid-argument paths used last-created globals.
The actual-source two-device RED observed invalid state on B delivered to A
after B then A were created. Both current constructors now call the same
device-scoped helper; independent fixture observations cover state B and
element-layout A with opposite last-created devices. Existing `E_INVALIDARG`,
valid-copy behavior and diagnostic records are unchanged. After this change,
the only global `SetErrorCb` caller is the generic rejected non-DEVICE_CORE
fallback; its list/queue handle ownership needs separate primary-source
mapping. No GPU or public SM6 behavior was tested here.

## Command-list rejection follow-up (EXP1551)

The actual list object already stores `device` and `runtime`; its existing
`ListError` reports through that device's `pfnSetCommandListErrorCb` using the
runtime list handle. The generic rejection instead used the last-created
device's `SetErrorCb`. A current-source CPU RED for rejected `ExecuteBundle`
on lists A/B/A observed three device-B callbacks and zero list callbacks.

Pinned WDK 26100 defines `PFND3D12DDI_EXECUTE_BUNDLE` with the destination
`HCOMMANDLIST` first (line 1767), and `PFND3D12DDI_SETCOMMANDLISTERROR_CB` with
`HRTCOMMANDLIST, HRESULT` (line 2585). The
[actual 0003 callback table](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/ns-d3d12umddi-d3d12ddi_corelayer_devicecallbacks_0003)
documents this as a runtime callback. The direct callback documentation was
inaccessible in this pass, so the pinned header is the exact ABI authority.
No new permissible-HRESULT policy is inferred from the callback's name.

Rejected COMMAND_LIST thunks now preserve their first typed handle and call
the existing `ListError` after the unchanged diagnostic. A compile-time type
check requires `HCOMMANDLIST` first for all four actual rejected list members.
The CPU test extracts the current list definition, thunk, existing error
helper and exact table assignment. It supplies live original list/device
records and callback observers; unused later list value types have host
stand-ins, so this is not native Windows list-layout evidence. Current-source
ASan/UBSan observations are A2/B1 list callbacks with exact HRTLIST values,
no device callback, and null-B callback isolation. The actual WDK compiler
checks all four real member signatures in both normal DDI build modes.

The `ExecuteBundle` implementation remains rejected, and the error value is
the existing `E_NOTIMPL`. No command is recorded or sent to Asahi, m1n1, Mu,
KMD or GPU. Queue rejection still needs a separate owner mapping: the current
queue private record stores its runtime queue and native context but no
parent device. This change does not manufacture that association or establish
post-destroy/reentrant callback lifetime. The next hardware checkpoint is
required only once a complete versioned runtime contract is ready; this
deterministic route itself needs no GPU-state experiment.

## WHAT IS STILL UNKNOWN:

This CPU check does not establish public acceptability of `E_NOTIMPL` for
each rejected DDI, concurrent destruction/reentrancy, command-list or queue
error routing, DXIL provider registration, public SM6 capabilities, or AGX
execution. No hardware experiment can resolve the deterministic A/B error
association more directly than the current-source CPU regression.

## Checkpoint and recovery

The test extracts the current `Rejected`, `AgxD3d12Device`, `CreateDevice`,
and actual table assignment into a small CPU fixture. Its table offset is
derived from and pinned to the WDK struct. It creates A/B and B/A, invokes
A/B/A, checks A=2/B=1, callback HRT values, three exact diagnostic receipts,
zero return, and a null-callback isolation case. The compiler's normal WDK
build is a separate gate for all actual DEVICE_CORE signatures. CPU tests
mutate no guest or driver package, so they require no J313 recovery.
