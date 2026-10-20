# C15 R1 typed element-layout adapter — CPU-only gate

WINDOWS CONTRACT: the pinned WDK26100 `d3d12umddi.h` (SHA256
`d338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28`)
defines `D3D12DDIARG_CREATEELEMENTLAYOUT_0010` with the old element pointer
and count plus `D3D12DDI_LIBRARY_REFERENCE_0010`. The calc/create PFNs take
that typed record; the [official create DDI](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_createelementlayout_0010)
confirms the argument shape. A nonzero library handle must be checked against
the live same-device zero-cache registry; PipelineIndex is runtime metadata,
not a private pointer or count.

AGX/ASAHI CONTRACT: current 0003 `CreateElementLayout` copies count and
input-element records into runtime-private CPU storage. The PSO later copies
up to its supported16 elements, and the native owner rejects a graphics
description with more than `AGX_D3D12_MAX_ELEMENTS`. Mesa Asahi assigns
`create_vertex_elements_state`, `bind_vertex_elements_state` and
`delete_vertex_elements_state` in `agx_state.c`. The typed wrapper does not
touch AGX, Gallium, m1n1, Mu, UAT, power, interrupts or recovery.

TRANSLATION: add unregistered 0010 calc/create wrappers in the DDI. Reject
NULL typed records, a positive count with NULL `pVertexElements`, unknown/dead
device or foreign/dead library before allocating/copying. Construct an old
`D3D12DDIARG_CREATEELEMENTLAYOUT` from the two semantic fields and call the
current calc/create functions. Do not copy LibraryReference into element
storage or reinterpret the larger 0010 record as the old struct. Invalid
VOID create reports through the existing per-device error callback without
writing its destination. Keep CORE_0003/GetVersions/GetCaps unchanged.

WHAT IS STILL UNKNOWN: the full CORE_0010 table, other state/shader/PSO
callbacks, child/library lifetime and DXIL OOM remain separate. No GPU
experiment distinguishes anything in this CPU projection. The checkpoint is
an actual-source host fixture with empty and eight distinct elements,
ordinary/live-library equivalence, NULL-array-with-count refusal,
foreign/dead-reference refusal, output canaries and WDK PFN type checks;
then pinned Windows ARM64 syntax/full-link gates before table publication.

The actual DDI now has unregistered typed 0010 calc/create wrappers. The
actual-source renderer initially rejected their absence; Apple clang21
ASan/UBSan then returned `R1_ELEMENT_WRAPPERS checks=18 failures=0` after a
fixture-only validator-count expectation correction. It tests eight distinct
elements, empty layout, ordinary/live-library equivalence, foreign library,
dead device, NULL array with positive count, NULL record/destination and
destination canaries. EXP1629 staged exact DDI/header/script hashes and the
pinned WDK26100 Windows ARM64 native-owner/NONE/winsys syntax gates all
returned rc=0. Raw builder log SHA256
`ddac7d6b56ae5ad6b0982f2191bba1037db051b79879fc66b6a51330c42730e1`.
This is CPU/native syntax only; full production link and CORE_0010 publication
remain separate.
