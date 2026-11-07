# C15 Step2: original bounded DXIL input parser

2026-10-18 source/layout/license record BEFORE implementation. Scope: standalone
CPU helper with explicit caller byte length and explicit CONTAINER or PROGRAM
kind. No DDI integration, unsized-pointer inference, translator link, model/cap
change, builder, hardware or guest operation. A success validates framing and
reports spans; it is not DXIL/LLVM semantic validation or runtime acceptance.

## Primary sources and license boundary

Microsoft DirectXShaderCompiler source pin
`194bb75bea2becb42810d72a16195089c1908e10`:

- [docs/DXIL.rst, DXIL Container Format](https://github.com/microsoft/DirectXShaderCompiler/blob/194bb75bea2becb42810d72a16195089c1908e10/docs/DXIL.rst), SHA256 `c79fe2a8917c16da8141d960615b9e32c60c9678a123ba3c9819e41aab03b162`.
- [DxilContainer.h](https://github.com/microsoft/DirectXShaderCompiler/blob/194bb75bea2becb42810d72a16195089c1908e10/include/dxc/DxilContainer/DxilContainer.h), SHA256 `f9975fb71c62f1aa37cba989adc83f7dcbb06d48e36a62a0b2ebf29c26eaa397`.
- [DxilContainer.cpp, IsValidDxilContainer](https://github.com/microsoft/DirectXShaderCompiler/blob/194bb75bea2becb42810d72a16195089c1908e10/lib/DxilContainer/DxilContainer.cpp), SHA256 `6c2d09130f9807d8696cb6760666bac4e2f6b62dc5711ba6a30c65eff7e72bb1`.
- [DxilConstants.h, ShaderKind](https://github.com/microsoft/DirectXShaderCompiler/blob/194bb75bea2becb42810d72a16195089c1908e10/include/dxc/DXIL/DxilConstants.h).

The inspected Microsoft headers carry University of Illinois/NCSA notices, covered
by repository LICENSE.TXT. They are primary format evidence only: no source,
helper implementation, packed struct or license text is imported. This project's
new C parser and tests are original MIT code, using bytewise numeric field reads
and independently written bounds arithmetic. The original EXP1389 shader fixture
may be used as test data; it contains the project's authored HLSL compiled by the
existing pinned compiler, not imported implementation code.

Reviewed translator pin remains dxil-spirv
`8434b0639fb2a48ce13aac08a420ecce5d23df0e`, MIT, with the previously reviewed gitlinks.
Read local `dxil_spirv_c.cpp` parse_dxil_blob versus parse_dxil and
`dxil_parser.cpp::parse_dxil/parse_container` at that pin. The blob API locates a
container's program and parses its bitcode; the raw API consumes the caller's raw
bitcode span. We copy neither implementation and link neither in this task.

## Observed layout and exact original fixture

EXP1389 input `/Users/pavel/public_windows/.local/c15-sm6-8434b063/compute.dxil`:
2824 bytes, SHA256 `f3934fccef3b1428d62ed6a675540cab1bf674faaf85c67320e3ed83c6c62bb4`.
Container has DXBC magic, major1/minor0, size2824 and7 parts. DXIL part header
starts1540; its body starts1548 and has1276 bytes, SHA256
`b2c39e1fbc25150750de33858b45f7fb17091013f799c3e68eea3c017c164df7`.
Program words are version0x00050060 (compute6.0), size319 DWORDs,
DXIL magic0x4c495844, DXIL version0x100, bitcode offset16, bitcode size1252.
Bitcode is at program offset24, hence container offset1572. This confirms the
BitcodeOffset origin is DxilMagic (program+8), not program or container start.

Container header is32 bytes: magic0, digest4..19, uint16 major20/minor22,
uint32 total-size24, part-count28, then count uint32 part offsets. Each part is
8-byte header plus declared body size. Microsoft's current validator requires
parts in table order to be contiguous from the offset table to declared container
end, accepts caller bytes beyond declared container size, checks major1, and caps
container size at0x80000000. It does not require a specific minor or alignment.
Those exact source-backed structural rules are the initial container policy.
They reject overlap/duplicate offsets without a quadratic pair scan. Exactly one
DXIL part is required by this helper's explicit DXIL-input purpose; a second DXIL
part is ambiguous and invalid, and no DXIL part is unsupported input. Unknown
non-DXIL parts are bounded and skipped, not interpreted as shader programs.

Program header is24 bytes. Checked SizeInUint32*4 must include that header and fit
the supplied program span. The bitcode starts8+BitcodeOffset, must follow the
16-byte bitcode header, have nonzero length, and fit the declared program size.
Caller bytes or part padding beyond declared program size are allowed; bitcode
may not escape into them. No bitcode alignment, exact bitcode-size-to-DWORD
multiple, fixed bitcode offset16, or fixed DXIL version1.0 is invented. DXIL
version is reported numerically for later frontend validation. Container digest,
LLVM bitcode and shader semantics are not authenticated/validated by this helper.

## Proposed bounded API

NEW `mesa/dxil/agx_dxil_program.{h,c}` under the driver:

- `agx_dxil_parse(const void *input, size_t caller_bytes, enum input_kind,
  const struct profile *, struct result *)`.
- Mandatory explicit kind CONTAINER or PROGRAM; no magic sniff fallback.
- Result contains only numeric offsets/lengths relative to the ORIGINAL input:
  container_bytes (0 for PROGRAM), program_offset/program_bytes,
  bitcode_offset/bitcode_bytes, shader kind, major/minor, raw program version and
  DXIL version. No pointers, allocation, retained borrowed data or global state.
- Profile specifies supported shader-kind bitmask and minimum/maximum shader
  model encoded major<<4|minor. Stage/model rejection is UNSUPPORTED after complete
  structural validation. No default SM6.0-only restriction is hidden in parsing.
- Status OK, INVALID framing/arguments, or UNSUPPORTED profile/input. Output is
  cleared before every failure, including unsupported kind/stage/model and invalid
  profile. NULL output is INVALID; zero-length/NULL input is INVALID.
- All reads bytewise little-endian, so unaligned input is supported. All bounded
  size calculations use subtraction/division checks before addition/multiplication.
  No pShaderCode pointer is dereferenced without the explicit supplied length.

WHAT REAL BUG OR INVARIANT WILL THE TEST CATCH? Wrong DXIL bitcode-offset origin;
reading beyond caller/program/container spans; offset/count arithmetic overflow;
ambiguous DXIL parts/overlapping container sections; silently interpreting a
program as a container; returning stale spans after failure; and conflating a
structurally malformed input with an unsupported stage/model.

Proposed CPU tests: exact EXP1389 container and extracted body; numeric span
agreement; every truncation prefix; empty/invalid args; self-size and bitcode
bounds including UINT32_MAX; duplicate/overlapping/into-table part offsets;
unknown parts; caller trailing bytes; nondefault offset gap/nonmultiple bitcode
size; unaligned input; profile stage/model rejection; output clearing and complete
input immutability. Compile original actual helper with ASan/UBSan. Fixture use
must check the exact2824-byte source hash and record extraction, not copy an
upstream shader or silently accept a different artifact. DDI payload form remains
unknown and this standalone helper cannot establish runtime acceptance.

## Implemented standalone gate

Implemented only the new header/C helper and original C/Python tests. The
ordered/contiguous rule comes directly from the pinned Microsoft's
`lib/DxilContainer/DxilContainer.cpp::IsValidDxilContainer`: its offset-table
iteration checks each part begins at the running end of the preceding region,
and checks final running end equals declared container size. This parser is an
independently authored bytewise implementation of that observable contract, not
copied control-flow/source code. There is no requirement for4-byte aligned parts,
zero minor version, or unpadded caller buffers.

`tests/fixtures/dxil/exp1389_cs60.{hlsl,dxil}` preserve the project's original
source/output bytes unchanged. `exp1389-provenance.json` records the exact hashes,
source/tool origin and compiler flags; DXC itself and translator sources are not
copied. The portable test therefore has no ignored-file dependency or silent skip.

```
python3 -m unittest discover -s tests -p test_dxil_program.py -v
AGX_DXIL_INPUT_PASS cases=4214 original_container=2824 program=1276 bitcode=1252 zero_failures=YES immutable=YES
```

The C test compiles the actual helper with C11, Wall/Wextra/Werror and ASan/UBSan.
It checks every truncation, part offset/size perturbations, duplicate DXIL parts,
count overflow, gaps/overlaps/header-table references, program self-size and
bitcode offset/length cross-products including UINT32_MAX, input-kind separation,
profile errors and unsupported stages/models, trailing input, nonzero container
minor and DXIL versions, unaligned input, complete input immutability and output
canaries/zero bytes on failure. Malformed framing is rejected before stage-mask
shifting; a high stage cannot trigger undefined shift behavior. Returned results
are numeric only and contain no allocation or borrowed pointer.

As usual for a parser with separate const input and mutable result arguments,
output must not overlap input/profile. This is explicit in the API contract;
clearing aliased output and preserving the same input bytes are incompatible
requirements. No such aliasing is required by any proposed caller. The helper
does not dereference the returned bitcode or prove LLVM/DXIL semantic validity.
Reviewed C API bitcode parsing remains a separate later gate. No production build
script, DDI negotiation, capability, translator link or hardware state changed.
