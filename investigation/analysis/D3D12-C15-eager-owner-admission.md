# D3D12 native owner publication at CreateDevice

Status: candidate owner-layer correction. No GPU package or hardware result
belongs to this note until a separate preregistered experiment passes.

## Primary sources and current observation

- Current `agx_d3d12_ddi.cpp`: `CreateDevice` initializes runtime handles,
  kernel callbacks, generation and shader-provider identity, then returns.
  `EnsureOwner` currently runs on the first native resource operation, reads
  `owner`/`owner_failed` without synchronization, constructs a native owner
  through `AgxD3d12OwnerCreate`, then publishes the pointer. `DestroyDevice`
  destroys only the published pointer.
- `agx_d3d12_windows.cpp`: `AgxD3d12OwnerCreate` acquires a KMD virtual
  context through the runtime callback and initializes the Windows runtime,
  Asahi screen and context. On failure it destroys the partial owner.
- Current Asahi Mesa `agx_pipe.c` assigns `screen->context_create` to
  `agx_create_context`; the Windows winsys owns the corresponding native
  screen/context. KMD owns context execution and GPU memory; m1n1 and Mu do
  not serialize UMD resource creation and are unchanged by this proposal.
- EXP1681 actual J313 UMD log: `d3d12-create-device` succeeds at QPC
  36762428916; `measure-d3d12-owner` succeeds later at 36763399797 after
  caps queries. That source/package and FactoryV2 recovery are the hardware
  comparison baseline.
- EXP1745 extracted actual-source host schedule creates two distinct owners;
  second publication replaces the first. EXP1752 keeps this as a bounded
  known-failure regression.
- Pinned [Microsoft D3D12 threading specification](https://github.com/microsoft/DirectX-Specs/blob/71e431fa0f713818c6a3a66f49ab0e6732712d15/d3d/CPUEfficiency.md)
  allows free-threaded resource creation. Microsoft's
  [CreateDevice_0003 reference](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_createdevice_0003)
  permits `S_OK`, `DXGI_STATUS_NO_REDIRECTION` and `E_OUTOFMEMORY`.
  [Context-creation guidance](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_createcontextcb)
  places GPU context creation in `CreateDevice`; the current driver uses
  the virtual-addressing counterpart `pfnCreateContextVirtualCb`.

## WINDOWS CONTRACT:

`CreateDevice` has runtime/kernel handles and callbacks before it returns.
The runtime cannot issue application resource operations on that newly
returned device before this synchronous callback completes. Subsequent
resource creation is free-threaded; it cannot be used as a one-time owner
publication lock. Keep current `CreateDevice` return codes: do not map every
native owner failure to `E_OUTOFMEMORY` or introduce `E_FAIL` without a
documented DDI return contract.

## AGX/ASAHI CONTRACT:

The UMD native owner comprises a KMD context and Asahi screen/context. Its
constructor and destructor make runtime/kernel callbacks, so a blocking
publication lock across the constructor would require an unproven callback
reentry guarantee. The owner must be created at most once per successfully
admitted UMD device and destroyed once. No hardware register, power, DART,
IRQ, m1n1 or Mu contract changes.

## TRANSLATION:

After current `CreateDevice` has registered its shader provider and device
identity, call the existing `EnsureOwner` once **before** returning the
device to the runtime. The resulting cached owner or `owner_failed` state is
immutable for the later free-threaded resource calls. Leave the existing
resource-call failure behavior in place if eager construction fails; this
keeps the change focused on timing/publication and avoids an unsupported
CreateDevice HRESULT mapping. Do not add a mutex around callbacks. Keep
non-native build variants behaviorally unchanged.

## WHAT IS STILL UNKNOWN:

The actual D3D12Core/KMD callback ordering may not admit full Asahi screen
construction at this earlier point, despite context creation being a
documented CreateDevice operation. A callback could also reenter a same-device
UMD path while CreateDevice is still constructing; no primary source proves
otherwise. These are reasons for a source-bound forced-reentry control and a
single minimal hardware discriminator after offline gates, not reasons to
assume early construction succeeds.

## Checkpoint and recovery

First make the actual-source host fixture fail on the old lazy state, then
require one owner creation during `CreateDevice`, cached success/failure and
no second creation under two concurrent `EnsureOwner` callers. Preserve the
known invalid-argument and poisoned-storage controls. Compile both pinned
WDK DDI variants and winsys. A later hardware experiment should change only
this owner timing, use one exact signed/hash-pinned package and a fresh-device
probe with concurrent resource creation, compare D3D12/regressions to
EXP1681, then use the ordinary GPU-visible FactoryV2 rollback to
`RECOVERY_DONE`. Failure to reach owner creation, baseline regression or
guest hang rejects the timing hypothesis; recover before a new ID.

## EXP1753 offline candidate

The current candidate calls `EnsureOwner` after provider registration and
device-identity publication but before successful `CreateDevice` returns.
The function uses Interlocked reads/publication for cached owner and failure
state plus an in-flight flag. A synchronous constructor callback that reenters
`EnsureOwner` returns no partial owner and cannot start a second constructor
or wait on itself. After the outer constructor finishes, later free-threaded
calls read the fully published owner or cached failure. `CreateDevice` keeps
its existing HRESULT behavior on an owner failure; the next resource call
still fails closed. This does not assert that a real runtime callback will
legally reenter or that early screen construction succeeds on J313.

The actual-source host fixture first failed because old `CreateDevice` did
not publish an owner. After the timing change, a forced callback-reentry
control failed because it started a second constructor; the Interlocked
in-flight guard closed that defect. Seven current-source ASan/UBSan controls
now pass, including poisoned storage, cached failure, two concurrent cached
readers and forced synchronous reentry. The pinned WDK builder compiled
NativeOwner DDI, None DDI and winsys with rc=0 for exact source SHA-256
`11f1fc2fd786175539deba234a52283371a100e297a17e08baa6bf23faa104dd`.
Raw host and builder output is pinned by
`.local/c15-eager-owner1753/result1753.json` SHA-256
`7bf355bfe8086a34442bc1e453dae80c2e63e136f8515d7dca3d2febbc733d65`.
An unrelated broad host fixture for rejected-device errors still lacks newer
provider/R1 stand-ins and fails at compilation; it is not a runtime result.
No hardware claim follows from these offline gates.
