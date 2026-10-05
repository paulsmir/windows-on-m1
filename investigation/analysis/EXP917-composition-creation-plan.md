# EXP917: distinguish composition creation from later presentation

WHY THIS HYPOTHESIS:
1. EXP916 proves an active primary Apple output at2560x1600, with DWM creating a
   feature-level10.0 device, but no tracked physical Present and a zero current
   DCP surface. Absence of display topology is already excluded.
2. Native source implements D3D10 DDI, and modern FULL GRAPHICS requirements
   expose an implementation gap. Requirements alone do not establish the actual
   DXGI runtime's first rejecting boundary. A full frontend rewrite without that
   distinction could leave the black-screen cause untouched.
3. The Microsoft-defined creation call returns an exact HRESULT before Present;
   it is a smaller discriminator than changing version/capability declarations.

WINDOWS CONTRACT: FULL GRAPHICS public D3D11 device creation on the unique APPL0002
adapter, D3D_DRIVER_TYPE_UNKNOWN, BGRA support, explicit FL10.0; DXGI1.2
CreateSwapChainForComposition with BGRA8 2560x1600, SampleCount1, BufferCount2,
FLIP_SEQUENTIAL, STRETCH, alphaIGNORE. The SDK defines the ABI. Query buffer0 only.
No visual binding, Present, fullscreen/mode or power call.
Sources: Microsoft CreateSwapChainForComposition and D3D11CreateDevice reference;
pinned SDK26100; current native adapter/device/presentation implementation.

AGX/ASAHI CONTRACT: exact EXP916 KMD/UMD/firmware and allocation/UAT/resource
lifetime owners remain unchanged. This stimulus issues no draw or Present.
Asahi/m1n1/Mu initialization and scanout contracts are the hardware-validated
EXP916 reference; this run does not introduce MMIO/IRQ/power/DMA behavior.

TRANSLATION: run a separately hashed ARM64 SDK sidecar once in active console
session1 after the original750s baseline. Match APPL0002 hardware IDs, print LUID
and active output, capture each HRESULT, verify returned backbuffer description,
release COM objects in ownership order. Exact package916 is reinstalled only
after EXP916 was fully removed; package retention is not a variable.

WHAT IS STILL UNKNOWN: does runtime device/flip-chain/backbuffer creation reject
this adapter, or succeed and leave the failure later in DWM/presentation?
A failure localizes a public runtime boundary; it does not by itself prove the
same internal DWM branch. A success rejects the blanket claim that the current
DDI cannot create this composition chain. Either verdict informs the smallest
next owning implementation. This is an observation of an internal Windows
runtime decision, not a capability probe or a new advertised driver model.

One variable versus EXP916: one creation-only client stimulus after baseline.
Dwm-Core tracing, package916 bytes, power/firmware and750s observer remain the same.
No artificial RED test for diagnostic wiring. Native build: W4/WX/analyze,
SDK26100 ARM64 zero warnings/errors; exact source/binary hashes preserved.

Recovery: original ETL/UMD/probe output and receipts independently host size/SHA
verified before ordered restart; immutable EXP377/392 ordinary Code43, separate
recovery evidence host gate, exact package/devnode/modules/signer/diagnostics
cleanup, then ordinary Code28 twice/free>=4GiB/shadows0. Hidden EXP385 emergency
only. Task must be removed after collecting its result; never rerun on timeout.
All artifacts and exact operational commands are in the EXP917 ledger BEFORE.

This refines EXP916's offline prerequisite: do not advertise unimplemented modern
DDIs or run a changed capability candidate. The unchanged-package creation
observation is allowed specifically to resolve the remaining runtime decision
before selecting the scope of that implementation.
