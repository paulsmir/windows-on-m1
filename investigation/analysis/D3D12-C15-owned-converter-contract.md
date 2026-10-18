# C15 owned converter and deterministic binding contract

2026-10-18. READ-ONLY single bounded source pass. New note only. No converter,
DXC fixture, code/caps/DDI/native build script, guest, hardware, build or commit
change. This is a proposed original CPU adapter contract, not production readiness.

## Sources, pins and license

Reviewed external candidate is dxil-spirv
`8434b0639fb2a48ce13aac08a420ecce5d23df0e`, C API2.75.0, using the already reviewed
builtin llvm-bc dependency graph. Local source snapshot is
`/tmp/c15-runtime-license-8434b063/dxil-spirv`.

| Source at exact candidate pin | SHA256 / inspected boundary |
| --- | --- |
|dxil_spirv_c.h|`4fbca14fe1de04b463cd8d8f6f449fc4e57490c5c900eaff9e6d6d65ea55303c`; callback structs, scan_resources, parse/free/converter/output and TLS declarations|
|dxil_spirv_c.cpp|`0bf241fa36d2967fe6e06b9f5d446c8d7aed06b8484ed610d6ba8a48cb57d81d`; Remapper, parsed_blob/converter ownership, scan result, run/getoutput, TLS/log wrappers|
|dxil_converter.cpp|`ad434fbcd506759447b0220d498c03d5f1c34fbc0ca8d50e8994205c77843389`; scan_srvs/uavs/cbvs/samplers at2817–2898, scan_resources3784, actual remapping/retry/representation|
|util/thread_local_allocator.cpp|`fc618a8f577e45d377a357b3638e7f0c1b654868566ed3ee131e3f9e10ad35ce`; allocator nesting/reset/end/allocation failure|
|util/thread_local_allocator.hpp|ThreadLocalAllocator and override-new/delete definitions|
|llvm_bitcode_parser.cpp,bc/module.cpp|Raw-bitcode parser/context lifetime, not outer container interpretation|
|dxil_parser.cpp|Container->copied bitcode span before raw parser|

Primary upstream links: [C API header](https://github.com/HansKristian-Work/dxil-spirv/blob/8434b0639fb2a48ce13aac08a420ecce5d23df0e/dxil_spirv_c.h),
[C implementation](https://github.com/HansKristian-Work/dxil-spirv/blob/8434b0639fb2a48ce13aac08a420ecce5d23df0e/dxil_spirv_c.cpp),
[resource scan/converter](https://github.com/HansKristian-Work/dxil-spirv/blob/8434b0639fb2a48ce13aac08a420ecce5d23df0e/dxil_converter.cpp),
[TLS allocator](https://github.com/HansKristian-Work/dxil-spirv/blob/8434b0639fb2a48ce13aac08a420ecce5d23df0e/util/thread_local_allocator.cpp).
These files carry MIT notices; existing C15 license/inventory notes govern any
future import. No implementation is copied here. Future project adapter/tests
must be original, and linking the reviewed runtime still requires all dependency
notices/pins rather than just the C header's license.

Local contracts inspected: `mesa/dxbc/dxbc_bindings.h`,
`dxbc_parse.c::dxbc_program_lower_bindings`, `dxbc_to_nir.c::mem_load`,
`mesa/winsys/agx_d3d12_windows.cpp` owner_binding_limits/create_shader/bind_stage;
EXP1389 `translate.c`, `nir-check.c`, associated checkpoint note and original
fixture manifest. Current C17 map uses kind/id/space/reg/slot, four independent
kind capacities14CBV/128SRV/8UAV/16sampler, maximum166 entries, and semantic root
lookup by kind/space/register. Its existing DXBC declaration-order allocation
must not be silently changed by adding a separate DXIL frontend.

## Exact ownership and cleanup order

1. Own/retain explicit-length input bytes throughout the operation. Run the
   committed bounded framing helper; choose its validated raw bitcode span and
   call `dxil_spv_parse_dxil`. Do not resniff a DDI pointer or use a full-container
   API as a substitute for validating caller bounds. `dxil_spv_parse_dxil_blob`
   remains useful for an independent actual-C-API regression comparison.
2. Call `dxil_spv_begin_thread_allocator_context` BEFORE creating any translator
   objects. It returns VOID and asserts no allocator already exists on that
   thread. This is not a nestable/refcounted scope. Initialize blob/converter
   handles toNULL ourselves: failing C API functions do not promise to zero them.
3. Parse and check the result. A successful parsed blob owns an LLVMBCParser,
   entry-point strings and related state. A converter holds a REFERENCE to that
   blob's parser, not ownership of the blob. If using
   `dxil_spv_create_converter_with_reflection`, both parsed blobs must outlive it.
   Initial adapter needs no second STAT/reflection blob for resource scanning.
4. Validate C API shader stage and select the intended single entry point using
   `dxil_spv_parsed_blob_get_num_entry_points`,
   `dxil_spv_parsed_blob_get_entry_point_name` and, where needed,
   `dxil_spv_parsed_blob_get_shader_stage_for_entry`. Returned names/raw IR strings
   are borrowed; copy needed names while blob/context remains alive. Stage enums
   are NOT interchangeable: DXIL program Compute=5, C API COMPUTE=6; pixel is0
   versus5. Use explicit translations, not enum casts.
5. Precollect/validate/freeze binding metadata as below. Create a fresh converter
   with `dxil_spv_create_converter`; set_entry_point copies its string internally.
   Install all four remappers, including explicit rejection for an unimplemented
   class. Callbacks retain userdata POINTERS; that original adapter state must
   stay alive and immutable during `dxil_spv_converter_run`.
6. Only after successful run, call
   `dxil_spv_converter_get_compiled_spirv`. Its returned data is a BORROWED view
   into converter->spirv. Check byte length/multiplicity/header and copy it into
   project-owned ordinary allocation. Copy any needed entry-point/warnings or
   workgroup metadata before teardown. There is no compiled-output-free API;
   never free that borrowed pointer. Never read old output after failed run or
   reuse a previously successful converter as failure recovery.
7. On every normal success/error path: free converter first using
   `dxil_spv_converter_free`, then free blob(s) with
   `dxil_spv_parsed_blob_free`, then optionally reset and finally end the thread
   allocator context. For a single call, end alone is sufficient; reset is useful
   for a batch only AFTER all objects from its previous job are destroyed.
   Converter/blob destruction after reset or end can dereference freed arena
   storage. All creates, callbacks, frees, reset and end stay on the SAME thread.
8. Publish owned SPIR-V, POD binding map and metadata only after all checks pass.
   On failure release partial owned allocations and leave public output empty.
   Owned output must remain usable after every C API object and TLS arena is gone.

EXP1389 already demonstrated the narrow success/rejected-remapper ordering:
parse blob, fresh converter, callbacks, run, owned output copy, converter free,
blob free, reset,end, then inspect/write owned SPIR-V. That harness only admitted
raw u0/space0, one resource, one entry and fixture-only SSBO0. It does not prove a
production binding implementation or all failure/OOM/thread behavior.

TLS interaction is an explicit adapter boundary: the C API has no query for an
already active foreign allocator context. A module owning all translator access
can enforce exclusive top-level calls and reject its own reentry; otherwise it
needs dedicated adapter-owned worker threads. A lock alone does not cure nested
same-thread begin. Do not change thread log callback state casually: the setter
is thread-local but offers no getter to restore a prior external callback. Initial
adapter can leave logging unmodified, avoiding retained log-userdata lifetime.

## OOM limitation: not solved by a C facade

Outer blob/converter allocations use new(nothrow) and return
DXIL_SPV_ERROR_OUT_OF_MEMORY when those allocations fail. This does NOT establish
complete recoverable OOM: begin uses ordinary new, internal vector/string growth
uses ThreadLocalAllocator, its allocation returns malloc/arenaNULL without a
standard throwing allocator check, and the reviewed build uses-fno-exceptions.
There is no C API allocator-failure callback. Constructor/internal failures can
therefore escape those outer result checks or terminate/violate allocation
requirements. This is source evidence of an unresolved failure contract, not an
observed injected-OOM verdict or a claim every OOM crashes.

The original adapter can guarantee cleanup for reported C API errors and its OWN
checked allocations. It cannot honestly promise recovery for all internal
translator OOM by wrapping C calls in a return-code check. Before UMD production
integration, derive and test a reviewed upstream allocation-failure policy (or
another explicitly approved architecture); do not silently alter external code
or claim that preallocating the input/output prevents internal OOM. Fault-injected
actual-library subprocess tests are appropriate later to isolate this boundary.

## Reflection supports precollection, with two important caveats

`dxil_spv_parsed_blob_scan_resources(blob,srv,sampler,cbv,uav,userdata)` traverses
LLVM dx.resources metadata before converter creation/run. The callback IDENTITY
includes semantic class from WHICH callback fired, stage, resource kind,
resource_index, register_space, register_index and range_size; UAV additionally
includes has_counter. `kind` in dxil_spv_d3d_binding is a resource SHAPE (raw,
structured,texture,etc.), not the CBV/SRV/UAV/sampler namespace class.

The scan wrapper unconditionally returns DXIL_SPV_SUCCESS. Internally a false
callback stops traversal, but this is not propagated as an error to the C caller.
Therefore collectors must record a sticky failure themselves. Use bounded storage,
returnTRUE while collecting all classes even after recording rejection, then
reject before conversion. Do not accept scan SUCCESS as proof every resource
was collected or supported.

Scan-created binding structs omit alignment (zero-initialized); actual conversion
remapping supplies raw alignment16 and structured alignment derived from stride.
Do not compare scan-time zero alignment with conversion-time alignment as an exact
key, or infer a structured stride from that zero. Additional typed/dimension/data
requirements must come from source-proven metadata or later converter validation.
The scan provides enough information for STATIC SINGLETON namespace collection,
not all shader semantics or arbitrary array/dynamic access proof.

## Deterministic map and representation plan

Initial proposed profile is a single compute entry, static singleton resources,
raw32 SRV/UAV and optionally proven CBV mappings. Reject arrays/ranges not equal1,
unboundedUINT32_MAX, counters, bindless/root-descriptor heaps, unsupported stages
and resource shapes. These are explicit initial adapter limits, not C API limits.

1. Collect a POD record per declaration: namespace class, semantic space/register,
   source resource ID, shape, count and counter flag. Validate finite capacities,
   unique IDs within each class and unique semantic keys; cross-class ID0 or
   register0 is legal. Reject duplicate/conflicting declarations rather than
   merging ambiguously. Keep fixed bounded storage; malformed large metadata
   cannot grow project-owned memory without limits.
2. Sort by(class,space,register) and assign dense slots INDEPENDENTLY per class,
   checking the current owner's actual capacity. Source ID is diagnostic/identity
   metadata, not the slot. Do not assign on scan or converter callback arrival.
   The resulting immutable semantic map can use the existing dxbc_binding_map POD
   contract for later root replay, with its namespace class translated explicitly.
3. Remapping callbacks look up the frozen record, verify stage/shape/ID/key/count/
   counter, and return its previously assigned slot. Unknown requests or changed
   declarations set sticky failure and returnFALSE; never create a new mapping
   while converting. Converter can retry an unbounded range as count1 after a
   failed callback; precollection rejects that range and sticky failure must never
   be cleared by a retry. Thus it cannot become an accidental range escape.
4. Give each namespace class a distinct SPIR-V descriptor-set identity and put its
   dense per-kind slot in binding. Numeric set assignments belong to the original
   adapter profile, not an upstream/Microsoft requirement. Initialize the entire
   C binding output, including unused counter/offset/bindless fields, explicitly.
5. Preserve representation as well as identity. The current owner binds raw/
   structured SRVs as R32_UINT PIPE_BUFFER sampler views and UAV buffers as
   shader_buffers; DXBC raw SRV loads become texture dword fetches, UAV loads/
   stores become SSBO operations. Therefore mapping BOTH classes to SSBO binding0
   is wrong even if they have independent D3D slots. A compatible initial candidate
   is C API raw32 SRV->TEXEL_BUFFER and raw UAV->SSBO; CBV->UBO. This route is
   source-supported by converter descriptor-type branches, but its exact NIR/owner
   contract must pass actual tests before owner admission. Raw16/64 operations
   needing SSBO/UBO/BDA cannot be assumed to work through this texel-buffer route.
6. Distinct SPIR-V sets alone are not a finished owner mapping. The later NIR
   adapter must consume(set,binding,type) while class identity is still available,
   lower each class to the matching Gallium namespace and preserve dense slot
   identity. EXP1389's descriptor lowering asserted set0/binding0 and converted it
   to SSBO0; generalizing that to discard sets would alias raw SRV/UAV accesses.
   An alternative unified SSBO namespace would require a new owner buffer-binding
   contract and is outside this step, not an implicit slot offset to guess.

## Proposed original public adapter API

Proposed names only, not implemented:

`agx_dxil_convert_owned(request,result)` with request containing explicit input
pointer/byte length/kind, structural stage/model profile, intended entry name,
static-resource feature profile and per-kind limits. It consumes input only for
the call and owns/copies anything retained during C API work. No raw-unsized DDI
pointer, pipeline/root handles, Screen or GPU context appears in this API.

Result contains project-owned SPIR-V bytes/count, immutable POD semantic binding
map and representation manifest (class/shape/slot->SPIR-V set,binding,type),
explicit stage, entry/workgroup metadata and bounded diagnostic code. A matching
project free function releases only project allocations. No translator object,
borrowed string, callback pointer or TLS memory escapes. Status distinguishes
framing, profile/resource refusal, reported parse/convert error and project OOM;
complete internal-OOM recovery remains blocked as described above.

## Smallest meaningful actual-C-API regressions to implement later

A. Reuse exact portable EXP1389 container/body. Run bounded framing; raw
`dxil_spv_parse_dxil` on its verified span; precollect one raw UAV u0/space0;
freeze slot0; convert; copy output; free/reset/end; validate copied SPIR-V and its
resource decoration afterward. Independently compare blob-parser resource/entry/
workgroup metadata, not necessarily byte-identical SPIR-V from unrelated options.
Negative: reject that same known binding deliberately; require reported run failure,
empty public output and cleanup, then immediately repeat valid conversion on the
same thread to expose stale handles/TLS state.

B. New ORIGINAL HLSL design, not created/compiled in this task:

- ByteAddressBuffer A:t0/space2 and B:t0/space7;
- RWByteAddressBuffer X:u0/space1 and Y:u0/space9;
- optional cbuffer Params:b0/space3 with a nonzero bias, added only with UBO gate;
- one compute thread loads distinct words from A and B and stores different
  noncommutative expressions to X and Y. Every resource must affect observable
  output, preventing compiler elimination from hiding the binding problem.

Gate exact precollected keys and sorted per-kind slots: SRVs0/1, UAVs0/1 and,
if included, CBV0. Shuffle the collected POD records before final sorting and
compare the immutable map and every callback lookup. Evaluate actual SPIR-V
resource types/set/binding decorations and later NIR load/store targets: raw SRVs
must remain texel fetches and UAVs distinct SSBO0/1. Distinct values/noncommutative
expressions catch swapped spaces and accidental SRV/UAV alias. This is an actual
C API/compiler-only gate; no GPU is necessary to distinguish these causes.

Negatives: sticky scan rejection despite scanSUCCESS; unknown/mismatched remap
key; duplicate semantic key; over-capacity map; unbounded/array range; counter;
unsupported shape/stage; malformed bitcode inside structurally valid framing;
caller-owned allocation failure; and failed call followed by valid call. A small
separate array/counter HLSL variant can provide real unsupported reflection data
rather than forging an upstream callback. No new DXC invocation is authorized
by this read-only note.

Thread gate: independent jobs on two adapter-owned threads, all output inspected
after their own TLS teardown, plus rejection of wrapper reentry before touching
upstream begin. Do not test unsupported foreign nested begin by crashing the
production process. Internal allocator OOM tests need an isolated subprocess and
an exact injection method; no passing result is asserted here.

## Decision / remaining gates

Precollection is source-proven for logical static singleton keys. Callback order
must not allocate slots. Scan success and scan alignment are weaker than they look;
use sticky validation and representation-aware immutable maps. Cleanup order is
fully derivable for reported errors, but robust internal OOM remains unresolved.
The proposed rawSRV TEXEL_BUFFER/UAV SSBO route has no actual multi-space fixture
verdict yet. Typed R1/library adapters, owner/NIR integration, build graph and DDI
payload form remain OUT. No model/capability or runtime-acceptance claim follows
from the standalone owned converter design.

## Bounded OOM follow-up: exact8434 has no supported recovery policy

2026-10-18 read-only follow-up, no code changes or fault-injection/build execution.
Inspected the complete C API allocator declarations, actual ChainAllocator and
ThreadLocalAllocator implementations, C API constructors/error boundaries,
upstream CMake compiler options and the existing host `flags.make` receipts.
**No supported allocator callback, checked-allocation mode or complete recoverable
failure policy was found at this exact pin.** The only allocator controls in the
public API are begin/reset/end, all VOID. They select arena lifetime/optimization;
they do not install an allocator, receive failures, or set a memory budget.
The presence of DXIL_SPV_ERROR_OUT_OF_MEMORY is not a whole-library guarantee.

Precise source-derived failure paths:

| Site | What source actually does on failure | What cannot be promised |
| --- | --- | --- |
|begin_thread_allocator_context|Asserts allocator absent, then ordinary new ChainAllocator; no catch or result output.|A recoverable C status if that new fails. It is not a nothrow allocation.|
|ChainAllocator::ensure_block|Appends a Block, whose storage is malloc(64KiB); returns false if its storage isNULL. The failed Block remains in the vector.|No exception/result translation beyond allocate returningNULL; no rollback of the failed block.|
|Small ChainAllocator::allocate|ReturnsNULL when the required new block failed. A later allocation can revisit that recorded failed Block.|No usable memory, no sticky failed state, no all-or-nothing job contract.|
|Block::allocate on a failed Block|block_size remains the requested size even though block.get() isNULL; the size comparison can pass and code performs pointer arithmetic on that NULL backing pointer and advances offset.|No safely reusable failed arena. At offset0 the result may beNULL; later offsets are not allocated storage. This is an invalid/undefined pointer path, not an OOM return code.|
|allocate_huge|Appends a Block for the requested size and directly returns its malloc-backed pointer, possiblyNULL.|No complete checked propagation to callers.|
|No TLS allocator active|allocate_in_thread directly returns malloc(size), possiblyNULL.|Disabling the arena does not restore an exception/result failure contract.|
|ThreadLocalAllocator<T>::allocate|Returns allocate_in_thread(sizeof(T)*n) directly, with no non-NULL/throw check and no checked multiplication.|Standard-container allocation requirements are not fulfilled by simply returningNULL for a nonzero request.|
|Vector/String/UnorderedMap growth|Use that custom allocator. The inspected libc++ allocator_traits forwards allocate directly; it does not turn a custom allocator's NULL result into bad_alloc.|Do not assume all STL growth failures throw and can be caught outside the C API. Null storage can reach pointer arithmetic/construction.|
|Outer blob/converter new(nothrow)|Checks a failed outer allocation and returns OUT_OF_MEMORY; their constructors and subsequent member/container allocations are separate operations.|nothrow does not make an allocating constructor's internals or the rest of parse/run safe.|
|C API functions|No general try/catch boundary surrounds parse/create/scan/run and setters.|No universal translation of internal C++ failures to dxil_spv_result.|

The no-TLS and small/huge allocation branches all converge on the same missing
failure contract. A project-owned output malloc check only protects that one
allocation. Neither an input-size bound nor an output reserve proves that all
internal object, STL, SPIR-V builder and parser allocations will succeed.

### Compiler-policy precision

Upstream `CMakeLists.txt` explicitly appends-fno-exceptions for GNU/Clang targets.
Existing host `flags.make` receipts confirm that flag for dxil-utils,
dxil-converter, spirv-module and the C API static/shared targets. This is concrete
host build evidence. The MSVC branch instead lists warning/platform options and
DOES NOT explicitly append the equivalent exception-disabling switch. Therefore
do not extend the host flag claim to every Windows archive without its actual
compile-command inventory. Absence of-fno-exceptions alone would still not fix
the custom allocator's NULL returns or add missing C API catches.

-fno-exceptions must not be paraphrased as "every ordinary new always returnsNULL"
or "every allocation failure necessarily aborts at this exact instruction."
Ordinary new is supplied by the linked C++ runtime; runtime/helper failures may
throw or terminate according to that implementation and compile policy. Frames
compiled without exception cleanup support are not a proved recoverable unwind
path. Independently, the custom allocator's NULL-return path is already unsafe
without any exception. No injected OOM run was performed, so no specific crash,
exception code or instruction is asserted as observed behavior.

### Minimal dependency options, all requiring separate approval/review

1. **Prefer a separately reviewed upstream allocator/failure patch or a future
   upstream pin with an explicit supported contract.** A minimal coherent patch
   must prevent failed-block reuse, check allocation arithmetic, enforce the
   allocation contract consumed by STL/custom new, and propagate failure at every
   exposed boundary, including VOID begin/setter APIs. If using exceptions,
   compile the relevant dependency graph with compatible exception/unwind support,
   catch at a safe boundary, and use exception-safe local ownership so a partially
   initialized blob/converter cannot leak before an output handle is published.
   An additive checked-begin API or another explicitly designed upstream error
   mechanism is needed if callers must obtain a C status for begin failure; do
   not invent such a symbol in the current8434 C header. Tests must cover both
   ordinary and TLS allocations, constructors, parser/container growth, callback
   setters, conversion/output growth, repeated jobs and cleanup.
2. **A reviewed C++ boundary plus allocator correction is possible, but is a
   dependency change, not merely our C adapter.** Enabling exceptions by itself
   is insufficient: all custom NULL returns must first become a defined failure
   mechanism, and relevant frames need compatible unwinding/cleanup. A global
   new_handler does not intercept custom malloc failures. Compiler null-new
   checks cannot repair custom STL allocator semantics. This option must receive
   the same source/license/build-policy and fault-injection review as option1.
3. **Out-of-process translation can contain an unrecoverable compiler failure,**
   but introduces a new IPC/service/artifact-validation/lifetime architecture.
   It is not the smallest in-UMD dependency correction and is outside the current
   C15 source-only adapter scope. No such architecture is proposed for immediate
   implementation here.

The dependency is MIT at the reviewed pin. Any original patch should remain a
separate, reviewable upstream/delta artifact with original notices preserved;
record patch hash and resulting compiled-source manifest, and run isolated
actual-library allocation-failure tests on the supported compilers before
claiming a safe UMD integration. Do not edit the ignored candidate in place and
then label it unchanged8434, or conceal a patched allocator behind a lockfile
containing only the original revision.

**Blocker:** exact8434 plus its present allocation implementation has no proved
recoverable internal OOM contract suitable for the proposed in-process UMD
owned-converter guarantee. The standalone original framing parser and reported-
error cleanup design remain valid, but a C facade cannot close this dependency
failure boundary. The next distinct gate is an explicitly scoped external
allocator/error-propagation design and deterministic fault-injection plan; no
hardware experiment or repeated source archaeology is justified.

## EXP1396 BUILD_ONLY: initial original adapter implementation

Parent preregistered RED in dc3102c9 and the frozen GREEN implementation SHA in
4d64d6ce. No hardware, guest or dependency rebuild. Original NEW
`mesa/dxil/agx_dxil_owned.{h,c}` uses the committed explicit-length framing parser
and existing reviewed host archives. It supports ONLY one compute6.0 entry with
static singleton raw UAV declarations; SRV/CBV/sampler/counter/range extensions
remain refused. This is an offline API, not a driver build input or DDI path.

The new-feature RED was a compile failure because the declared original owned
adapter implementation did not yet exist. It is not evidence of an upstream
functional defect. Then the implementation was written and frozen at SHA256
`6224d2ec8bf9cdbf02b43772755f7c9c6d1537e7a3fff85d7f182697474ae2c7` before the
parent authorized GREEN compilation/run.

Exact authorized host command, phase red then green:

```
AGX_C15_OWNED_RECEIPTS=/Users/pavel/public_windows/.local/c15-owned-1396 AGX_C15_OWNED_PHASE=green PYTHONPATH=tests python3 -m unittest test_dxil_owned -v
```

`tests/test_dxil_owned.py` is explicitly opt-in: ordinary discovery does not launch
this separately preregistered external compiler/link checkpoint. It records exact
clang/clang++ commands, every source/header/archive hash and raw step outputs.
It compiles only the original parser/adapter/actual-C-API fixture with C11,
Wall/Wextra/Werror, O1 and ASan/UBSan, then links the existing nine Release archives.
Those upstream archives were NOT rebuilt with sanitizer instrumentation, so this
is not complete external-code ASan coverage. No compiler install/fetch/new DXC
shader or production build-script edit occurred.

Actual result:

```
C15_OWNED_CONVERTER_PASS one_key=YES raw_and_container=YES scan_false_success_verified=YES reject_then_valid_same_thread=YES output_after_TLS=YES map_sorted_multikey=UNPROVEN internal_OOM=UNPROVEN
```

The fixture first verifies actual upstream scan returns SUCCESS despite a false
resource callback, then verifies converter remap rejection and normal cleanup.
It performs repeated owned valid/limit-rejected/valid calls on the same thread,
checks exact semantic u0/space0->dense UAV slot0, SPIR-V descriptor set2/binding0,
workgroup64x1x1, immutable input/output canaries and owned SPIR-V after the adapter
has destroyed converter/blob/TLS. Raw program input is checked separately from
container input. Missing entry and malformed program return no owned output.

Map collection has sticky rejection, sorts before slot assignment and uses frozen
lookup callbacks, but the existing one-key fixture cannot prove multi-space order
independence. That needs the separately designed new HLSL/DXC checkpoint; it is
explicitly unclaimed here. No promise of recoverable internal library OOM follows.
The external allocator blocker above remains a production-integration gate.

Receipts: `/Users/pavel/public_windows/.local/c15-owned-1396/before-inputs.json`,
`red/{inputs,commands,results}.json`, `green/{inputs,commands,results}.json`, raw
numbered stdout/stderr files, original objects/executable and
`EXP1396-verdict.json`. SPIR-V was inspected from owned memory after teardown and
then freed; no persistent SPIR-V file was emitted by this minimal fixture.
