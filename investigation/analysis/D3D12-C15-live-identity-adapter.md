# C15 live device and PSO identity adapter

This CPU prerequisite is implemented for the already written but unlinked
zero-native-cache library. It does not negotiate R1, publish a library table,
link the external DXIL converter or claim SM6 support. The signed EXP1582
package and its recovery are frozen and validated.

## WINDOWS CONTRACT:

The pinned WDK 26100 `d3d12umddi.h` defines typed `HDEVICE` and
`HPIPELINESTATE` private handles and the R1 `LIBRARY_REFERENCE_0010` record.
Microsoft's pinned DirectX D3D12 threading contract allows concurrent device
resource methods. The existing [zero-cache foundation](D3D12-C15-zero-native-cache-foundation.md)
needs two resolvers: a live device identity, and the creating device of a live
PSO. Its registry checks unknown addresses before dereferencing them and rejects
foreign owners. `CreatePipelineState` currently discards its `HDEVICE` argument;
the source has no published device/PSO identity for those resolvers.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? An arbitrary or retired private
handle must never be dereferenced during library reference validation; a live PSO
must resolve to the device which created it, even when another device was created
later. Destroying one PSO/device cannot change unrelated live identities. Valid
concurrent registry lookups must not race registration/removal. This is a future
library admission invariant, not a test of current public library availability.

## AGX/ASAHI CONTRACT:

Identity publication is CPU metadata only. No AGX owner creation, context,
allocation, GPUVA mapping, native PSO compile, interrupt, power or firmware call
belongs under its lock. The existing native PSO destructor runs after identity
retirement. The existing Windows runtime owns the private allocations and the
ordinary callback lifetime; a pointer returned by a resolver is borrowed only
for that callback. The test does not grant arbitrary stale-address ABA safety or
repair the separate native owner/context concurrency gate.

## TRANSLATION:

Use intrusive links in actual runtime-sized `AgxD3d12Device` and
`AgxD3d12PipelineState` storage and one process metadata SRW lock. Publish the
device after successful CreateDevice initialization; retire it before owner
destruction. Publish the PSO with its typed creator device only after successful
CreatePipelineState; retire it before native PSO destruction. Resolver hooks scan
registered addresses before reading any private fields, return a live device
identity or null, and invoke no callbacks while holding the lock. PSO resolution
also checks that its device remains registered. No dynamic registry allocation,
global last-created device shortcut or change to shader caps/table negotiation.

The main DDI's `CalcPrivateDeviceSize` and `CalcPrivatePipelineStateSize` already
derive from `sizeof`/`offsetof`; their new native layouts require an actual WDK
compile and signed-package regression. An extracted host fixture can test the
registry's safety, but its stand-ins do not prove native Windows ABI layout.

## WHAT IS STILL UNKNOWN:

The complete R1 typed adapters, library serialization/reference semantics,
device-child lifetime on all runtime failure paths, internal dxil-spirv OOM
handling and shared AGX context serialization remain separate gates. The
Microsoft callback reentry question in the existing threading note is also
unresolved; the proposed short registry lock encloses no outward callback.
Do not publish R1/SM6 or run a caps probe from this adapter alone.

## Checkpoint and recovery

EXP1583 actual-source host fixture reproduced the absent resolver contract, then
passed unregistered/foreign/live/retired device and PSO identities, protected
unknown addresses and concurrent valid lookups under ASan/UBSan and TSan.
Focused device/error-owner/ABI/native-owner tests passed35. EXP1585 native
compile exposed a `DeviceOf` declaration-order error; the one-line typed handle
correction passed EXP1586's three WDK variants. EXP1587 compiled and executed
six original PSO compute/graphics serial/parallel/failure modes under ASan/UBSan.
EXP1588 ran1640 host tests with no added FAIL/ERROR IDs relative to the
recorded124-ID baseline; the suite is not wholly green.

A later hardware package changes only identity metadata and must retain all
accepted D3D12, GL, corpus, DWM and pool checkpoints, followed by exact normal
GPU-visible package removal and `RECOVERY_DONE`. That run verifies native
private-size integration, not R1 library execution or full concurrency safety.
