# EXP511 current-ABI UMD render seam

## Boundary and decision

EXP509 and EXP510 proved that successful offscreen and direct-display `PatBlt`
calls are not routed to this adapter's `DxgkDdiRenderKm` on the current Windows
stack. They do not reject the KMD or backend. The next supported producer seam is
the ordinary user-mode command path documented for an OpenGL ICD:

`D3DKMTCreateContext` -> user command/allocation list -> `D3DKMTRender` ->
`DxgkDdiRender` -> Patch -> SubmitCommand.

This does not restore the old TestContext token or its private ABI. The producer
creates a normal context with flags zero and no context-private data. Microsoft
documents the returned command, allocation and patch-list buffers as the buffers
an OpenGL ICD fills, and requires `DxgkDdiRender` to protect every access to
user-mode command/list memory with `__try/__except`, validate it, and translate it
to kernel DMA and patch-list buffers.

## Sources inspected

- Pinned WDK 10.0.26100.0: `d3dkmthk.h`, `d3dukmdt.h`, `d3dkmddi.h`,
  `d3d10umddi.h`.
- Microsoft `D3DKMT_CREATECONTEXT`, `D3DKMT_RENDER`, `PFND3DDDI_RENDERCB`,
  `DXGKDDI_RENDER`, `DXGKARG_RENDER`, and private-data validation contracts.
- Current `callbacks.c`, `gdi_windows.c`, `submission_windows.c`,
  `allocation_windows.c`, `render_gdi.c`, and EXP208 backend-image path.
- Current UMD: it has a narrow DirectFlip/resource path and advertises no 3-D
  pipeline bit; it does not provide a draw or flush command encoder.
- Current hardware evidence: EXP508 completes the Windows CDD CPU-copy fence;
  EXP509/510 never enter KMD render; EXP478 already proves backend runtime ready.

## WINDOWS CONTRACT

- A normal non-system context has one 4-KiB command buffer, allocation list and
  patch-list capacity plus driver-private DMA shadow storage.
- The user command is a fixed, pointer-free, versioned 48-byte color-fill record.
- `DxgkDdiRender` copies the record inside `__try/__except`, accepts no input
  patch list, recreates one output patch location, and never trusts user
  addresses or allocation ownership.
- Patch and Submit accept either a GDI context or a normal render context, but
  never a system context. They preserve the existing exact context/device/
  allocation/fence checks.

## AGX/ASAHI CONTRACT

No AGX-side behavior changes. The existing color-fill translator produces the
same immutable DMA record; the existing EXP208 materializer, TA and 3D queues,
event/stamp/done-pointer completion, interrupt notification and DPC remain the
sole hardware path.

## TRANSLATION

The pointer-free user command is validated against the opened destination
allocation, converted through `AdmissionGdiPrepareColorFill`, recorded in the
existing DMA shadow, patched to the existing local-memory/UAT view, and submitted
through the existing render packet/backend. The EXP509 qualification receipt is
reused; no second backend or completion implementation is created.

## WHAT IS STILL UNKNOWN

Whether current dxgkrnl accepts the normal context/allocation/render request and,
if so, whether the existing backend completes both physical TA and 3D work for
that exact Windows fence.

## ATOMIC CONTRACT

The following changes are indivisible because no one is meaningful alone:

1. pointer-free user command ABI and validator;
2. non-GDI context private DMA-shadow size;
3. `DxgkDdiRender` safe translation and output patch generation;
4. Patch/Submit admission for a normal non-system context;
5. a one-shot D3DKMT producer using the buffers returned by the normal context.

No capability bit, D3D pipeline claim, UMD draw table, platform, firmware,
memory topology, scheduler, IRQ, display or backend behavior changes.

## Offline and hardware gates

RED/GREEN tests cover malformed version/length/opcode/rectangle/allocation/ROP,
user-pointer exception handling, context sizing, system-context denial, project
wiring and the absence of TestContext/private-token use. Then run the existing
render/shared suites, pinned WDK KMD/UMD/producer build, analysis, Universal,
Inf2Cat/signing and exact hashes.

Hardware uses one clean exact candidate and one exact producer invocation. PASS
requires Render, Patch, Submit, backend result zero, both TA/3D event/stamp/done
receipts, exact completion fence, NotifyInterrupt and DPC. API success alone is
not PASS. Evidence is collected before exact experiment cleanup.
