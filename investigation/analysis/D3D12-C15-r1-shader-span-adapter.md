# C15 R1 shader PROGRAM span acquisition — CPU-only gate

WINDOWS CONTRACT: pinned WDK26100 `d3d12umddi.h` SHA256
`d338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28`
defines `D3D12DDIARG_CREATE_SHADER_0010` with `pShaderCode` but no explicit
byte length. The [official shader create DDI](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_create_shader_0010)
uses that typed record. The exact inspected D3D12Core26100.9549 successful
early-R1 path supplies a DXIL PROGRAM payload or legacy SHEX/SHDR body, not an
outer DXBC container. Before forwarding a DXIL payload it checks at least
eight bytes and `payload[1] <= payload_bytes/4`; this is local runtime
implementation evidence, not a general arbitrary-pointer safety guarantee.

AGX/ASAHI CONTRACT: the original owned shader core and bounded
`agx_dxil_program`/legacy framing helpers accept an explicit trusted readable
span. They do not infer accessible memory from a naked DDI pointer. This new
helper is CPU-only; AGX, Mesa Asahi, m1n1, Mu, UAT, interrupts, power and
recovery do not consume it. The reviewed dxil-spirv converter still has an
unresolved recoverable internal OOM contract and is not admitted here.

TRANSLATION: given a DDI pointer whose first eight bytes are known readable by
the caller and an explicit CPU policy `max_bytes`, read only the second UINT32
with `memcpy`. Reject NULL, fewer than two words, a limit below eight bytes or
`words > max_bytes/4`; clear output on failure. On success return the original
pointer and checked `SIZE_T(words)*4` byte count without touching any body
byte. Pass that span to an explicit-length parser only when the caller's
runtime/lifetime contract establishes those bytes are actually readable.
Keep the policy limit a caller argument rather than inventing a capability or
hardware constant. Do not classify DXIL versus legacy or publish CORE_0010.

WHAT IS STILL UNKNOWN: whether all body bytes are readable in every runtime
error/library path; root-signature and IO-signature canonical ownership;
provider registration/teardown and shared-context concurrency; converter
internal OOM; R1 table and SM6 execution. A successful span calculation does
not close these. The smallest deterministic CPU test places only eight bytes
at a page boundary with the next page inaccessible: an invalid huge length
must be rejected without touching body, and a valid two-word span must not
read past those words. Host sanitizers and pinned Windows ARM64 syntax check
the helper; no J313 experiment distinguishes its behavior while unregistered.

The original CPU helper `agx_d3d12_r1_shader_span.h` implements only this
first-eight-byte calculation and is included, but uncalled, by the current
DDI. The fixture first failed on its absence; Apple clang21 ASan/UBSan then
returned `R1_SHADER_SPAN_PASS checks=9 failures=0` with the next page
`PROT_NONE`. EXP1651 staged exact DDI/header/script hashes and the pinned
WDK26100 ARM64 native-owner/NONE/winsys syntax gates each returned rc=0.
Raw builder log SHA256
`ddac7d6b56ae5ad6b0982f2191bba1037db051b79879fc66b6a51330c42730e1`.
No production shader callback or PE/runtime/GPU result follows from this gate.
