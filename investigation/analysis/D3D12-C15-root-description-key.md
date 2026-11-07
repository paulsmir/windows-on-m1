# C15: an owned exact-field key for the 0001 root description

## WINDOWS CONTRACT:

The pinned WDK 26100 `d3d12umddi.h` defines
`D3D12DDIARG_CREATE_ROOT_SIGNATURE_0001`, its description, parameter active
unions, descriptor ranges, and static samplers. The source-only inventory is
`.local/c15-root-compatibility-source-plan/field-inventory.json`, SHA-256
`9a4c406ec3b7072bc357292f0784bc594536fefd3bcfab63b89230843121f18a`;
the six inspected source files and official references are recorded in
`source-manifest.json`, SHA-256
`13c51707bb8beb37c2c21a17273de839fd905497d9b690b6f58494922d8e6edc`.
The accompanying source plan has SHA-256
`978285ad1efed5794395a5f7e8a0f07a3154a76447e9c58a7116cde9df1b20ff`.

Root adopts only the data encoding portion of that proposal. In particular,
its proposed APPEND cursor arithmetic, register endpoints, 64-DWORD admission
checks, namespace/alias validation and feature/flag acceptance are **deferred**.
Encoder success proves structural input consistency and completed encoding;
it does not admit a Windows root signature or prove shader compatibility.

Inputs are actual typed records with explicit readable, immutable spans,
guaranteed by a caller for the complete operation. Pointer/span consistency
checks cannot establish Windows pointer readability. Select the active union
using raw ParameterType bits copied by reference with memcpy. Reject unknown
parameter kinds before reading a union. Copy enum and FLOAT representations
with memcpy rather than evaluating unsupported enum values or converting
floating-point values.

## AGX/ASAHI CONTRACT:

This is an unlinked UMD CPU module. No Gallium/Asahi call, GPU allocation,
binding, shader compilation or firmware interface is added. Asahi resource,
descriptor and shader contracts remain unchanged, as do KMD, m1n1 and Mu.
The existing opaque-root provider independently enforces live device/root
membership and owns its copied bytes; its caller serialization, lease and
borrowed-input lifetime requirements remain in force.

## TRANSLATION:

Use an original versioned byte format: `AGXRKEY1`, then little-endian u32
schema version, ABI tag, NodeMask, flags, parameter count and sampler count.
Encode parameters in order: raw type, visibility and only the active union.
Tables include their count and all five fields of each ordered range;
constants include register, space and count; root descriptors include register
and space. Each ordered static sampler contributes all thirteen scalar fields,
including the exact three FLOAT representations. Exclude pointer values,
padding, inactive union storage, handles, device identity and execution GPUVAs.

Preserve raw APPEND spelling, raw scalar flags, ordering, signed zero and NaN
payloads. Different keys can describe semantically equivalent signatures.
Equal keys prove equality of this schema's selected fields, not broader
semantic compatibility or Microsoft serialized blob identity.

Validate counts against supplied element spans before multiplication or array
traversal. Check every size addition/multiplication in size_t. Complete input
and capacity preflight before output writes. A refused operation publishes no
valid key; insufficient capacity must not produce a partial published key.
Specify and test destination/input overlap handling. Do not add arbitrary
parameter/range/sampler or encoded-byte limits.

## WHAT IS STILL UNKNOWN:

Production acquisition of readable records, Windows concurrency and teardown,
root admission policy, HRESULT/VOID-DDI policy, out-of-memory behavior, PSO
integration and broader semantic compatibility remain open. This work neither
repairs nor bypasses those boundaries. No public SM6 capability follows.

## Checkpoint and recovery

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Address/padding-dependent
root identity, omitted active fields, enum/float representation loss, unchecked
span/size arithmetic and publication of incomplete keys. Use owned valid
buffers, independent hand-computed byte oracles, field mutations, short spans,
short capacity, and provider copy/lease continuity. No pointer-fault tests or
allocation fault injection. Preserve the existing provider/core regressions.

First review the source-only proposal. Record exact sources, compiler argv and
input hashes before one bounded host run; native compile is a separate gate.
Stop on an unexpected result and preserve its receipt before any repair.
No hardware experiment is justified for this deterministic CPU operation.
The active EXP1438 guest remains exclusively controlled by its existing
pipeline, and rollback remains its unchanged GPU-visible recovery controller.

## Initial CPU evidence (EXP1441)

The first compilation stopped on four fixture enum-name spellings; its raw
outputs remain in `.local/c15-root-key-prototype-prep/run`. The separate
attempt2 changed only those spellings. All four fixed commands then returned
zero with empty stderr; 979 checks and four literal byte oracles passed, and
all 84 canonical input pins matched afterward. The source/header were
unchanged. Result manifest SHA-256:
`f6add038401166b89977f64eca2354001442c2d8ae278b436da9cc0d7be89403`.
This is host CPU evidence using actual extracted WDK records, not a native
Windows ABI, runtime DDI or GPU result. Durable regression/native compilation
are separate gates.

## Production integration remains a separate lifetime contract

The bounded lifetime source review is retained at
`.local/c15-runtime-lifetime-source-plan/plan.md`, SHA-256
`ce4b392a3be2657d040a109ff0197a9cdb69d40b6c7f81e9e9601551f01aae36`,
with its input hashes in `input-hashes.json`, SHA-256
`608780b0eae951555f6b5c8b90aeacde06e4a1142cc140f4ab31c692a2f7e6b7`.
The official [CPU Efficiency threading contract](https://microsoft.github.io/DirectX-Specs/d3d/CPUEfficiency.html)
does not provide blanket serialization of root and shader device callbacks.
The current caller-serialized provider therefore needs an explicit concurrent
adapter or a revised provider contract before DDI registration.

That adapter must own device-qualified immutable root records, retire membership
without invalidating pinned CPU bytes, and acquire independent callback-scoped
leases for Calc and Create. Calc/Create adjacency or a TLS/argument-address
handoff is not assumed. Registry operations must be narrow; core work and runtime
error reporting must occur outside registry locks. Device teardown must close
new acquisitions and preserve the validity of any outstanding CPU work without
using a runtime context after its lifetime. The precise borrow, resolver/sink
reentry, stable capacity and teardown guarantees remain to be implemented and
tested. A raw copy of the present private root object cannot replace this
field-wise key because it contains padding and private storage details.

## Durable and native evidence (EXP1441)

The current-tree opt-in test passed in 1.953 s with the same 979 checks, four
zero-exit commands, empty stderr and 68 unchanged input pins. Default discovery
skips before importing the external-input helper. The full suite ran 1609 tests
in 217.440 s: exactly the previous 119 failures/errors (68 failures, 51 errors),
no added or removed failures, 13 skips, and all six new source pins unchanged.
Full log SHA-256:
`09ba9ae63379df80b4de021ba4d9c9ad6d373be96fd710a8d1e0763c96b037be`.
This is a baseline comparison, not whole-suite GREEN.

The builder compiled the key and original internal API consumer as two ARM64
objects using MSVC 14.44, WDK/SDK 26100, W4/WX and NOMINMAX. Both commands
returned zero with empty stderr, actual dependency closure was within the
recorded set, both COFF machines were AA64 and all 4900 post-pins matched.
No link or object execution occurred. Controller artifact manifest SHA-256:
`fca9df7e0fc3c5f17969b15cbd026d42c702645ed17b987fc951697474886821`.
The explicit ARM64 and x86 production source lists were not changed; this
module remains unlinked. No runtime registration, SM6 or hardware claim follows.
