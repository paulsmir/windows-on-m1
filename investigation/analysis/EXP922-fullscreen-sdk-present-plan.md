# EXP922: locate the public fullscreen presentation boundary

WHY THIS HYPOTHESIS: EXP918/920 composition-only creation and backbuffer acquisition
succeeded; that API does not create a fullscreen primary. EXP921's fresh DWM1228
selects Apple/FL10 and completes initial work but never enters any native DXGI
Present/SetDisplayMode function; its display-set derivation reports failure.
These observations do not establish whether the fullscreen primary path is valid.
No additional ambient boot or DWM restart will distinguish it.

WINDOWS CONTRACT: FULL GRAPHICS public D3D11 FL10 plus DXGI1.2 fullscreen flip
chain for a real console HWND, BGRA87, two buffers, one sample, native refresh
(numerator0), one clear and one Present1. Select the exact Apple ACPI IDs and
attached output; verify the actual fullscreen state, backbuffer and every HRESULT.
Use documented API structures, never a fabricated runtime DXGI context.
Microsoft sources: CreateSwapChainForHwnd,
https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforhwnd;
DXGI_SWAP_CHAIN_FULLSCREEN_DESC,
https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/ns-dxgi1_2-dxgi_swap_chain_fullscreen_desc.
AGX/ASAHI CONTRACT: exact signed package30.0.920.0/source d16f42c0 retains current
native Mesa FL10 resource/clear/flush translation, KMD primary SetDisplayModeCb,
PresentCb and existing AGX/scanout owners. Firmware m1n1d3e0f999/Mue54c0098 unchanged.
TRANSLATION: a single console SDK workload creates the fullscreen primary and
backbuffer, clears the buffer, calls Present1 once, records device-removed reason,
then restores windowed state and releases resources. No new advertised capability
or NO_REDIRECTION change. This tests the ordinary primary/Present contract rather
than the earlier composition-only object-creation boundary.
WHAT IS STILL UNKNOWN: whether fullscreen creation, primary handoff, clear or
Present first fails; whether the KMD receives/completes it and current scanout
changes. DWM's internal display-set failure remains unproved until an exact
refusal or violated source contract is identified. Client output alone would not
prove a working desktop.

Inspected: current native Device.cpp and DxgiFns.cpp; build-native-asahi-state.py
DXGI adaptation; umd.c primary validator and SetDisplayMode callback; lifecycle.c
caps; current source display.c and EXP921 actual new-PID ETW/UMD/frame receipts.
No low-level hardware protocol is being changed. Windows ABI/frontend owns any
refusal; KMD owns residency/submission/completion and m1n1 owns scanout transport.

One variable: one exact hash-pinned SDK workload on a fresh install of unchanged920
package. No repeated DWM reinitialization. At first confirmed Code0/CPU8/Stage12 and
active console, invoke once; after its bounded result and one Present/frame check,
collect and recover immediately while Present0. Long stability windows are reserved
for actual physical Present/output per the user's explicit instruction.
Preserve build/sign/hash manifests before staging; all evidence host-size/SHA gates
before normal377/392 Code43 exact rollback and ordinary Code28 twice. No live Code0
package removal. If the workload does not finish, collect the exact blocked stage
and do not retry it. A source-confirmed deterministic defect requires its own real
regression test and correction before another driver package experiment.
