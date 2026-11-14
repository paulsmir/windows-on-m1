# C15 SM6.0: minimum UMD DDI is not proved by the suffix

2026-10-18, one bounded read-only primary-source pass. No implementation,
cap/version change, compiler/build, guest, hardware, ledger or commit.

## Decision

**No inspected official source states that advertising SM6.0 requires negotiation
of R1/0011 rather than the current0003 UMD family.** The0011 declarations identify
where shader-model enumeration was introduced; their suffix alone is not a
normative rule forbidding that query under every earlier negotiation. Conversely,
no inspected source positively authorizes the26100 runtime to query it or pass
SM6 programs to a0003 driver. Keep those two absence findings separate.

There IS a positive narrower fact: the0003 shader pointer/length ABI can represent
and own the actual EXP1397 DXIL program body without a new argument layout.
Therefore neither CONST UINT* nor the two-word program header proves DXBC-only.
A pipeline-library/R1 migration is not justified solely by enum age or a supposed
inability of0003 to carry the bytes. This qualifies the earlier DDImodel note's
"actionable smallest path" recommendation:0011 remains one coherent candidate,
not a demonstrated minimum dependency of accepting/advertising SM6.0.

## Primary inputs and what each actually proves

- Pinned WDK26100 `d3d12umddi.h`, SHA256
  `d338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28`:
  lines2209–2225 give ordinary and compute0003 shader callbacks with
  `_In_reads_(pShaderCode[1]) CONST UINT*`. The declaration supplies a DWORD-count
  convention, with no DXBC-only marker or explicit maximum shader-model rule.
- Same header: GetCaps input2611–2617 contains Type/pInfo/pData/DataSize, no
  negotiated-interface field. Shader-model query1012 and
  D3D12DDI_D3D12_SHADER_MODELS_DATA_0011 supply a count pointer and optional model
  array sized through that pointer. Release6.0 is0x00060005. Neither this struct
  nor the adjacent version definitions says exactly when older drivers are
  eligible for the query. The declaration is not a complete runtime query policy.
- Official [D3D12DDICAPS_TYPE documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/ne-d3d12umddi-d3d12ddicaps_type)
  identifies1012 as shader models; it supplies no0003/0011 eligibility condition
  for that constant. Its detailed model-data page was unavailable via endpoint;
  actual ABI fields were read in the pinned header.
- Microsoft's [Shader Model6.0 wiki](https://github.com/microsoft/DirectXShaderCompiler/wiki/Shader-Model-6.0),
  edited2017-11-21/retrieved2026-10-18, describes DXC/SM6 and optional wave/Int64
  additions, but supplies no minimum D3D12 UMD interface encoding.
- Microsoft's [DXIL.rst at pin194bb75bea2becb42810d72a16195089c1908e10](https://github.com/microsoft/DirectXShaderCompiler/blob/194bb75bea2becb42810d72a16195089c1908e10/docs/DXIL.rst),
  recorded SHA256 `c79fe2a8917c16da8141d960615b9e32c60c9678a123ba3c9819e41aab03b162`,
  describes ProgramVersion and SizeInUint32 including its header. The container
  section is itself labelled additional/non-normative notes; its format is also
  supported by the pinned [DxilContainer.h implementation](https://github.com/microsoft/DirectXShaderCompiler/blob/194bb75bea2becb42810d72a16195089c1908e10/include/dxc/DxilContainer/DxilContainer.h),
  SHA256 `f9975fb71c62f1aa37cba989adc83f7dcbb06d48e36a62a0b2ebf29c26eaa397`.
  Neither source specifies a minimum UMD DDI family. Format representation must
  not be promoted into a runtime eligibility guarantee.
- Official [WDDM2.1 features](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/wddm-2-1-features)
  associates DXIL's introduction with that Windows graphics generation, and
  distinguishes feature checks from the KMD WDDMVersion value. It does not map
  that statement to a required D3D12 UMD0003/0011 interface. Existing AppleAgx KMD
  WDDM3.x admission cannot by itself answer the UMD negotiation question.

The official-source search was restricted to this exact model/format/query/version
question. No pipeline-library absence search or old-experiment archaeology was
repeated. Stop after this pass; no third-party implementation was treated as a
normative substitute. In particular do not transfer a much later feature's
explicit minimum-DDI rule to SM6.0 without an SM6-specific statement.

## Actual EXP1397 program versus the existing0003 storage contract

Read-only bytes from
`$HOME/public_windows/.local/c15-spaces-1397/builder-output/exp1397_spaces_cs60.program.dxil`:
1680 bytes, SHA256
`1a7860f346cd2c430bba8725824f1e107c5c26c8c0c58b5a48a0185f38e9c179`.
No new compiler/converter run occurred for this comparison.

| DWORD | Observed value | Interpretation |
| ---: | --- | --- |
|0|0x00050060|Stage5 compute, major6/minor0|
|1|0x000001a4|420 DWORDs, exactly1680bytes|
|2|0x4c495844|DXIL magic|
|3|0x00000100|DXIL version1.0|
|4|0x00000010|Bitcode offset16 relative to magic/program+8|
|5|0x00000678|Bitcode1656bytes; starts program+24 and ends at1680|

Current CalcPrivateShaderSize and StoreShader use code[1] as DWORD count, own
that many DWORDs and derive stage from code[0]>>16. For this actual body those
operations can preserve the complete program and stage exactly. The compute PSO
also owns a copy before the original shader is destroyed. This is a structural
compatibility argument from actual functions and input, not a newly executed DDI
test and not a claim that current CompileCompute accepts DXIL: it still routes
the bytes to the DXBC translator. Integrating the owned frontend, encoding/model
discriminator and safe size validation remains real implementation work.

The container starts with DXBC magic and has a different layout. Passing its
3704bytes as the0003 program pointer is not justified: code[1] there is a digest
word, not program length. Preserve the explicit PROGRAM-versus-CONTAINER boundary.
The owned parser can consume the verified program span; the reviewed external raw
API receives only its verified LLVM-bitcode span. A format gate and a model cap
are separate from the callback's binary pointer shape.

## Remaining runtime contract and next causal decision

WHAT IS STILL UNKNOWN: under the exact26100 runtime and negotiated0003, is
GetCaps1012 issued/eligible, how does successful enumeration constrain subsequent
CreateShader callbacks, and is any interface/version test applied before the
DXIL program is handed to the driver? Current lack of an answer is neither a
permission to advertise6.0 nor evidence that0011 is compulsory.

No version/cap bit probe is proposed. The next useful offline discriminator is
an explicit normative model-query version rule or a bounded inspection of the
already-local exact runtime's model-query/dispatch gate, with evidence labelled
as implementation behavior if that is what supplies the answer. Do not start
R1 library infrastructure as an unavoidable SM6 prerequisite on the basis of
header suffixes. Equally, do not change0003 advertisement merely because the
compiler-only1397/1398 path succeeds. First complete the owned frontend's ordinary
shader/PSO contract and establish this remaining eligibility boundary.

## Follow-up: exact local26100 runtime has a proved version gate

This adds implementation evidence to the earlier bounded documentation finding;
it does not reinterpret absence of caps1012 from an old sequential probe.
Only already-local bytes/disassembly were inspected. No Windows module executed,
no symbol/binary fetched, no probe/compiler/cap/hardware run performed.

Binary: scratchpad `d3d12core/d3d12core.dll`, COFF-ARM64X,
FileVersion10.0.26100.9549, SizeOfImage0x5eb000, SHA256
`052241d7d9fdcf32dc2a86491c56e524f80e78990e6a3ddd787f75767bd41a82`.
CodeView GUID `{CA04B74C-704D-76D2-917D-3123557CF688}`, age1;
PE timestamp0xb4cd0413 is recorded as an identity field, not a calendar-date claim.
The preserved EXP1381 probe dump's loaded D3D12Core module has exactly matching
FileVersion, timestamp, image size and CodeView GUID/age. No matched local PDB
was found; the proof uses numeric RVAs, actual instructions and version-table
bytes, not guessed function names from nearest-export disassembler labels.

**For this exact runtime,0003 does NOT reach shader-model enumeration1012;
0011 is the first known negotiated version that passes the gate.**

### 1. Negotiated interface/build maps to a stored ordinal

At RVA0x68b4–0x690c the constructor loads the selected64-bit driver version,
extracts interface from bits32..63 and build from bits16..31, then searches
32-byte records relative to RVA0x474250. It compares the interface at record+4
and the low16-bit build at record+8. A match reaches RVA0x6b00, which retains
the found index and stores its low byte at device+0x130 (RVA0x6b20).

The actual first records, independently decoded from the PE's rdata:

| Record RVA | Ordinal | Interface | Build | Matched WDK token |
| --- | ---: | --- | ---: | --- |
|0x474270|1|0x000c0002|8|SUPPORTED_0003=0x000c000200080000|
|0x474290|2|0x000c000a|0|SUPPORTED_0010=0x000c000a00000000|
|0x4742b0|3|0x000c000a|1|SUPPORTED_0011=0x000c000a00010000|
|0x4742d0|4|0x000c000a|2|SUPPORTED_0012=0x000c000a00020000|

This establishes the ordinal meaning from both compare operands and actual
version constants, rather than assuming that integer3 means revision0011.

### 2. Model initialization explicitly skips the query for ordinals below3

Caller RVA0x87ca0–0x87cb4 passes device+0x128 to the model-init function at
RVA0x1b93c0. Therefore that function's object+8 is the same stored byte at
device+0x130. Its relevant control flow is:

```text
RVA1b93f8: mov  w8,#0x51
RVA1b93fc: str  w8,[x21,#0x104]       ; initial maximum model5.1
RVA1b9418: ldrb w8,[x21,#8]           ; negotiated ordinal
RVA1b941c: cmp  w8,#3
RVA1b9420: b.lo RVA1b97bc            ; direct return, query loop skipped
...
RVA1b94c4: mov  w4,#16               ; model-data struct bytes
RVA1b94c8: add  x3,x26,#16           ; count/list pointer pair
RVA1b94cc: mov  w1,#1012
RVA1b94d4: bl   RVA1b8cf0            ; caps wrapper
```

The loop first supplies count pointer/NULL model list, then allocates/fills the
list for the second call. The wrapper at RVA0x1b8cf0 forwards cap type, NULL
pInfo, output pointer and byte size through its stored caps callback. A second
code copy at RVA0x3b99c4–0x3b9a98 has the same default, ordinal<3 return and
1012 call, so the other visible code copy does not offer an ungated alternative.
For ordinal1, the branch returns before the only later write that derives a
maximum model from the returned enumeration. The retained maximum is0x51.

A4-byte shader-model-shaped query consumer at RVA0x152d8–0x15318 validates
requested values in0x51..0x69 and clamps the caller value against
[device+0x22c]. That address equals model-object+0x104, the field above. This
corroborates that the skipped initialization is not merely unused diagnostics.
No unsupported callback type or public method symbol was invented to name this
consumer without a PDB.

### Consequence and limits

The earlier positive format fact remains true:0003 can carry the self-sized
DXIL program bytes. It is insufficient to expose SM6 on this runtime, whose
version-dependent model initialization is now directly observed in static code.
A caps1012 implementation added while continuing to return only SUPPORTED_0003
would not be called through this initialization path and would retain5.1 here.

For the current26100.9549 target, the smallest known eligible negotiated version
is consequently0011, which entails the typed CORE_0010 companion ABI. This is
an implementation-specific proof, not a universal claim about all Windows
runtimes and not merely a suffix/age inference. It restores a concrete reason
for the coherent R1 candidate while preserving the unresolved library-reference
semantics in the separate positive library design. Do not advertise an ABI whose
companion behavior is not implemented. No hardware bit experiment is needed to
confirm the shown branch.

Evidence excerpts and numeric identity/table receipt are preserved under
`$HOME/public_windows/.local/c15-runtime-sm60-gate/`:
constructor.txt, model-init.txt, caps-call-and-consumer.txt, identity-table.json.
Manifest SHA256:
`19d60ad6d4f1bb7cfa64c16abc601413702a2cbeaf55f10d921de94520fb3299`.
