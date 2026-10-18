TOP-LEVEL MISSION UPDATE — FULL GRAPHICS WINDOWS DESKTOP

Continue autonomously from the exact current Apple AGX project state.

The primary mission is now:

DELIVER A REAL, USABLE, FULL-GRAPHICS WINDOWS DRIVER FOR APPLE AGX WITH A HARDWARE-ACCELERATED NORMAL WINDOWS DESKTOP.

This supersedes CS 1.6 / OpenGL as the immediate top-level milestone.

CS 1.6, OpenGL/WGL and broader application compatibility remain later goals, but they must NOT distract from completing the standard Windows Full Graphics path first.

Do not restart the investigation.
Do not repeat hardware-proven EXPs.
Read and preserve the current truth in:

investigation/GPU_CURRENT_STATE.md
investigation/ACCELERATED_DESKTOP_ROADMAP.md
investigation/EXPERIMENTS.md
investigation/CHANGES.csv

Continue from the first currently open accelerated-desktop milestone.

==================================================
FINAL PRIMARY ACCEPTANCE TARGET
==================================================

The mission is complete only when the MacBook Air can boot Windows normally with the final Apple AGX driver installed and provide a stable, hardware-accelerated ordinary Windows desktop.

Final acceptance requires ALL of the following:

1. DRIVER / BOOT

- final Apple AGX KMD + UMD package installed and enabled;
- APPL0002 Code 0;
- correct driver/service/package versions and hashes;
- Windows boots normally with the driver installed;
- no requirement to return to Code28/no-driver mode for ordinary use;
- 8 CPUs remain available;
- keyboard, trackpad/input, USB/xHCI and storage remain functional;
- no deterministic GPU-caused boot failure.

2. REAL DIRECT3D DEVICE

The standard Windows Direct3D runtime must create a real hardware device on the Apple AGX adapter.

No WARP.
No Microsoft Basic Render Driver.
No software fallback presented as success.

The advertised feature level and pipeline capabilities must be truthful and backed by implemented behavior.

Do not enable a capability bit merely to advance Windows admission.

Implement the complete mandatory contract for the selected advertised D3D level.

3. PRODUCTION UMD FRONTEND

Complete the selected Mesa D3D frontend integration over the existing Windows AGX architecture.

Target architecture:

Windows applications / DWM
→ Direct3D runtime
→ production D3D UMD frontend
→ Mesa/Gallium
→ Asahi state/compiler/encoder
→ Windows AGX winsys/transport
→ existing WDDM KMD
→ Render / Patch-or-prepatch / Submit
→ retained-root / firmware owner
→ physical AGX TA/3D
→ Windows fence completion

Do not create a second independent renderer when existing Mesa/Asahi functionality can be reused correctly.

The existing EXP208-derived/fixed workload remains a hardware control and firmware-structure reference, not the production renderer.

4. DYNAMIC GRAPHICS

A PASS requires dynamic application-controlled rendering.

The application/runtime must be able to change at least:

- geometry;
- vertex data;
- shaders;
- render-target contents;
- colors;
- resources/textures where required by the selected D3D contract.

A fixed replay or prebuilt qualification image is NOT a production graphics PASS.

5. WINDOWS RESOURCE MODEL

Use normal Windows/WDDM ownership:

- runtime-created devices and contexts;
- WDDM allocations;
- VidMm residency;
- allocation-relative references;
- proper resource lifetime;
- correct generation/ownership checks;
- exact fences;
- cleanup/reset/re-entry.

Do not trust user-supplied physical addresses or firmware-private pointers.

Firmware-private root/tables/queues remain owned by their established owner.

6. STANDARD PRESENT

Implement the standard Windows/DXGI Present path.

Private qualification flips, direct-KMT exclusive-owner shortcuts and diagnostic Escape-present paths are NOT final acceptance.

A standard application-created render target must progress through the supported Windows presentation model to the physical panel.

CPU-assisted presentation conversion may be used temporarily as a clearly labelled intermediate diagnostic milestone, but it does NOT constitute final accelerated-desktop acceptance if CPU rasterization/composition substitutes for the GPU.

7. DWM HARDWARE ACCELERATION

Desktop Window Manager must create and use a hardware Direct3D device on the Apple AGX adapter.

Prove this through correlated evidence:

DWM
→ Apple AGX D3D device/context/resources
→ real Windows-originated GPU packets
→ physical TA/3D
→ exact completion/fences
→ standard Present
→ physical DCP latch

The mere presence of dwm.exe is not evidence of acceleration.

8. ACTUAL WINDOWS DESKTOP

The built-in panel must display a usable normal Windows environment through the supported graphics path:

- LogonUI;
- desktop;
- Explorer;
- taskbar;
- windows;
- cursor;
- window movement;
- minimize/restore;
- resize;
- overlapping windows;
- normal desktop updates.

The screen must update continuously rather than displaying a one-shot qualification surface.

9. REPEATED PRESENT / LIFETIME

Prove repeated real rendering and presentation.

Minimum final stability qualification:

- >= 1000 standard Presents;
- >= 100 create/resize/minimize/restore/destroy cycles;
- changing framebuffer contents;
- correct allocation replacement/lifetime;
- no stale generation reuse;
- no deterministic fence reuse bug;
- no deterministic resource leak according to measured counters.

10. STABILITY

Run at least a 30-minute normal accelerated desktop session.

During this session verify:

- desktop remains interactive;
- repeated rendering continues;
- no deterministic GPU-caused TDR;
- no GPU-caused bugcheck;
- no corruption;
- no permanent hang;
- no deterministic input regression;
- no demonstrated GPU-caused storage regression.

Event129 remains storage telemetry unless a reproducible GPU causal link is proven.

11. RESET / RE-ENTRY

The final driver must survive the supported controlled graphics reset/re-entry path.

Firmware-local sequence state and Windows fence lifetime must remain correctly separated.

Do not claim sleep/resume unless actually implemented and tested.

If sleep/resume is not ready, list it explicitly as a known limitation rather than blocking the first Full Graphics desktop PASS unless Windows requires it for normal operation.

==================================================
CURRENT PRIORITY ORDER
==================================================

Work in this order:

A. Complete the production D3D frontend/runtime contract.

B. Connect it to the existing device-scoped agx_win32_pipe_screen / Windows transport.

C. Complete resource/BO lifecycle and mandatory backend operations.

D. Implement the complete truthful D3D feature-level obligation matrix.

E. Prove a standard D3D application creates an Apple AGX hardware device.

F. Prove a normal runtime-originated dynamic draw:
   D3D runtime
   → UMD
   → Mesa/Asahi
   → KMD
   → physical TA/3D
   → Windows fence.

G. Complete standard DXGI Present.

H. Bring up DWM on Apple AGX.

I. Reach a usable hardware-accelerated Windows desktop.

J. Perform repeated-present/lifetime/stability/reset qualification.

Only after Full Graphics desktop acceptance move the primary mission to:

- OpenGL/WGL ICD;
- broader D3D compatibility;
- x86/Windows-on-ARM legacy application compatibility;
- Counter-Strike 1.6;
- additional applications/games.

==================================================
DEVELOPMENT DISCIPLINE
==================================================

Continue autonomously.

Do not stop after an individual task, EXP, PASS, REJECTED result, build, cleanup, milestone or handoff.

Whenever you identify NEXT CAUSAL ACTION, execute it.

Use:

source-first analysis
→ exact contract
→ RED test
→ minimal implementation
→ regression tests
→ build/analysis/sign gates
→ hardware experiment only when hardware evidence is required
→ exact evidence
→ cleanup/recovery when required
→ next boundary

Do not use hardware experiments to discover things that can be deterministically proven offline.

Do not weaken validation to make Windows advance.

Do not fake successful DDIs.

Do not advertise unsupported capabilities.

Do not confuse:

DLL load
Code0
CreateDevice
triangle
fence
private Present
visible diagnostic surface

with Full Graphics desktop acceptance.

They are intermediate milestones only.

==================================================
HARDWARE-PROVEN LOWER LAYERS
==================================================

Preserve already hardware-proven lower layers unless direct contradictory evidence appears.

Do not gratuitously redesign:

- m1n1/Mu platform;
- retained-root ownership;
- firmware-private memory ownership;
- RTKit lifecycle;
- UAT/broker foundations;
- TA/3D queues;
- completion/fence path;
- DCP ownership;
- hardware-proven dynamic rendering foundations.

The current work should increasingly move upward into:

UMD
Mesa frontend
resource model
D3D runtime
DXGI Present
DWM

rather than reopening solved low-level AGX questions.

==================================================
RECOVERY / AUTONOMY
==================================================

Routine recovery is part of the task.

If ordinary recovery fails, choose among already validated safe recovery mechanisms yourself.

Do not stop merely because:

- SSH temporarily disappears;
- a launcher needs controlled restart;
- a test package is boot-bound;
- emergency cleanup is required;
- Windows reset after an experimental candidate;
- a builder/environment-only issue occurs.

Preserve evidence, recover, restore the known baseline and continue.

Ask me only when:

- a genuinely unavoidable physical action is required;
- an explicit platform approval gate requires me;
- a secret/credential interaction is required;
- or every established safe recovery path has been exhausted.

Before asking, complete all independent offline work.

==================================================
PROGRESS REPORTING
==================================================

Keep progress visible without ending execution.

After each major milestone publish a concise status:

CURRENT PHASE:
HARDWARE/SOFTWARE PROVEN:
CURRENT FIRST UNKNOWN:
IMPLEMENTED:
TESTS:
NEXT:
PROGRESS TO ACCELERATED DESKTOP:

Then immediately continue executing NEXT.

This status is not a handoff.

==================================================
FINAL MACHINE STATE
==================================================

At final Full Graphics PASS:

DO NOT uninstall the final driver.
DO NOT restore Code28.
DO NOT return to the no-driver ordinary baseline.

Leave the final known-good Apple AGX KMD/UMD installed, enabled and active.

Record:

FINAL KMD/UMD VERSION
PACKAGE/HASHES
D3D FEATURE LEVEL
D3D DEVICE RESULT
DYNAMIC DRAW RESULT
TA/3D RESULT
FENCE RESULT
STANDARD PRESENT RESULT
DWM RESULT
DESKTOP RESULT
1000-PRESENT RESULT
LIFETIME/STRESS RESULT
RESET/RE-ENTRY RESULT
30-MINUTE STABILITY RESULT
KNOWN LIMITATIONS
CURRENT MACHINE STATE

Only when the ordinary Windows desktop is genuinely hardware accelerated through Apple AGX and the acceptance criteria above are satisfied may this primary mission be declared complete.

OpenGL and CS 1.6 are the next mission after that, not a substitute for Full Graphics desktop completion.

Resume now from the exact current accelerated-desktop roadmap and execute the first unfinished stage.