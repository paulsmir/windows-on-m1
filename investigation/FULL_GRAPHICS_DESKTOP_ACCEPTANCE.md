# Active mission: usable hardware-accelerated Windows desktop

Authoritative objective: user attachment
`/Users/pavel/.codex/attachments/d02412ec-8aeb-4a37-ac38-74d62a9f2ef1/pasted-text-1.txt`.
OpenGL/WGL and CS1.6 are subsequent goals. The mission is not complete until
every row below is supported by evidence from the final installed package.

| Requirement | Evidence required | Current completion |
|---|---|---|
| Driver and normal boot | Final KMD/UMD installed/enabled, exact versions/hashes, Code0, normal boot,8CPU/input/USB/storage | NO; ordinary test baseline has no GPU package |
| Hardware Direct3D device | Standard runtime creates hardware AppleAgx device, truthful complete feature level, no WARP/fallback | NO; pipeline0 |
| Production frontend | Runtime -> Mesa/Gallium -> Asahi -> Windows winsys -> existing KMD | INCOMPLETE; frontend compiles, Windows adapter/device ownership helpers tested |
| Application-controlled graphics | Geometry, vertices, shaders, contents, colors and required textures/resources change through runtime | NO; native qualification draws are lower-layer controls |
| WDDM resource model | Runtime devices/contexts, allocations/residency/lifetime/generations/fences/reset/re-entry | PARTIAL; component proofs do not cover integrated frontend |
| Standard Present | Normal application target reaches panel through supported DXGI/DDI path | NO; private qualification presentation is not acceptance |
| DWM acceleration | Correlated DWM device/context/resources/packets -> physical AGX/fence -> standard Present/latch | NO |
| Usable ordinary UI | LogonUI, Explorer/taskbar/windows/cursor, movement/minimize/restore/resize/overlap/continuous redraw | NO |
| Repeated lifetime | >=1000 standard Presents and >=100 create/resize/minimize/restore/destroy cycles, measured no deterministic leaks/stale fences | NO; four native raw frames do not substitute |
| Stability | >=30 minutes interactive accelerated desktop without GPU-caused TDR/crash/corruption/hang/input/storage regression | NO |
| Reset/re-entry | Supported controlled graphics reset and re-entry on final driver, firmware sequence distinct from Windows fence lifetime | NO integrated final proof |

Sleep/resume must be listed as a limitation if unimplemented; do not claim it
from another reset test. Event129 remains telemetry without reproducible GPU
causality. At final acceptance leave the known-good driver installed and active.

Continue in the user's order: production frontend/runtime -> device-scoped
pipe screen/transport -> resources and mandatory backend operations -> full
feature-level obligations -> runtime hardware device/draw -> standard Present
-> DWM -> usable desktop -> stress/stability/reset qualification.

Final audit must record package/hashes, D3D feature level/device, dynamic draw,
TA3D/fences, standard Present/DWM/desktop,1000 Presents,lifecycle stress, reset,
30-minute result, limitations and current machine state. Build/test/component
PASS never marks the entire mission complete.
