# D3D12 C40: 10-bit (R10G10B10A2) swapchains

## Why now

MiniEngine (EXP1836) creates an R10G10B10A2_UNORM flip-discard swapchain;
HDR-capable titles commonly do. The D3D12 presentation path (C7b) only
gives BGRA8/BGRA8_SRGB/RGBA8 swapchain buffers a kernel presentation
allocation; DXGI cannot share any other buffer (EXP1370: FinalConstruct
E_INVALIDARG when the buffers have no kernel resource).

## WINDOWS CONTRACT

DXGI creates flip-model buffers through the D3D12 runtime
(CreateHeapAndResource with an hRTResource) and presents them through
pfnPresent (source allocation + kernel context); DWM opens the shared
kernel resource through its own D3D11 device (the admission UMD's
OpenResource, which takes the DXGI format from our resource private data).

## AGX/ASAHI CONTRACT

The admission UMD (umd.c AdmissionUmdCreateResource / OpenResource), the
KMD allocation validation and DWM composition store and read 32-bit
A8R8G8B8/A8B8G8R8 only; Asahi imports the allocation as a linear
BGRA8/RGBA8 render target (AgxWin32AsahiImportLinearColor32). Gallium
`blit` converts between colour formats (u_blitter).

## TRANSLATION

- An R10G10B10A2_UNORM swapchain buffer gets an RGBA8 presentation
  allocation exactly like an RGBA8 buffer (unchanged admission/KMD/DWM
  contract) plus its own R10G10B10A2 Asahi texture; the D3D12 resource
  (render target, copies, SRVs) is that texture.
- pfnPresent -> AgxD3d12OwnerPreparePresent blits (nearest, RGBA mask) the
  10-bit texture into the RGBA8 presentation import, then flushes for
  present as before. The displayed image is the 8-bit conversion of the
  10-bit buffer (no HDR output: the colour space stays sRGB).
- Destroy releases both.

## WHAT IS STILL UNKNOWN

Whether DXGI or DWM checks the presentation storage against the swapchain
format beyond our private resource data (measured by C40A and ModelViewer A).

## Checkpoint

Probe C40A: 64x64 R10G10B10A2 flip-discard swapchain, back buffer cleared
green, read back (0xC00FFC00), presented, fence after present completes.
Swapchains cannot be created in the builder's non-interactive session, so
C40A is hardware-only; it is C7A with a format parameter and the readback.
