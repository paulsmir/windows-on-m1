# EXP936: software-producer control on the same physical output

WHY THIS HYPOTHESIS: hardware936 Clear/rotation and device status succeed, but visible foreground window receives realPresentOCCLUDED even after TESTS_OK. Official ARM64CDB observed kernelNtGdiDdDDIPresent returnc01e0006 beforeKMDPresent, withlegacyBlt flags3081. A control producer distinguishes whether our UMD presentation route is necessary for this failure.

WINDOWS CONTRACT: RENDER_ONLY comparison using Microsoft D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_WARP), sameFL10, sameBGRAflip-sequential window, exactAPPL physicaloutput and existing boundedreadiness procedure. Query actualWARP adapterLUID/vendor/device and use its IDXGIFactory2; preserve the physicaloutput selected before device creation. No software render result may be called AGX rendering success.
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-d3d11createdevice

AGX/ASAHI CONTRACT: full936 display/KMD/UMD andbdcf/MuR143 remain installed and active. WARP replaces only this test application's producer, not DWM's renderer or the display driver. Initialization/DART/IRQ/DMA/power/recovery remain original936. Thus a WARP failure does not exclude an AGX compositor failure; a successful control isolates a difference in the producer/presentation route.

TRANSLATION: optional --warp-control implies samewindowedreadiness mode and changes only the producer plus its required factory. No NO_REDIRECTION/capability/driver/resource-layout/power change. Source inspected: currentSDKprobe, officialD3D11CreateDevice, currentPresent/rotation paths and pinnedWDK publicPRESENT header.

WHAT IS STILL UNKNOWN: whether WARP can create/present a healthy frame on the same physicalmonitor while theAGX producer isoccluded. Bounded18s ownedworkload; captureAPI/window status, realpicture separately, andstoponlyownprocess. No newpackage/reboot. Existing377/392 recovery.

Verification: diagnostic-only SDK26100/v14314.44 ARM64 W4WXPREfast build, source/hash/PEgates. Keeppriorce544 and5a6 probes unchanged. No artificialRED test for this control experiment.
