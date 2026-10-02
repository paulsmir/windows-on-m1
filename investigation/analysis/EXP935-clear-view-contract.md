# EXP935: clear an RTV independently of output-merger bindings

WHY THIS HYPOTHESIS:
1. EXP934 windowed SDK ce544e78 creates the hardware device, BGRA2560x1600 flip chain and RTV successfully, then GetDeviceRemovedReason returns887a0020 immediately after Clear/Flush, before Present.
2. Exact PID1340 UMD trace records ClearRenderTargetView line302 E_NOTIMPL with zero color bindings; source contains a guard requiring this view to be the sole bound target.
3. Microsoft specifies clearing the supplied RTV's full extent. Neither prior OM binding nor the absence of another bound depth/color target is a precondition. The runtime treats our E_NOTIMPL as a critical driver error.

WINDOWS CONTRACT:
RENDER_ONLY: ID3D11DeviceContext::ClearRenderTargetView and PFND3D10DDI_CLEARRENDERTARGETVIEW. Clear the supplied valid view independently of viewport/scissor and current output-merger bindings; retain those bindings for later draws. Existing admitted format/subresource restrictions remain; no new capabilities are advertised.
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-clearrendertargetview
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d10umddi/nc-d3d10umddi-pfnd3d10ddi_clearrendertargetview

AGX/ASAHI CONTRACT:
Read pinned Mesa agx_pipe.c::agx_clear and agx_state.c::agx_set_framebuffer_state: clear records the selected framebuffer batch key; set_framebuffer_state retains framebuffer references, clears the current batch pointer and dirties state. A batch retains its framebuffer independently. The native driver supplies clear but no clear_render_target callback. Existing Windows frontend fallback wrongly equates a view clear with an already-bound framebuffer clear.
The correction belongs to this frontend translation, not m1n1/Mu/DCP. Current m1n1 display.c/DCP owner, MuR143 memory/ACPI and934 launch contract remain byte-identical. Linux AGX queue/power/DMA/interrupt initialization and recovery do not change. Observed live934 remainsCPU8/SSH/Code0 with actual GPU submissions; no new hardware register or route is needed for this deterministic frontend defect.

TRANSLATION:
Retain the direct clear_render_target branch where provided. For the native clear fallback, validate the existing supported view descriptor, build one temporary framebuffer containing that view and its full dimensions, bind it, issue color0 clear with no scissor, then restore the frontend's existing framebuffer. Do not alter Device::fb, other color buffers, depth/stencil, shaders, capabilities, source address, queue fences or package admission.
ATOMIC CONTRACT: temporary bind -> actual clear -> restore is indivisible; omitting restore changes later application draws, omitting bind clears the wrong target. Basis is the view-specific Microsoft API plus native Asahi framebuffer-key semantics, not an arbitrary group of DDIs.

WHAT IS STILL UNKNOWN:
Whether repairing this deterministic defect is enough to advance the windowed probe through GPU completion and Present, and whether desktop composition also reaches a correct physical image. The test does not yet establish this as the sole desktop failure. Implement and verify the known mapping offline first; hardware then checks actual rendering/presentation only.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The actual projected fallback rejects a legal unbound RTV clear. An executable replay must fail on original source, pass after repair, and verify clearing a different view while preserving existing MRT/depth bindings and untouched pixels. Invalid descriptor guards remain tested. No fabricated protocol-only test.

Validation: ASan/UBSan actual projected body RED/GREEN; related frontend/copy/binding tests; build-native source projection and pinned SDK/WDK ARM64 build/sign/hash/PDB gates. Preserve934 evidence, remove exact934 package through normal GPU-visible broker-disabled377/392 recovery, install only the hash-recorded935 package. Immutable hidden385 only emergency. Smallest hardware checkpoint: same ce544e78 SDK --windowed, Clear/Flush no device error, then actual Present/physical evidence. Short budget, no stability wait before a correct picture.
