# EXP1401 proposal: execute the exact actual EXP1398 post-NIR

2026-10-18. PREPARATION ONLY. No compiler/run/converter/dependency build, guest,
hardware, production source, DDI, owner, caps or ledger change. Existing1398 source
and receipts are preserved. New original test-only header/runner/pins are prepared,
but have NOT been compiled or executed. Parent must confirm ID/BEFORE first.

## Exact boundary and primary source pass

EXP1398 proves five-key structural namespace/representation preservation; it does
not prove integer arithmetic. This checkpoint executes instructions in the actual
NIR object produced by the SAME already validated1398 pipeline. It does not parse
NIR text, regenerate HLSL expressions, interpret a new invented IR, invoke DXC, or
use expected results to compute actual outputs.

Primary source inspected: pinned Mesa `compiler/nir/nir_opcodes.py` integer
operation definitions and nir_op_infos metadata; nir_intrinsics.py load_ubo,
store_ssbo/offset_shift definitions; nir.h SSA/source/swizzle/CF/texture types;
actual1398 post.nir; existing ORIGINAL EXP1389 cpu-oracle.h. Existing1397 original
`tests/dxil_spaces_oracle.py` and its previously generated binary results supply
independent input/expected data. Mesa source definitions are specification evidence;
the new evaluator is original project MIT code, not copied Mesa implementation.

Actual post-NIR has only:

- load_const, integer ALU iadd/isub/imul/amul/iand/ishl/ult;
- load_global_invocation_id, load_base_global_invocation_id, load_ubo, store_ssbo;
- unsigned buffer txf, structured block/if nodes, no phi/loop/jump/call.

All ALU values are32-bit except1-bit ult result. Swizzles select source components
using actual nir_alu_src fields. The evaluator consults actual opcode metadata for
input count and refuses any opcode outside this exact set. It does not silently
extend support when the pipeline changes.

Important primary distinction: imul yields low32 bits; iadd/isub use uint32 wrap;
ishl masks the shift count to31. **amul is address arithmetic with undefined
overflow**, so the evaluator checks the widened product and refuses overflow
rather than claiming generic wrapping multiplication. Actual addresses use at
most191*4=764, without overflow. ult produces a1-bit condition and drives the
actual NIR if node; no separate C `gid<173` bypass computes the shader's branch.

## Restricted original interpreter contract

`tests/dxil_nir_semantic_oracle.h` reads actual nir_instr/nir_def and CF nodes.
It preflights BOTH branches before lane execution, rejecting unknown ALU,
intrinsic, CF or texture forms, including instructions on an unexecuted branch.
The profile has four SSA components, at most4096 SSA slots and bounded CF depth16;
these are explicit test limits, not driver or NIR capabilities.

For every lane0..191, create fresh checked SSA value/defined-component storage:

- Global invocation ID is(gid,0,0), base global ID is(0,0,0), corresponding to the
  fixture's existing dispatch3x1x1 and local size64x1x1. Source values are read
  through the actual SSA index and swizzle, with bounds and definition checks.
- UBO loads read actual slot/offset sources, require slot0 and the actual16-byte
  four-component32-bit read within CBV storage. They do not substitute a hardcoded
  seed/count expression inside the interpreter.
- Buffer txf reads its actual texture_index and evaluated coordinate from the
  two192-word SRV inputs. Only the actually used R component is marked defined.
  The unused G/B/A components are NOT invented or silently filled; any later
  read from one fails as undefined under this restricted profile.
- SSBO stores read actual value/slot/byte-offset sources. Require scalar32-bit
  wrmask=x, offset_shift0, slot0/1,4-byte alignment and range by subtraction
  before indexing. The interpreter does not force the offset to equal gid*4;
  independently comparing every output catches a wrong but in-bounds address.
- Arithmetic is performed from instruction operands with explicit unsigned
  low32 semantics; no signed-overflow C operation is needed. Unsupported forms,
  undefined reads/amul and bounds errors return explicit test status. Failed
  execution discards partial outputs rather than publishing them.

The positive oracle compares ALL192 words in EACH UAV (384 total), not only the
173 active prefix. It also checks exactly one store to each UAV for active lanes
and zero stores for19 tail lanes. The initial distinct sentinels must remain in
both tails. No HLSL output formula appears in the interpreter; expected binary
arrays were generated independently by the committed original1397 Python oracle.

## Negative sensitivity and refusal tests on actual NIR

- Swap the two SRV binding data arrays while executing the SAME actual NIR:
  execution remains valid, but the golden comparison must fail.
- Zero only the CBV seed word while executing the SAME NIR: arithmetic must remain
  defined but golden outputs must differ. Count/guard remain173.
- Clone the actual NIR and redirect its second UAV store to the dominating first
  store's existing slot0 SSA source. No new IR or source-language algorithm is
  constructed. This remains valid NIR, but the golden outputs/store distribution
  must fail. The original shader and1398 dump are not mutated.
- Clone the same actual shader and change one existing imul to ixor, a valid NIR
  opcode outside this evaluator's explicit set. Preflight must return unsupported
  before execution. The test does not accept silently emulating an unknown op.
- Supply a172-word SRV extent: actual lane172 must cause a bounded-read refusal,
  without an out-of-bounds C access or partially published outputs.

Negative arithmetic cases compare against the original untouched expected arrays;
they do not create new expected expressions that could mirror a wrong evaluator.
No generic interpreter, backend code generator or GPU dispatch is introduced.

## Exact input inventory: SEVEN binary arrays, plus JSON

Existing SPIR-V2152B:
`$HOME/public_windows/.local/c15-owned-1398/green/mixed.spv`, SHA256
`24e3df26411f940c71e4dc2322d38226cb3c1d50725e07d27b890aa9b2b307d4`.
Original post-NIR3848B SHA256
`ee2c212cca1cd284b0eb8d11bd242e0eee1184ec76aca92af6b45cf19458708c`.

Existing `.local/c15-spaces-1397/oracle-output` contains exactly7 binary arrays:

| Input | Bytes | SHA256 |
| --- | ---: | --- |
|cbv_b0_space3.bin|16|`a8522a1dcbea0b5db570e2cf2fcf870bdb749b083fdbc9381c999421d713f1c5`|
|srv_t0_space2.bin|768|`901abe1c10552838ca6b03a6a278843ee164d57cb4ed87a7ae4e911ad95ac67e`|
|srv_t0_space7.bin|768|`9b84ababf0ae07d761e7751c5e0f4ffc6d45411938cf9ad3ed458e38ff454b2b`|
|initial_u0_space1.bin|768|`d830053706e352ad46a336bcc29b6a7e7e93a1eee50b3e8b6403781a92606bb4`|
|initial_u0_space9.bin|768|`f080dcf3ec9f7c11262b0a5a697e20376dd9d8ba82a84faae39ae93d1ffa1311`|
|expected_u0_space1.bin|768|`aba11b1d128a762fb39b374a9d1817115a34f468622a8b20f8be6a085cdac6f2`|
|expected_u0_space9.bin|768|`af2f46607d4dc7bf008997bc0f09b1df07ca7884e9cc3358894021e6eeef1967`|

vectors.json18475B SHA256
`a1d083f17988f40c84f87809cda00ea4c8aae09284bd228b5a3eab95a47f0a07`.
Committed independent oracle source SHA256
`17637f129a1bfa7ba0d56d3fb0f873a48f14b6f88177c0dfeaee6fb1b50c1571`.
No oracle or data-generation script was run in this preparation pass.

## Prepared sources, commands and parent approval gate

Prepared NEW test files only:
`tests/dxil_nir_semantic_oracle.h`, `tests/test_dxil_nir_semantic.py`,
`tests/dxil_nir_semantic_pins.json`. The runner generates a variant of the EXACT
`tests/dxil_owned_mixed_nir_fixture.c` (SHA24335148...) into the receipt directory,
adding only the original interpreter include/call and a vectors-directory argument.
The original1398 source is unchanged. Every original lowering operation remains
identical, and the runner requires the resulting post.nir hash to match1398
exactly after read-only interpretation. It also byte-compares both emitted actual
UAV arrays to their independently pinned expected binary files.

Prepared source manifest:
`$HOME/public_windows/.local/c15-nir-semantic-1401/before-inputs.json`, SHA256
`f7de5506a4512f528b2b2ffba591a5ee58b03284c568351dcbb2359ed205357b`.
Pins SHA256 `18602a87c04f9b84c1ac98b71d1e6c38f7397ac830a55a9e3df694a3d27822a3`:
51 existing dependency/tool/source/artifact files, including exact seven binaries.
No pin is merely recorded: the opt-in runner enforces them BEFORE compiler launch.
Original new source/runner hashes are in the approved manifest as well.

Exact proposed command, NOT EXECUTED:

```
AGX_C15_SEMANTIC_RECEIPTS=$HOME/public_windows/.local/c15-nir-semantic-1401 AGX_C15_SEMANTIC_APPROVED_MANIFEST=$HOME/public_windows/.local/c15-nir-semantic-1401/before-inputs.json AGX_C15_SEMANTIC_APPROVED_SHA256=f7de5506a4512f528b2b2ffba591a5ee58b03284c568351dcbb2359ed205357b PYTHONPATH=tests python3 -m unittest test_dxil_nir_semantic -v
```

Exact compile/run arrays are in `prepared-commands.json` under that receipt root,
SHA256 `9eee8bf79255181a828f16360669f5bae5a47bce85c5d344ecacc27058d6598b`.
They reuse every actual1398 NIR compile/link flag and existing library, replacing
only generated test source and output executable. No converter run or translator
link is necessary. The executable consumes the pinned2152B SPIR-V and existing
vector directory, and emits pre/post NIR plus actual UAV binary arrays. Preserve
all raw logs, commands, generated source hash and output hashes; never overwrite
an attempt. Original test code gets ASan/UBSan; pre-existing external archives
are not fully instrumented.

WHAT IS STILL UNKNOWN: whether this restricted evaluator and actual NIR agree
with all384 independent golden words and reject the planned negative mutations
is precisely the unexecuted CPU gate. Prepared source is not a passing result.
No semantic/hardware/driver support is claimed until the corresponding gate runs;
even a CPU PASS would not establish owner/DDI/runtime/GPU behavior or internal
translator OOM recovery.

## EXP1401 actual fixed-NIR semantic result

Parent BEFORE9603aabc authorized the exact prepared source manifest and two host
commands. All51 pins and five original-source hashes passed before compilation.
The first authorized compile/run passed without source/expectation changes:

```
C15_NIR_SEMANTIC_PASS lanes=192 active=173 tail=19 compared_words=384 stores=346 uint32_wrap=YES amul_overflow=CHECKED_UNDEFINED srv_swap_bad=346 seed_loss_bad=346 wrong_slot_bad=346 unsupported=REFUSED bounds=REFUSED ACTUAL_NIR_ONLY NO_GPU
```

Both192-word outputs byte-match the independent1397 expected files, including the
19 untouched tail words in each UAV. Every active lane stored exactly once to
each UAV; tail lanes stored nothing. Each of SRV swap, CBV seed loss and wrong
UAV slot produced346 differing words, while the correct execution produced zero.
The unsupported-opcode clone returned refusal before execution and the172-word
SRV extent returned bounds refusal with cleared partial output. No fabricated
HLSL algorithm was used to compute actual results.

The post-NIR remained byte-identical to1398, SHA256
`ee2c212cca1cd284b0eb8d11bd242e0eee1184ec76aca92af6b45cf19458708c`.
The original1398 structural fixture and receipts were not edited. Its inherited
structural PASS marker still says SEMANTIC_EXECUTION_UNPROVEN, reflecting that
fixture's original scope; the separate new semantic marker above is the1401
proof for this exact fixed shader/input domain, not a general interpreter claim.

Receipts are under `$HOME/public_windows/.local/c15-nir-semantic-1401`:
approved inputs/pins, generated actual-semantic-nir.c, exact commands/results,
raw stdout/stderr, executable, pre/post NIR, actual UAV binaries, artifact manifest
and EXP1401-verdict.json. Actual UAV0 SHA256
`aba11b1d128a762fb39b374a9d1817115a34f468622a8b20f8be6a085cdac6f2`;
actual UAV1 SHA256
`af2f46607d4dc7bf008997bc0f09b1df07ca7884e9cc3358894021e6eeef1967`.

Only our original test code was newly compiled with ASan/UBSan; existing external
Release libraries were not rebuilt or fully instrumented. No owner/DDI/Windows/
GPU execution, production capability, complete SM6 conformance or recoverable
internal translator OOM claim follows. No external rebuild, guest command,
hardware operation, commit or ledger edit was performed by this lane.
