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
