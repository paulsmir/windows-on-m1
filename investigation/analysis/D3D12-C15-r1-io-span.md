# C15 R1 shader IO-signature span preflight

Classification: RENDER_ONLY, CPU-only unregistered shader-input prerequisite.
Primary sources inspected: pinned WDK26100 `d3d12umddi.h` SHA256
`d338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28`
lines 2089–2115 and 3269–3296; current
`agx_d3d12_shader_owned_core.cpp` `Calculate`/`Create` (uses deprecated
Standard signature pointers and explicit trusted spans); existing
`agx_shader_input_provider.h/.cpp`; and official Microsoft
[CreateShader_0010](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_create_shader_0010).
The Mesa/Asahi owner only sees a later compiled shader; m1n1, Mu, KMD,
ACPI, AGX queue and GPU state do not own this CPU pointer arithmetic.

WINDOWS CONTRACT: the 0010 shader descriptor holds a Standard or Tessellation
IO-signature pointer in a union; each Standard signature has a pointer/count
pair for input and output. `_0012` aliases the deprecated pointer in the
same union slot, but the current owned core explicitly uses the deprecated
record layout. `CreateShader_0010` is VOID, so this helper returns only a
boolean and does not report a runtime error or register a DDI.

AGX/ASAHI CONTRACT: the linked owned core copies fieldwise Standard signature
entries only after the caller supplies exact readable `AgxShaderTrustedSpan`
values. The provider snapshots the descriptor and leases caller-owned arrays,
but neither module can prove that an arbitrary DDI pointer is readable.

TRANSLATION: a pure helper takes an actual caller-readable 0010 descriptor,
an explicit maximum entry count policy, and a zeroed result. For Standard
VS/PS/CS paths it rejects count>policy, count with NULL pointer, or checked
`count*sizeof(D3D12DDIARG_SIGNATURE_ENTRY)` overflow. It returns pointer/byte
pairs without dereferencing any entry; NULL Standard means two empty spans.
Tessellation signatures and stage classification stay outside this helper.
No fixed policy is invented here; the future typed adapter must derive one
from its supported pipeline and storage contract.

WHAT IS STILL UNKNOWN: readability and stability of every referenced entry,
which type of union arm applies to hull/domain paths, exact VOID DDI error
category, shader PROGRAM body readability, and converter OOM. This helper is
not evidence for CORE_0010 or SM6. The deterministic checkpoint is a host
ASan/UBSan guard-page fixture: a count beyond policy with a PROT_NONE array
must fail before dereference, NULL/count mismatches must clear output, and
valid distinct input/output arrays must yield exact byte spans. Then compile
against pinned WDK ARM64 native-owner/NONE/winsys. No J313 hardware reason
exists while the helper is uncalled.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? The future shader adapter
must not pass an unbounded or NULL signature array with a positive count to
the owned core, nor produce a wrapped byte length that aliases another input.

Offline result: the host Appleclang21 ASan/UBSan guard-page fixture first
stopped on a missing header, then passed `R1_IO_SPAN_PASS checks=11 failures=0`.
The initial EXP1682 pinned MSVC/WDK compile stopped on an include-order error
(`AgxShaderTrustedSpan` not yet defined); EXP1683 moved the existing provider
include before the new header without changing its logic. Native-owner,
NONE and winsys WDK26100 syntax then returned three `rc=0`; staged and local
DDI SHA256 `32abef72a8e05ae8738d1d48f643b639217d96620158a7e0af14058c8009ceb0`
and helper SHA256
`1af200ff16cf202f5cda422fe4ee0d07d712e799390e07b16f666eda09d1a675`
matched after the run. Focused ABI/native-owner/change-ledger 32 PASS and
`git diff --check` PASS. Raw EXP1683 builder log
`.local/c15-r1-io-span-syntax1683/builder.log` SHA256
`ddac7d6b56ae5ad6b0982f2191bba1037db051b79879fc66b6a51330c42730e1`.
The helper remains uncalled/unregistered: no production PE, J313 or
full signature-body readability claim.
