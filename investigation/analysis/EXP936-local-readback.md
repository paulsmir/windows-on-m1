# EXP936 same-process readback discriminator

WHY THIS HYPOTHESIS: Two cross-process runs give exactly204800 bad pixels despite successful open/fences. Shared-resource mapping is one remaining difference that can be removed without changing clear, texture geometry, or staging copy.

WINDOWS CONTRACT: RENDER_ONLY. Same BGRA DEFAULT shared texture, RT|SRV, same clear+eventcompletion; CopyResource to identical dimensions/format STAGING and MapREAD in producer instead of child. Microsoft CopyResource and Map documentation inspected 2026-10-03. Map must expose completed data; no mapped resource during copy.
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copyresource
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-map

AGX/ASAHI CONTRACT: Inspected Asahi agx_pipe.c transfer_map/staging path, native linear shared import, build-native-asahi-state.py ResourceCopy and ResourceMap; copy uses native blit/flush, Map FlushRetire. m1n1 render.py tiling formulas agree with g4 scalar arithmetic at this comparison. No Mu/ACPI/power/IRQ/DMA contract changes; original936 hardware manifest and measured boot identity retained.

TRANSLATION: Refactor identical readback body into helper and add --local diagnostic option. Existing cross-process mode unchanged. Same-process clear/copy/map failure excludes cross-process open as a necessary cause; success implicates shared import/visibility and warrants targeted comparison. No new driver or framebuffer layout change. Existing immutable377/392 recovery, outer25s budget, no reboot.

WHAT IS STILL UNKNOWN: Which data path first produces the spatially periodic zeros. Local failure alone does not distinguish render store and staging copy.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Hardware-only pixel corruption; no artificial unit RED. ARM64 pinned SDK W4WX/PREfast and source/hash/PE gates before one bounded run.
