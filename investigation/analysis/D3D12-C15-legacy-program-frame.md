# Explicit-span legacy PROGRAM framing before the R1 bridge

WINDOWS CONTRACT: pinned SDK26100 d3d12TokenizedProgramFormat.hpp55..120 and current
project dxbc_parse.c540..562 describe SHDR/SHEX PROGRAM: version DWORD, total unsigned
word count including both header words. Actual macros decode major bits7:4/minor3:0,
in agreement with twelve original FXC outputs. The byte-fields comment is not used
to invent a different decoder. Legacy PROGRAM has no magic field. This internal
CPU module does not advertise a DDI, model or capability.

AGX/ASAHI CONTRACT: eventual shaders still require the existing DXBC-to-NIR/Asahi
compiler path. This helper frames existing tokenized shader input before owned
storage; it neither replaces instruction validation nor compiles GPU code. It
uses no native screen, BO, DMA, interrupt or hardware initialization path.

TRANSLATION: caller supplies a stable readable explicit span and nonaliased output/
profile. The original C helper reads exactly two little-endian DWORDs, supports
unaligned data, checks word-count minimum and span bounds, then publishes metadata.
Accepted framing excludes caller tail. Invalid/unsupported output bytes are zero;
no pointer is retained. Explicit caller masks cover stages0..5/models4.0/4.1/5.0/5.1.
A two-word body can be valid framing without being a valid empty shader. Outer DXBC
is a different boundary; DXIL/raw bitcode/SM6 are not accepted here. Bits15:8 remain
uninterpreted as actual macros/current parser do. No arbitrary unsized-pointer
readability, root-provider lifetime or VOID-DDI error policy is supplied.

The checked product follows directly from N <= floor(B/4): 4*N <= B <= SIZE_MAX.
The span check occurs before the only multiplication. The initial redundant
uint32_t-to-SIZE_MAX comparison produced a host64 warning; its passing raw gate
is preserved. A separate refinement removed that comparison and added this proof.
No 32-bit execution is claimed, and no overflow protection is lost.

WHAT REAL BUG OR INVARIANT WILL THE TEST CATCH? Header overread before8bytes,
unaligned typed reads, signed/truncated counts, multiplication overflow, container
confusion, stage-shift overflow, partial output publication, and incorrect actual
FXC metadata decoding. Test setup uses the CURRENT full parser as an allocating
positive comparator; the production framer itself has no allocator calls. Guard
page ends immediately after the8-byte header, proving no body scan in this domain.

## Observed verification

Original host gate7c509a49: 4argv0,3664checks,12FXC positives, one compile warning
preserved. Refined gate146f80da: same4argv, allrc0/allstderrEMPTY,3664checks/12FXC
actual-parser comparisons, all61pins stable. Source C5f57cd63/header75bb708a.
Undefined-symbol gate finds no direct C allocator references; sanitizer-runtime
allocation is not claimed absent.

Native gate54fb6a05: actual C /TC/c11/W4/WX and C++ header-layout assertions /TP/
c++17/W4/WX bothrc0/stderrEMPTY, AA64. C object8b7ea2734b49a9702e4500c7ed947328133a27c4cc0ff954b0ff1d003b3d88cd;
ABI objecta309a42d1988e5ba965c88cb28044927103f4632a4208a3a72f6ed5c609be81a.
All4901postpins and15downloaded artifacts matched. Native manifest
 d084857077afd8df6512f037314ba2368dec93c3de08c159f03935e87ddaf9b1.
This is internal C/C++ header ABI compilation, not a WDK shader callback assertion
or native execution.

After six-file adoption, CURRENT dynamic-ROOT durable test under0b5a53ae passed
once in0.443s: fourderivedargvrc0/allstderrEMPTY,3664checks/12FXC, post36inputs
unchanged. It has no archived-candidate fallback and default discovery skips before
tool/compiler work. Manifest3e685cb71e0a55d4384b4749e23872f632737a00fae280f1f4ec58a96ffd576b.
Full1593tests213.192s: exact119baseline failures/errors, no additions/removals,
11skips. FullSHA3a4624e275fb84e960e0f2bf4a032887f5ae7ce2504c951cdbed2bac5f56558c.
The whole suite is not GREEN.

WHAT IS STILL UNKNOWN: integration with tagged owned shader storage for ordinary
legacy and DXIL inputs, root compatibility provider, same-device membership and
read leases, component lifetime, legal VOID-DDI error routing, PSO/state/library/
table companions, registration and complete advertised model behavior. This new
module is unlinked and changes no current shader constructor, parser, project link,
DDI table, caps or GPU behavior. Hardware1430/1431 immutable sources exclude it.
The next checkpoint is deterministic CPU tagged-storage continuity, not hardware.
Any later hardware admission requires its own documented coherent contract,
BEFORE/build/sign/hash gates and exact visible package rollback.
