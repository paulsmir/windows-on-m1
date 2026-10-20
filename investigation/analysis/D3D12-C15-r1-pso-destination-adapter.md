# C15 R1 pipeline-state destination-handle adapter — CPU-only gate

WINDOWS CONTRACT: pinned WDK26100 `d3d12umddi.h` SHA256
`d338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28`
places `hDrvPipelineState` inside the 0001 create record, but 0010 removes it,
adds `D3D12DDI_LIBRARY_REFERENCE_0010`, and passes the destination handle as
a separate third callback parameter. The [official 0010 create DDI](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_create_pipeline_state_0010)
shows that separate handle. Treating 0010 as 0001 would read a shader handle
as the destination and corrupt private storage. Calc and create have distinct
typed PFNs, and a nonzero library handle must pass the same-device registry
check; PipelineIndex is not a pointer.

AGX/ASAHI CONTRACT: current 0003 `CreatePipelineState` copies old shader
tokens and blend/raster/depth/element semantics into private PSO storage,
publishes live Device/PSO identity, then lazily compiles through the existing
AGX owner when first used. This CPU adapter changes none of Mesa Asahi,
Gallium, m1n1, Mu, UAT, power, queues, interrupts or recovery. The accepted
zero-cache library retains no native PSO payload; the exact inspected runtime
forwards full ordinary descriptions on valid load misses. This does not
establish all future library reference/elision or lifetime cases.

TRANSLATION: add an unregistered fieldwise 0010-to-0001 projection. For calc,
set old destination handle to NULL and preserve every other semantic field;
validate the live same-device LibraryReference first. For create, place the
separate third-argument destination into the old record, validate its private
pointer and library reference, then call the existing 0001 CreatePipelineState.
Copy all eight RTV formats and SampleDesc fields; never memcpy/cast the larger
record, read the library index as memory, or publish a partial PSO on a rejected
reference. Exact WDK PFN static assertions guard signatures. Keep
CORE_0003/GetVersions/GetCaps unchanged.

WHAT IS STILL UNKNOWN: complete R1 shader/root/state/library lifetime,
typed CORE_0010 table selection and DXIL converter OOM policy remain open.
The adapter is unregistered, so no J313 experiment can distinguish behavior.
The deterministic gate is an actual-source host fixture verifying destination
move, all semantic fields, ordinary/live/foreign library handling and output
canaries, followed by pinned Windows ARM64 WDK syntax and source-bound full
UMD link. Neither gate licenses R1 or SM6 capability publication.

The actual DDI now contains an unregistered fieldwise 0010-to-0001 PSO
projector and typed calc/create wrappers. An actual-source renderer initially
rejected their absence; Apple clang21 ASan/UBSan then returned
`R1_PSO_DEST_PASS checks=17 failures=0`. It distinguishes all eleven object
handles, eight RTV formats, SampleDesc/NodeMask, ordinary and valid-library
creates, foreign/dead references, NULL input/destination and destination
canaries. A fixture-only unused-variable compile error preceded final GREEN.
EXP1643 staged exact DDI/header/script hashes and pinned WDK26100 ARM64
native-owner/NONE/winsys syntax returned rc0 for all three. Raw builder log
SHA256 `ddac7d6b56ae5ad6b0982f2191bba1037db051b79879fc66b6a51330c42730e1`.
Full production link and R1 table/lifetime admission remain separate.
