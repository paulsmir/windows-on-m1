# rc3: descriptor-heap-end reader audit and opt-in guard

Offline task on `codex/rc3`, 2026-10-25. This implements the TASK's fallback
behavior experiment, not a proven root-cause repair. No hardware, guest,
builder, package operation, or other worktree was accessed. Earlier rc2 evidence
is in `GravityMark-rc2-fault-and-progress.md`, section 1.

## Finding and source evidence

The repeated exclusive-end relationship is strong evidence for a heap-related
address fault. However, **the proposed offset-zero GPU reader is not present in
the audited descriptor paths**. It would be incorrect to claim its instruction
or index is proven. The narrower statement that ordinary index N reads the
texture part at N*160+64 also applies to buffer SRVs and buffer UAV reads.

Paths below are relative to `drivers/apple-agx/` unless prefixed otherwise:

| Consumer | Source and result |
| --- | --- |
| Entry layout | `d3d12/agx_d3d12_ddi.cpp:2746` defines the first 64 bytes as CPU metadata. `:2805` defines texture/PBE/counter at offsets 64/88/112. `mesa/dxbc/dxbc_bindings.h:51` pins stride 160 and all offsets. |
| DXBC heap texture/buffer SRV | `mesa/dxbc/dxbc_to_nir.c:2002` forms `index*160+64+delta`; `:2117` supplies range base plus shader index. No clamp or heap-count uniform is used. |
| DXBC UAV, including raw/structured | `mesa/dxbc/dxbc_to_nir.c:2017` and `:2042` select counter or texture/PBE. `mesa/winsys/agx_d3d12_windows.cpp:2882` represents raw/structured heap UAVs as R32_UINT buffer images. Reads use +64, writes +88, counters +112, not entry zero. |
| DXIL heap images and textures | `mesa/dxil/agx_dxil_to_nir.c:412` and `:469` form the same addresses from range base plus array element. No heap-end clamp. |
| CBV and slot buffer paths | `d3d12/agx_d3d12_ddi.cpp:6063` reads descriptor metadata on the CPU, then binds the referenced resource, offset and length. `:5487` rejects heap-range CBVs. These shaders read the referenced resource, not the first 64 bytes of the descriptor heap. |
| Samplers | `mesa/dxbc/dxbc_to_nir.c:2070` reads a dword from CB14's sampler map, not the D3D12 sampler heap backing. `mesa/winsys/agx_d3d12_windows.cpp:3074` creates a batch sampler union and map. `mesa/dxil/agx_dxil_to_nir.c:476` follows the same map convention. |
| Base address/uniform | `mesa/scripts/build-native-asahi-state.py:6018` pairs marked handles with `ubo_base[15]`. `mesa/winsys/agx_d3d12_windows.cpp:3160` binds the heap resource as CB15 (buffer_size capped at 64 KiB, but bindless accesses use its base). `.local/reference/mesa/src/asahi/compiler/agx_compile.c:3178` lowers the descriptor address to base plus byte offset; no subtraction of 64. |
| Indirect dispatch | `mesa/winsys/agx_d3d12_windows.cpp:3433` rebinds heaps before each indirect record. It rebuilds batch handles/references, not an end pointer or a count+1 index. |
| Descriptor copies | `d3d12/agx_d3d12_ddi.cpp:3249` and `:3264` use CPU memmove and MirrorHeapForms. There is no GPU CopyDescriptors reader in these functions. |
| Root table bounds | `d3d12/agx_d3d12_ddi.cpp:1075` preserves NumDescriptors, APPEND uses offset+count, UINT_MAX stays unbounded. `:5503` rejects delta >= count for bounded ranges. `:5511` forms heap_first; dynamic indices themselves remain unchecked. `:6226` walks CPU entries with i<count. |
| Inclusive shader declaration bound | `mesa/dxbc/dxbc_parse.c:865` uses last-register - first-register + 1. Those declaration endpoints are inclusive; this is not D3D12 NumDescriptors+1. Unbounded declarations route to heap ranges at `:871`. |

For the two saved heaps, N=0x20000, so N*160 is 64-byte aligned. With a
64-byte fault-address granule, normal reads of descriptor N at +64, +88 or
+112 all report end+64, not end. The last valid descriptor's three 24-byte
records also end before the logical heap end. This refutes the simple
"ordinary emitted descriptor N reads offset zero" explanation in these paths;
it does **not** rule out corrupted address/base, a different reader, a hardware
fetch beyond the requested descriptor, or a stale adjacent-object reference.
No exact failing shader, dynamic index, or instruction was recovered offline.

GravityMark's saved `gm1941b-umd.log` has root-signature receipts, e.g. lines
184 and 500, but their schema (`agx_d3d12_ddi.cpp:1112`) contains only parameter
count, static sampler count, flags, and DWORD count. They do not record range
sizes or table values. Therefore whether the failing GravityMark signature
uses an unbounded range remains unknown; general support for it is not proof
that this application used it.

## Windows and Asahi contracts; ownership

Pinned WDK `.local/reference/wdk26100/d3d12umddi.h:817` defines shader-visible
as flag 0x2 and `:826` defines NumDescriptors. `:698` defines Binding Tier 3,
but this driver advertises **Tier 2**, `agx_d3d12_ddi.cpp:433`. Local analysis
`D3D12-C47-binding-tier2.md:13` records tier resource limits and descriptor
population requirements. Resource-element OOB behavior and permission to
leave unused table entries unpopulated must not be mistaken for permission
to index outside the descriptor heap. The pinned ABI header does not establish
a Tier-3 OOB-index guarantee. No fresh Microsoft prose specification was
retrieved because this task is offline; a Tier-3 guarantee is not claimed.
The guard is diagnostic robustness, not a capability change or a declaration
that arbitrary OOB descriptor indexing is supported.

Asahi owns descriptor encoding. Existing `AgxWin32AsahiNullTextureDescriptor`
and `AgxWin32AsahiNullPbeDescriptor` call `agx_set_null_texture/pbe`:
`.local/reference/mesa/src/asahi/lib/agx_helpers.h:221` uses the zero page and
`:237` uses the scratch sink. The patch calls these existing helpers; it copies
no external implementation. All sampler guard bytes are zero, including
kind/Valid, which is the existing CPU null-sampler representation.

The UMD owns allocation, shadow initialization and complete submitted resource
references. VidMm/KMD still own mapping and residency. Asahi
`.local/reference/mesa/src/gallium/drivers/asahi/agx_uniforms.c:14` adds the
whole CB buffer resource to the batch's read set. The guard is part of that
same BO, so it shares residency and retirement; it is not an independently
evictable neighbour. This is an ownership contract, not proof of a hardware
run. No new initialization, interrupt, DMA protocol, power or recovery owner
is introduced. m1n1/Mu and launch/recovery profiles are unchanged; live-state
collection is explicitly prohibited by this task.

## Implemented discriminator

`APPLE_AGX_D3D12_DESCRIPTOR_HEAP_GUARD=1`, exactly, enables an extra 64 KiB of
backing for every shader-visible CBV/SRV/UAV and sampler heap. Other values,
non-shader-visible heaps and RTV/DSV heaps keep the existing behavior. The
allocation is `align_up(logical_bytes + 65536, 65536)`; this guarantees at
least 64 KiB after the logical end even for non-page-aligned descriptor counts.
The tail starts on a descriptor boundary, not at the old allocation end.

The cached shadow and GPU mirror get zero metadata and complete null
texture/PBE/counter forms in every complete guard entry. The final partial
entry remains zero; the guarantee is for complete entries in the guard, not
arbitrary reads beyond the backing. Logical handle lookups, mirror writes,
root-table bounds and CPU heap walks exclude the tail. Physical bytes still
flow to HeapBuffer, allocation, residency and destruction. The default arm
retains its existing rounded-size semantics.

One `measure-d3d12-descriptor-guard` receipt per successful guarded heap has
8 hexadecimal UINTs: type, NumDescriptors, heap VA low/high, guard-begin VA
low/high, guard-end-exclusive VA low/high. Compare new fault addresses to
these ranges, not the old experiment's absolute VA. Sampler backing is padded
as requested, but this does **not** protect an OOB access to the separate CB14
sampler map/batch sampler heap. No shader clamp is introduced.

## Tests and limitations

The actual CreateDescriptorHeap function is extracted and compiled with
AddressSanitizer and UBSan. Before the implementation it failed the extra
backing assertion. It now checks CBV/SRV/UAV and sampler heaps, non-visible
and non-target heaps, four descriptor counts including aligned and unaligned
ends, exact opt-in parsing, unchanged descriptor counts, initialized GPU-tail
contents, map/shadow failure and full mirror-registry cleanup. Null-packing
calls are stand-ins with distinct byte patterns in this host fixture; it tests
that production initialization publishes every part, not AGX binary encoding.
The null encoders themselves are the existing Asahi implementation.

The old ABI string guard and root-resolution extraction fixture are updated
to consume the real logical-extent helper. This preserves their original
bounds and namespace tests. Full results and commit IDs are in the handoff
REPORT.md. Native Windows UMD compilation and hardware validation remain for
the integration owner; no KMD change or rebuild is required by these sources.

## Next checkpoint and interpretation

WHY THIS HYPOTHESIS:

1. EXP1939 and EXP1941 independently fault at the exclusive end of their
   shader-visible 20 MiB heap despite different process/VA values.
2. EXP1941 history shows an adjacent 64 KiB allocation invalidated before
   the fault. A heap overread could succeed while that neighbour is resident.
3. The normal emitted offsets disagree with the exact fault granule. A
   same-BO null guard changes the boundary behavior without inventing an
   unproven shader-index correction.

Integration owner: base **EXP1941 + this UMD**, no KMD/caps/firmware/signer
changes. Compile the affected native UMD libraries and DDI, hash the actual
DLLs and preserve the exact original UMD for the established hot-swap restore.
Use `Wom1GpuFaultInfoRead=1`, `Wom1PoolHalfSize=1`, and the existing validation
profile (`APPLE_AGX_UMD_VALIDATE_REFS=1`, REFUSE=0, REFUSALS_ONLY=1,
TRACE_SYNC=1). Set the guard environment in the new GravityMark process
before any heaps are created. Run the existing harness with
`-Tag <new-id> -Api d3d12 -Asteroids 2000 -Fullscreen 0 -Timeout 180 -ValidateRefs 1`.
Run guard **on first**, fresh process and recorded starting RAM/pool state;
then off (=0) only if the existing recovery policy and cost allow a matched
control. Do not precede this with the 10k crawl. Preregister hashes and use
the established normal GPU-visible recovery; this note does not execute or
replace that procedure.

| Observation | Decision |
| --- | --- |
| On completes real frames/benchmark, off reproduces heap-end fault | Strong support for boundary-sensitive H1; not proof of the reader, because larger backing also moves neighbour VAs and changes memory pressure. Keep opt-in until the reader is established. |
| On faults in the recorded guard | Guard should belong to the heap BO; inspect exact heap mapping/reference history and successful initialization. A missing leaf here points to backing/residency or deployment failure, not an unguarded adjacent object. |
| On fault moves to new physical allocation end | Supports a reader tied to allocated length or a larger overrun; do not repeatedly enlarge the tail. |
| On faults elsewhere outside all guards | Near-end descriptor overread is insufficient to explain the new failure. Attribute to its mapping/resource evidence. |
| No guard receipt, creation fails, only Ready is reached, or timeout/OOM | Deployment/allocation/progress outcome is inconclusive for H1. Extra backing itself costs memory. |
| Both complete | Non-reproduction; no H1 confirmation. |

Collect logs, guard ranges, actual frame progress, fault receipts and any dump
before restore/cleanup. No GPU fault/0x116 and a live desktop are required;
Ready alone is not success.

The secondary 10k issue is unchanged from rc2: 40.060429 s includes thousands
of completed replay copies, with individual ~9.66/~9.06 s allocation gaps;
one measured gap is overwhelmingly GPUVA binding. The last E_PENDING precedes
a blocking paging-fence wait, but its exact terminal stack is absent. There
is no newly proven 40-second memcpy defect to fix in this patch.
