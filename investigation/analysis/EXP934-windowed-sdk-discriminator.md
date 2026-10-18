# EXP934 windowed SDK discriminator

WHY THIS HYPOTHESIS:
- Both original and late fullscreen SDK runs stopped in DXGI proxy-window/ALPC creation before a completed CreateSwapChain result; no Clear/Present was reached.
- Paired late DWM was in a real paging-fence wait during window-border rendering; an idle DWM snapshot before the launch excluded a permanent allocation stall.
- Console-session enumeration now proves one display source. VSync delivery is independently observed in ETW. Repeating either basic check cannot distinguish the remaining presentation boundary.

WINDOWS CONTRACT:
FULL GRAPHICS application workload. Microsoft IDXGIFactory2::CreateSwapChainForHwnd documents a null pFullscreenDesc for a windowed swap chain. Keep BGRA8, 2560x1600, two-buffer flip-sequential, feature level10_0 and exact hardware adapter. Add --windowed to the existing SDK probe; default fullscreen remains unchanged. Query actual fullscreen state; do not require a fullscreen output or perform fullscreen restoration in windowed mode. Clear and Present stay identical.
https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforhwnd

AGX/ASAHI CONTRACT:
No AGX or DCP change. Existing934 package2602f23d, m1n1bdcf8715 and MuR143e54 remain the original run. Inspected current winsys agx_win32_gpuva.c and umd_gpuva_windows.c (residency wait before submission), m1n1 display.c and DCP surface/latch path. Current Asahi plane.c derives scanout IOVA from GEM DMA address plus framebuffer offset; no behavior is copied. Broker retains DART/power/interrupt/recovery ownership, Windows retains allocation/render/present ownership. Mu exposes unchanged memory and ACPI. No register or route is inferred.

TRANSLATION:
Only an application presentation-mode option changes. No capability, driver callback, fence semantics, cache policy, allocation layout, or package state changes. Unknown whether creating a windowed flip swapchain avoids the exclusive fullscreen proxy boundary and permits the existing Clear/Present chain to execute. Atomic lifecycle companion changes are expected-state assertion and restoring fullscreen only for the fullscreen case; this is one application-mode contract.

WHAT IS STILL UNKNOWN:
Whether windowed CreateSwapChain completes and whether Clear/Present reaches the KMD/physical display. This is a short application discriminator in the same original934 boot, not a repaired/stable desktop claim. Twelve-second completion budget then paired stacks if pending. Do not wait minutes or change another variable.

Verification: pinned SDK26100 ARM64 v14314.44, W4/WX/PREfast clean build; source and binary SHA256, ARM64 PE check. No manufactured RED test for a diagnostic-only workload. Preserve original e1d5875c executable; separate paths and hash-guarded console task. Only owned workload may be stopped. Restore baseline through unchanged377/392 if needed; no package install or reboot is required for this diagnostic.
