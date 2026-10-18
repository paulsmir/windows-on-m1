# EXP1398 proposal: bounded mixed static binding map and actual NIR representation

2026-10-18. READ-ONLY plan, new note only. No adapter/test implementation edits,
compiler, converter, NIR execution, guest, hardware, package, caps or ledger work.
Current owned adapter is under independent review and remains unchanged.

## Inputs and smallest scope

Parent reports EXP1397 DXC/CPU PASS: original shader has CBV b0/space3, raw SRVs
A:t0/space2 and B:t0/space7, raw UAVs X:u0/space1 and Y:u0/space9, all singleton,
compute6.0 main64x1x1. DXIL3704 bytes SHA256
`b22b71c9856cd3f2514a98609331b9ee4ab2b5e83aa8086d148243c277d52de5`;
HLSL679 bytes SHA256 `a3639f4285c36d0ccf38a10cba51aff9f6ff519bed33fe25b5e9b873643c5131`.
Use the parent's portable `tests/fixtures/dxil/exp1397_spaces_cs60.dxil` plus its
provenance, not a new DXC invocation. The already built local copy was read only.

Extend ONLY static singleton CBV/raw32 SRV/raw UAV resource-class admission,
frozen map lookup and descriptor representation. Keep compute6.0/single entry,
existing capacities, no counter/array/unbounded/bindless/texture/sampler classes,
TLS ownership/reported-error cleanup and external OOM blocker. No DDI, runtime,
owner, root replay or production build/cap change. No new native backend lowering
or AGX executable is required to prove this namespace/representation checkpoint.

WINDOWS CONTRACT: source registers retain independent CBV/SRV/UAV namespaces and
register spaces. TRANSLATION: precollect semantic keys, sort per kind, freeze,
then map to typed SPIR-V descriptors and verify actual NIR retains the same
kind/slot interpretation before lowering away Vulkan descriptor identity.
AGX/ASAHI CONTRACT: existing owner uses constant_buffer slots for CBVs, R32_UINT
PIPE_BUFFER sampler views for raw SRVs, shader_buffers for raw UAVs. No unified
SSBO index space for SRV+UAV is currently bound by that owner.

## Exact reviewed C API fields and source evidence

External pin8434b0639fb2a48ce13aac08a420ecce5d23df0e remains unchanged, C API2.75.0.
Inspected exact `dxil_spirv_c.h` resource-kind/descriptor enums and unions;
`dxil_spirv_c.cpp::Remapper`; `dxil_converter.cpp` scan_srvs/uavs/cbvs,
emit_srvs/emit_cbvs and get_ssbo_offset_buffer_id. Source hashes and MIT notices
remain in the owned-converter contract; no implementation is copied.

| Semantic class | Scan/conversion callback identity | Exact populated C output |
| --- | --- | --- |
|SRV|srv callback; kind RAW_BUFFER11; stage COMPUTE6; ID/space/register/count1|Zero entire dxil_spv_srv_vulkan_binding. buffer_binding.set=DXBC_BINDING_SRV1; binding=frozen SRV slot; descriptor_type=TEXEL_BUFFER2; bindless.use_heap=FALSE; offset_binding remains zero.|
|UAV|uav callback; nested d3d_binding RAW_BUFFER11; has_counterFALSE|Zero entire dxil_spv_uav_vulkan_binding. buffer_binding.set=DXBC_BINDING_UAV2; binding=frozen UAV slot; descriptor_type=SSBO1; bindless.use_heap=FALSE; counter_binding and offset_binding zero.|
|CBV|cbv callback; kind CONSTANT_BUFFER13 (not RAW_BUFFER); stage COMPUTE6|Zero entire dxil_spv_cbv_vulkan_binding. push_constant=FALSE; vulkan.uniform_binding.set=DXBC_BINDING_CBV0; binding=frozen CBV slot; descriptor_type=UBO4; bindless.use_heap=FALSE.|

C API resource CLASS enum is SRV0/UAV1/CBV2/sampler3, whereas the current C17 map
kind enum is CBV0/SRV1/UAV2/sampler3. Infer class from WHICH callback fired and
translate explicitly; do not cast these enums. Resource-kind RAW_BUFFER11 describes
shape and is a third, separate enum domain. Scan alignment is zero, conversion
raw alignment16; don't treat scan alignment as part of the equality key. For CBV
alignment is0. Both sides must check range_size1, stage, source ID and semantic
key; UAV additionally rejects counters. Scan success does not propagate a false
callback, so sticky rejection remains mandatory.

The SRV TEXEL_BUFFER path does not require an offset descriptor with bindless
flags off: get_ssbo_offset_buffer_id enables that machinery for misaligned SSBO
or enabled bindless typed-buffer offsets. Keep all bindless/offset options at
their reviewed defaults. No SSBO descriptor type for raw SRVs is introduced here.
Raw16/64 operations are outside this fixture/profile proof.

Expected map (source IDs are captured from actual metadata, never assumed slots):

| C17 kind | Space | Register | Dense slot | SPIR-V set/binding/type |
| --- | ---: | ---: | ---: | --- |
|CBV0|3|0|0|0/0/UBO|
|SRV1|2|0|0|1/0/uniform texel buffer|
|SRV1|7|0|1|1/1/uniform texel buffer|
|UAV2|1|0|0|2/0/storage buffer|
|UAV2|9|0|1|2/1/storage buffer|

Every conversion remapper is a lookup into this pre-existing immutable map; no
callback arrival can allocate/change a slot. A retry from unbounded range to1
cannot erase sticky failure. Input and mapping remain unchanged across callbacks.

## Actual SPIR-V -> NIR identity preservation and safe lowering order

Inspected current pinned Mesa source, not a guessed Vulkan binding convention:

- `compiler/spirv/vtn_variables.c::vtn_variable_resource_index` writes desc_set,
  binding AND descriptor type into vulkan_resource_index for UBO/SSBO. The paired
  load_vulkan_descriptor carries the descriptor type. Variable construction also
  stores data.descriptor_set/binding on NIR variables.
- `compiler/spirv/spirv_to_nir.c` maps SpvOpImageFetch on a buffer image to
  nir_texop_txf, with buffer sampler dimension. Thus this is a texture fetch,
  not an SSBO load.
- `compiler/nir/nir_lower_samplers.c::lower_tex_src_to_offset` uses only
  deref->var->data.binding when forming texture_index and removes the deref. It
  DOES NOT preserve descriptor_set. Therefore verify class/set/type FIRST and
  apply the pass only after all relevant variables are known to be the admitted
  SRV buffer class. Distinct SPIR-V sets alone are not a final ownership mapping.

Proposed new ORIGINAL CPU NIR fixture:

1. Feed adapter-owned, persisted SPIR-V after converter/blob/TLS teardown to actual
   spirv_to_nir with the existing Vulkan environment and32bit_index_offset UBO/
   SSBO address formats. Use current agx_nir_options, same pinned libs as EXP1389.
   Run nir_validate_shader before and after the bounded lowering sequence.
2. Before consuming descriptor identity, enumerate all CBV/SSBO resource-index
   intrinsics: accept ONLY(set0,binding0,uniform_buffer),
   (set2,binding0,storage_buffer) and(set2,binding1,storage_buffer), with constant
   singleton array index0 and expected two32-bit address components. Reject
   reindex/dynamic/unknown tuples. Replace each validated resource index with
   ivec2(frozen_slot,0), preserve descriptor type, and replace matching descriptor
   loads with their address source. A fresh pass must not rewrite all descriptors
   to the old fixture's fixed SSBO0.
3. Inspect texture derefs/variables: accept only descriptor_set1, binding0 or1,
   32-bit unsigned buffer image shape, no arrays or sampler state. Lower only those
   verified SRV variables with actual nir_lower_samplers. Verify txf texture_index
   retains0/1 and sampler_dim is buffer. Reject any leftover resource deref that
   was not accounted for.
4. Use nir_lower_explicit_io for BOTH nir_var_mem_ubo and nir_var_mem_ssbo with
  32bit_index_offset. Normal copy/constant-fold/DCE cleanup may follow. Verify
   load_ubo targets only CBV0; store_ssbo targets include BOTH UAV0 andUAV1; txf
   targets include BOTH SRV0 andSRV1. Require no load_ssbo for the two raw SRVs,
   no leftover Vulkan resource intrinsics, no index modulo/flattening, and exact
   workgroup64x1x1. Inspect sets of targets rather than brittle exact instruction
   counts that legal compiler optimization can change.
5. Persist pre/post NIR text and owned SPIR-V with hashes. This structural gate
   proves representation/namespace retention, not full runtime binding or GPU
   execution. EXP1397's independent source arithmetic oracle does not by itself
   prove NIR arithmetic execution; do not label it as such. An extended NIR CPU
   interpreter would be an additional separately scoped semantic gate.

## Deterministic RED/GREEN and permutation proof

RED must compile the NEW mixed-fixture test against the CURRENT unchanged owned
adapter and expect successful conversion. It should fail at that actual assertion
because the adapter refuses SRV/CBV, with empty output. This is a real missing
resource-family boundary, unlike EXP1396's missing-implementation compile RED.
Preserve that raw result before extending admission. Do not relax the expected
outcome to call the refusal GREEN.

After authorized source edits, GREEN uses the same3704-byte shader: exact five
keys and independent slots, correct descriptor representations, owned output
usable after all TLS teardown, rejected limits/unknown metadata followed by valid
conversion on the same thread, and all actual NIR checks above.

The shader has distinct spaces but its declaration order may already match sorted
order. Passing it alone does not exclude callback-order allocation. Add an actual-
function unit gate over all120 permutations of the FIVE precollected actual C API
records: execute the adapter's real pure freeze_map/ordering logic and compare the
same key->slot map for every permutation. No invented matching sort and no test
switch in production callbacks. This can extract the actual static helper and job
record into a test translation unit, as existing repository actual-function tests
do. This remains a deterministic pure-map gate; actual conversion of the original
fixture independently proves C API callback lookup against that frozen map.

## Proposed exact checkpoint commands and artifacts (not executed)

No new DXC, translator/dependency configure/build, install or fetch is needed.
Reuse EXP1396's exact nine archives and hashes. Parent first persists EXP1397's
portable fixture/provenance and preregisters input/source hashes for1398.

Proposed NEW opt-in test command, phase red then green:

```
AGX_C15_MIXED_RECEIPTS=/Users/pavel/public_windows/.local/c15-owned-1398 AGX_C15_MIXED_PHASE=red PYTHONPATH=tests python3 -m unittest test_dxil_owned_mixed -v
AGX_C15_MIXED_RECEIPTS=/Users/pavel/public_windows/.local/c15-owned-1398 AGX_C15_MIXED_PHASE=green PYTHONPATH=tests python3 -m unittest test_dxil_owned_mixed -v
```

Planned outputs: phase/{inputs,commands,results}.json; raw numbered stdout/stderr;
original adapter/parser/fixture objects and mixed-converter executable;
green/mixed.spv and map.json; pure-map permutation result; pre/post NIR dumps;
final verdict/hash manifest. Future test source names:
`tests/test_dxil_owned_mixed.py`, `tests/dxil_owned_mixed_fixture.c`,
`tests/dxil_owned_mixed_nir_fixture.c`, and a generated actual-freeze-map unit.
These files are PROPOSED and were not created in this read-only task.

Exact proposed NIR compile/run argument arrays are saved in
`/tmp/EXP1398-proposed-nir-commands.json`, SHA256
`793d13a7126dab49b23ee2e975c782543442a1f518b7332a6d4cb19f64508203`.
They retain every flag/library from the recorded successful EXP1389 actual NIR
link command, substituting ONLY the original new fixture source and output:

- source: worktree `tests/dxil_owned_mixed_nir_fixture.c`;
- executable: `/Users/pavel/public_windows/.local/c15-owned-1398/green/mixed-nir-check`;
- run: that executable with `/Users/pavel/public_windows/.local/c15-owned-1398/green/mixed.spv`.

Basis command file is `.local/c15-sm6-8434b063/EXP1389-nir-link-command.json`;
proposal JSON records its exact hash. Libraries: existing libvtn, libasahi_compiler
(for actual options), libagx2_disasm, libasahi_layout, libnir, libcompiler,
libmesa_util, libmesa_util_simd, libmesa_util_c11, libblake3, existing zstd/z/pthread
and SPIRV-Tools dylib/libc++. No AGX backend compile function is invoked by the
proposed NIR fixture; linking an existing archive does not claim hardware output.
Only original new test/helper code gets ASan/UBSan instrumentation. Hash all
existing archives/tool inputs before parent authorizes execution; no complete
external-library sanitizer coverage claim.

WHAT IS STILL UNKNOWN: whether this exact translator+NIR path emits only the
admitted representations and preserves all five bindings is the falsifiable
1398 CPU gate. OOM production safety, semantic NIR execution, runtime/DDI shader
form, R1/library companions and actual owner integration remain separate blockers.
No hardware bit probe or current adapter edit belongs to this plan.
