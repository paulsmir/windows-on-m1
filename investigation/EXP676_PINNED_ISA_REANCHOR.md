# EXP676 pinned-ISA and register-allocation re-anchor

Date: 2026-09-09. Scope: the exact 16x16 fragment-colour boundary after
EXP673--EXP676. This is offline evidence, not a hardware verdict.

## Primary implementations and evidence inspected

- Pinned Asahilina Mesa commit
  `7a4f24061fa56ef7eff12132dd7b1461d5a890d8`:
  `agx_compile.c`, `agx_pack.c`, `agx_opcodes.py`,
  `agx_register_allocate.c`, `agx_lower_parallel_copy.c`,
  `agx_print.c`, `agx_compile.h`, and the parallel-copy tests.
- The same pinned compiler's own pre-optimization, post-optimization and
  post-register-allocation printout in
  `.local/analysis/exp676-pinned-ra/pinned-fragment-ir.log`.
- Immutable working EXP659 shader and frame archive, the EXP673 final active
  graph/store receipts, and exact EXP674--EXP676 fragment assets/results.
- Production EXP208 composition and active-job code, including
  `AppleAgxExp208AdoptNativePipelineLayout`, dynamic bind/overlay,
  `AppleAgxRenderSharedMemoryBuildActiveJob`, and the generated WorkCommand3D.
- Current m1n1 G13/V13_5 `WorkCommand3D`/`Start3DStruct*` schema and the native
  EXP659 decoded WorkCommand log.

## Correct pinned-2022 shader semantics

The modern disassembler's multi-operand rendering of this old instruction is
not the contract for the pinned binary. In the pinned source, `st_tile` has one
source. Its packed `D=4` selects the 16-bit base register `r0h`; format 4 is
`AGX_FORMAT_U8NORM`, and mask `0xf` selects `xyzw`. Register allocation assigns
the four-component FP16 value to four consecutive half-registers.

The pinned compiler prints this final sequence for the first render target:

```
mov_imm r0h, 0x3c00
mov_imm r1l, 0x3c00
mov_imm r1h, 0x0000
mov_imm r2l, 0x0000
xor-swap r0h <-> r1l
xor-swap r1l <-> r1h
xor-swap r1h <-> r2l
writeout 0xc200
writeout 0x000c
st_tile r0h, xyzw, U8NORM
```

Thus the loads begin as `{R,A,G,B}` and the compiler-generated parallel copy
produces the packed `{R,G,B,A}` vector at `r0h..r2l`. The pinned compiler's
first 94 emitted bytes have SHA-256
`9c7a134baeb2bac51f39f13c3c4228e543e8bc0f16bf5e5e65f9bfb6230a9fdd`,
byte-exact with the working EXP659 object through the first `st_tile`. The
complete immutable 112-byte EXP659/production fragment shader remains
SHA-256 `fa139e95a66a67b921221a3927ccfca209bc9037fdd31156d74894902ab2dd92`.

This corrects the explanation of EXP675, but not its hardware result: the
direct construction still populated the same consecutive four-register source
and was rejected on hardware. EXP676 retained the compiler-generated
permutation and was also rejected. Neither result may be reinterpreted as an
incorrectly decoded source register.

## Final static execution-state comparison

Parsing the production WorkCommand with the current G13/V13_5 schema, rather
than treating printed version-neutral offsets as raw byte offsets, eliminates
the apparent offset mismatches. The stable native and production values agree:

| Field group | Native EXP659 | Final production |
| --- | --- | --- |
| Tile geometry | 4x4 blocks, one tile, 16x16 aux/depth dimensions | exact |
| Tilebuffer allocation | `tib_blocks=8`, 32x32 shared layout, 8-byte pixel stride | exact |
| Multisample/sample state | `0x88`, one sample | exact |
| Clear/reload/store | `0x22004` / `0x23004` / `0x24004` | exact |
| Pipeline base | `0x1100000000` | exact |
| Fragment USC | shared + shader + registers + fragment properties | byte-exact |
| RT/PBE | tiled BGRA8 16x16 descriptor and companion | exact except destination address |
| Fragment program | working EXP659 112 bytes | byte-exact at the same fixed VA |

Context ID, object addresses, stamps, event numbers and statistics owners are
expected ownership/publication differences. They do not alter the decoded
fragment/tile conversion contract, and the exact physical completion path is
already proven. No source-backed active graph or WorkCommand mismatch remains.

## Correction after EXP677

The register and WorkCommand conclusions above remain valid, but the former
runtime-scalar conclusion did not survive source review. The qualification
path executes `RtlFillMemory(output->Data, output->Size, 0xa5)` immediately
before flushing and publishing the job. EXP674 then selected a shader whose
putative output was exactly `0xa5a5a5a5`. Its72 covered pixels therefore do
not distinguish a real tile write from untouched pre-submit poison. EXP677
subsequently rejected the endpoint hypothesis by producing the same zero
result with values just below1.0.

The first unknown is again the fragment write itself. The smallest valid
discriminator keeps the same all-lanes-equal construction but selects FP16
0x3800. U8NORM converts it to0x80 per component, which is distinct from poison
0xa5, background0xff112233 and zero. A full256-pixel result with72 exact
0x80808080 pixels proves a fragment tile write without depending on channel
ordering. Seventy-two poison pixels mean the target range was left untouched;
seventy-two zero pixels preserve the current failure. No context/queue
hypothesis is justified before that corrected observation.
