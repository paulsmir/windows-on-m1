# EXP936: observe window visibility and bounded occlusion recovery

WHY THIS HYPOTHESIS: the same SDK on936 completes Clear and rotation without a device error but Present1 returns DXGI_STATUS_OCCLUDED. Before modification it only pumps messages after that single Present and never retries. Current KMD Present remains0.935 console diagnostic excluded a locked/other input desktop; it did not observe the actual SDK window at Present.

WINDOWS CONTRACT: FULL GRAPHICS application workload. Microsoft DXGI_STATUS describes OCCLUDED as content not visible; DXGI_PRESENT documentation permits PRESENT_TEST when recovering from an already-observed occluded state. Add optional --windowed-ready: record GetWindowInfo, visibility, minimized state, rect/client rect, foreground PID, monitor and actual swap effect. Only after actual OCCLUDED, pump messages and poll PRESENT_TEST for at most3s. Only S_OK permits one new clear and Present, because the first sequenced Present can rotate buffer identities. Persistent occlusion or device failure is a nonzero probe result. No forcing foreground, power, desktop, resolution or topmost state.
https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-present
https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/waiting-when-occluded
https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowinfo

AGX/ASAHI CONTRACT: unchanged original936 KMD/UMD4e7bba96, m1n1bdcf/MuR143. Previously inspected Asahi framebuffer-batch semantics and current Windows presentation/rotation path retain all ownership, DMA, fences and recovery. No new hardware initialization/register/interrupt assumptions; those remain existing m1n1/KMD owners. Mu ACPI/memory unchanged.

TRANSLATION: only optional application readiness policy plus read-only window receipts. Existing --windowed and default fullscreen behavior remain. No driver capability or NO_REDIRECTION change. Test the same render target and same green clear. Twelve-second normal workload budget is extended to18s only for this3s readiness diagnostic plus existing5s hold and possible snapshot; never wait minutes.

WHAT IS STILL UNKNOWN: whether the one-shot OCCLUDED was window setup/readiness, or persists for a visible non-minimized correctly placed window. Smallest checkpoint: window receipt and TEST/second Present result with healthy device. Original936 guest stays running; no new package or reboot. If still blocked, collect and stop the owned SDK only; do not call it display success. Existing377/392 recovery remains.

Verification: pinned SDK26100/v14314.44 ARM64 W4WX/PREfast build, source/binary hashes and PE type. Diagnostic workload only: no artificial RED test. Record build and run in936 ledger; keep original ce544 executable intact.
