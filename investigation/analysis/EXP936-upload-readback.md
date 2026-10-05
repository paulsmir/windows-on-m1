# EXP936 CPU upload discriminator

WHY THIS HYPOTHESIS: Local clear/copy/readback has exactly the same corrupted bands as cross-process readback, excluding shared open as necessary cause. CPU upload replaces native GPU clear/store while preserving downstream blit/map.

WINDOWS CONTRACT: RENDER_ONLY. UpdateSubresource with complete tightly packed BGRA green bytes on DEFAULT texture; input pointer lifetime ends after return. Same event completion and CopyResource/Map as local control. Microsoft UpdateSubresource, CopyResource, Map documentation.
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-updatesubresource

AGX/ASAHI CONTRACT: build-native-asahi-state.py ResourceUpdateSubResourceUP flushes, texture_map WRITE, util_copy_rect using transfer stride, unmaps. Existing shared import is linear. Asahi agx_transfer_map linear branch returns exact BO+pixel offset; staging readback remains native GPU blit. Existing936 Mu/m1n1 measured contract unchanged; no new device or initialization.

TRANSLATION: diagnostic --local-upload replaces only ClearRenderTargetView with full CPU data upload, retaining creation and event/readback. Zero bad pixels points to clear/store; same bad bands rules out clear as necessary and points to common copy/map/memory path. Not a display success claim.

WHAT IS STILL UNKNOWN: Actual pixel corruption boundary between source write and destination copy. Bounded25s diagnostic, original936 immutable boot/hash gates, recovery377/392 preserved. No driver or firmware modification.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Hardware corruption discriminator, no artificial unit RED. PinnedSDK W4WX PREfast plus hash/PE verification before run.
