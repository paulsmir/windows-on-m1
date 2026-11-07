# D3D12 private device initialization: EXP1381

## Evidence and current boundary

EXP1381 source snapshot `56fe3977d3909012636dc87aaff9f0d177526574`
crashed after `C13 fresh device/queue/fence hr=0x00000000`, before C13
resource creation completed and before either new C18 component-mapping step.
The last UMD records show device creation and runtime initialization, with no
new native-owner creation receipt for that device. C18 hardware behavior is
therefore unmeasured in this run.

The preserved dump is
`.local/experiments/EXP1381-d3d12-srv-component-map/evidence/EXP1381/agx_d3d12_probe_e1381_arm64.exe.11168.dmp`
(host artifact root `/Users/pavel/public_windows`), SHA-256
`2728c3c3b634159873bca65eb9d8aa72585085f8d4416c536ffc62f64b8665e1`.
Process 11168, exception thread 9628, exception `0xc0000005`, read address
`0x31a94f`, PC `0x7ff9ecd22530`, UMD base `0x7ff9ecab0000`, RVA `0x272530`.

Dump CodeView identity, preserved UMD PE identity and preserved PDB agree:
GUID `{89DB4417-73A2-4C52-A0D5-7A0772EF46F7}`, age 1, PE timestamp
`0x6ad4c230`. PDB publics identify RVA `0x272508` as
`AgxD3d12OwnerCreateBuffer`; the failing instruction at +0x28 is:

```text
add x10, x0, #0x31a, lsl #12
ldr x8, [x10, #0x950]
```

At fault `x0 = 0xffffffffffffffff` is the incoming owner, `x1 = 0x10000`
is buffer size, and `x2 = 0xdc831c070` is the output buffer record. The
instruction reads `owner->Screen` at owner offset `0x31a950`; adding that
offset to -1 gives the exact fault address. This is a CPU pointer fault
before Gallium buffer creation, not evidence of a GPU execution fault.

Frame-pointer records give native buffer creation -> `CreateHeap` return
RVA `0x25c8d8` -> caller return RVA `0x25d264` -> D3D12Core. Disassembly of
`EnsureOwner` (RVA `0x25e478`) loads `device->owner` at +0x28 and immediately
returns it if nonzero. The device address recovered from the caller's saved
x20 is `0x14497703e90`; its bytes are absent from this minidump. The dump
proves the invalid owner value and its consumption. It does not identify
the allocator or writer that produced those bytes.

## Primary-source comparison

Inspected pinned WDK `.local/reference/wdk26100/d3d12umddi.h`:
`D3D12DDIARG_CALCPRIVATEDEVICESIZE` / `PFND3D12DDI_CALCPRIVATEDEVICESIZE`
at lines 2595–2604 and `D3D12DDIARG_CREATEDEVICE_0003` at lines 2655–2672.
The latter labels `hDrvDevice` as driver-private handle/storage; runtime
handle and callback inputs are separate fields. These header declarations
do not promise that storage arrives zeroed. Our lazy-state contract must
be established by our own constructor.

One bounded comparison of accepted source `2bd3b5d0` against `56fe3977`
shows no change in `AgxD3d12Device`, `CreateDevice`, `EnsureOwner`, or
`CreateHeap`. Both constructors assign runtime/callback/kernel/generation
only; neither initializes owner, owner_failed, paging_queue, paging_fence,
heap_count, or heaps. C18 changes descriptor component mapping and SRV
binding. Thus missing private-device initialization is an inherited
source defect exposed in EXP1381, not a demonstrated C18 swizzle failure.

## WINDOWS CONTRACT:

The runtime allocates driver-private storage of the size our size DDI
returns, and passes it back to our create DDI. Our constructor establishes
all state subsequently consumed by our DDIs. Initial owner and paging
handles are absent, owner failure is false, and the heap registry is empty.
This is a FULL GRAPHICS UMD object-lifetime contract; no KMD capabilities
or display-only assumptions apply.

## AGX/ASAHI CONTRACT:

There is no new hardware protocol. The existing native owner constructor
establishes its screen/context and owns their destruction. A buffer may
only be created through a successfully constructed owner. Neither AGX,
m1n1, Mu nor KMD owns initialization of this runtime-allocated UMD object.

## TRANSLATION:

Zero the complete `AgxD3d12Device` immediately after valid argument/storage
checks, then assign runtime callbacks and a new generation as before.
`EnsureOwner` retains its existing lazy creation and cached-failure policy.
No pointer-sentinel workaround or KMD change is needed.

## WHAT IS STILL UNKNOWN:

The allocator/writer responsible for the observed -1 bytes is not captured.
The deterministic missing-initialization defect and fix do not require
that provenance. Hardware verification must still rerun the interrupted
C13-and-later baseline and C18 steps in a newly pinned experiment after
EXP1381 recovery; the offline test does not validate C18 GPU pixels.

## Regression and verification

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A reused nonzero runtime
private block must not bypass owner construction, retain an old failure,
or expose stale paging handles and heap pointers.

`tests/test_d3d12_device_initialization.py` extracts the actual device
structure, CreateDevice, OwnerError and EnsureOwner into the new native
fixture. It runs under AddressSanitizer and UndefinedBehaviorSanitizer.
For 0xff and 0xa5 poison it checks all lazy fields and all 4096 heap
pointers, verifies runtime callback/generation preservation, then executes
actual EnsureOwner against a counting native-owner stub. Success and
failure are cached exactly once; both invalid argument forms leave storage
untouched. The same storage is repeatedly reconstructed.

Before the fix: 3 tests, 2 failures (`stale paging state`), invalid arguments
PASS. After the two-line constructor fix: all 3 PASS. Together with existing
`test_d3d12_abi_gate` and `test_d3d12_native_owner`: 33 tests PASS in 0.393s.
Builder/package/hardware gates belong to the parent experiment workflow;
none were run by this diagnostic subtask.
