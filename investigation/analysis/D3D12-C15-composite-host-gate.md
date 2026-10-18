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

## Observed native argument gate

The exact22 tests also passed on the pinned Windows builder Python08a64dc7,
in0.086s with return0. The wrapper independently observed os.name nt and the
actual Windows_CommandLineToArgvW backend on a fixed lexical sample before the
tests. The new result directory is .local/c15-eh-native1421-result; final21-file
manifest8b0129a66a796d149e9b082bd1d9f1f3dbcd66ae71bef58f08432169c0a5a410,
native receipt270e275adadcb933a3644882421591c18e06bca70465f62a4662233a4e8ff6ef,
lexical receipt4c4416da831e6e0f75a81296de8eca53f5aa2a7603d76dbf13f2ebcd74a444c1.
This closes the selected parser API branch gate. The composite Windows build
and its actual83 compile commands remain a separate unexecuted checkpoint.

## Observed Windows ARM64 build

EXP1421 WINDOWS_COMPOSITE_RETRY1 passed all25 recorded commands. Fresh83-unit
archive compilation took66.96s and produced nine new libraries; separate project
units and facade compiled and both ARM64 fixture executables linked. Root read
all83 effective policy models: native CommandLineToArgvW, EHs on, EHc/EHa off,
release /MD. The separate facade has the same effective exception/runtime policy.
All3798 collected file hashes matched final manifest
703dd966cdb93758956ac38b836efa74d41e9ee240664b0f25ae5fafc279f7d3
in .local/c15-composite-windows-1421-retry1-result. The artifact manifest records
531 outputs and source postchecks record2709 inputs; distribution23files includes
required notices. Actual compiler dependency closure, CRT directives and ARM64
PE/import receipts are retained.

The first configure attempt failed because raw RC path backslashes were parsed
as CMake escapes. Its complete2773-file evidence remains preserved. Retry1 changes
only RC/MT configure path spelling to forward slashes for the identical pinned
tools, under freeze91b52456d500be58b505b1543db3c52686c42979480944b50ecdf71cd75143a8.
No upstream source, exception flags, archive graph or policy changed in this repair.

Neither ARM64 executable was run. Successful native compilation/linking closes the
selected Windows build gate and does not establish a Windows converter job, an
allocation failure sweep, production linkage or R1/SM6 admission.

## EXP1425 observed native ARM64 jobs and semantic review

After EXP1424 RECOVERY_DONE, the two freshly linked ARM64 callers executed on the
normal GPU-visible inert guest. Each returned observed rc0 and its original PASS
marker without timeout. Refusal followed by a valid same-thread job and output
after TLS exit passed for the two original fixtures. All28 input hashes remained
unchanged; the mixed binding map matched all five keys/slots. The surrounding
read-only guard retained Code28/no service/8CPU, the same boot and unchanged
recovery durability manifest. No driver, package, GPU or capability operation
was part of this CPU-only checkpoint.

The original controller returned1/REVIEW_REQUIRED because native mixed SPIR-V
differed from the expected host SHA. Its result is preserved. Native2152-byte
SHA1a5bef149143573bf7fcd8ca2d07772e054f8b61f6958363f8f3ce22e408c07f
differs from host24e3df in four words: OpTypeBool result41->40, OpULessThan
type41->40/result40->41, and OpBranchConditional condition40->41. Primary grammar
comparison found these are all uses of the two IDs. Native bytes also equal the
previous EXP1406 native input. This supports consistent ID renaming; semantic
validation below is separately observed rather than inferred from byte shape.

Under new BEFORE3f9d2131, exactly three existing pinned tools ran once with all40
pins unchanged: spirv-val Vulkan1.1, structural actual-NIR and independent semantic
actual-NIR. All returned0. Structural checks retained CBV0/SRV0,1/UAV0,1 and no raw
alias. Semantic384 words matched independent EXP1397 vectors over192 lanes
(173 active,19 tail),346 stores. Deliberate srv_swap/seed_loss/wrong_slot controls
each produced346 bad words; unsupported/bounds cases refused. The post-NIR files
matched SHA3129a611189d61e21248ad7ea790a0ab4eac8fc07b1b10d02b8b9b3b174ed67e.
Replay artifact manifest4a2c98eb43daf555388a06402d6da04816677c00e2b0da839e281ae2c16a7865
is in .local/c15-native-replay-1425-run.

Verdict: native ordinary CAPI/CRT/TLS jobs and the specific emitted mixed shader
semantics are proven. The strict host-byte oracle failure remains an original
observation, resolved by this appended semantic review. Internal OOM/fault sweep,
arbitrary shader behavior, production driver linkage and R1/SM6 caps remain unproven.
