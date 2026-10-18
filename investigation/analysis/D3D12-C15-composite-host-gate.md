# C15 composite converter: observed host build boundary

EXP1421 HOST_RETRY3 completed on 2026-10-18 at19:06:53Z. This is the
external dxil-spirv candidate at upstream8434b0639fb2a48ce13aac08a420ecce5d23df0e
plus reviewed original ownership patch
df3470cac9939a1acf0f0f11982970971d409476fe1a3141ccd8b7cae17bb863.
The license review and selected runtime closure remain the conditions in
D3D12-C15-dxil-license-review.md. External implementation stays outside Git.

The frozen candidate manifest is
a6d657e9e0ad09d4992d20c302faef097041c995b2e232cb03630022a448c325.
The fresh run .local/c15-composite-host-1421-retry3 built all83 selected C++
translation units into nine archives, then the original checked19-wrapper C
facade and four project C units. All11 configure/build/compile/link/run commands
returned0. Both original fixture callers passed under ASan/UBSan with no
sanitizer diagnostics. They exercise one-key and five-key mixed binding maps,
same-thread refusal followed by a valid job, and output lifetime after TLS exit.
They do not establish an internal OOM sweep or arbitrary shader coverage.

Root independently verified all408 artifact entries and23 distribution files
including attribution notices. Artifact manifest SHA256
0464799f955896302f2da304686aa206dd3ff27f5cd7e68640c9f3d9d9d789e6;
distribution manifest
4d72531ec6f10d2710a1a6cc3199993fd81cbed7370b480c9fec09cf4dc9675a;
outer receipt .local/c15-composite-host-1421-retry3-receipt.json
9bf83747b8618680bf5e072646a06dcfaeb2da84d429c5871c840f4697180f4b.
The2152-byte mixed SPIR-V output
24e3df26411f940c71e4dc2322d38226cb3c1d50725e07d27b890aa9b2b307d4
is byte-identical to the previously validated EXP1398 host input. This is a
comparison with existing evidence, not a claim of another validation execution.

Earlier stopped attempts remain in EXPERIMENTS.md: object output path accounting,
two explicit tool dependency pins, and missing host link SDK argument. Their
compile or bookkeeping failures are not functional RED results. No old archive
was substituted into the final fresh graph.

## Effective Windows exception policy

All83 upstream C++ units and the separate facade must have effective EHs enabled,
EHc disabled and EHa disabled, with release /MD. The documented /EHsc- compound
option enables s and disables c. A substring search cannot prove this policy
when later per-letter overrides, response files or CL/_CL_ are present.

An original external strict argument parser and22 deterministic tests are frozen
in .local/c15-msvc-eh-parser-prep/before-inputs.json
4b3687471b15b23be2fb4bd115ecb0a467b5791b34dd3f23008af34cd1b33df2.
All22 host tests passed in0.007s, return0; receipt
.local/c15-msvc-eh-parser-host-1421/receipt.json
db986dc7415b33a806177916dbeea389a2dd2dd384f8ece6480a64564b0c69ae.
They catch incorrect effective EH/CRT state, token payload false positives,
actual child environment injection, response-file encoding/nesting/drift and
contradictory command/arguments. Host execution covers the portable lexer; the
native CommandLineToArgvW branch remains unobserved and needs Windows tests.
The proposal must be rebased onto runner46b08e39 preserving the accepted host
SDK link fix, then independently frozen before Windows composite compilation.

No production driver linkage, R1/SM6 capability advertisement, Windows composite
execution or hardware behavior is established by these host results.
