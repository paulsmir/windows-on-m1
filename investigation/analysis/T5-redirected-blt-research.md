# T5: windowed redirected blits on a hardware WDDM driver

Research date: 2026-10-23. Source snapshot:
`193e9e76c888ca0e78977b79c88a585bda4a64d5`, branch
`codex/t5-redirected-blt-research`. This is research, not a validated fix.
No hardware, guest, package build/install, or other worktree was accessed.
Public documentation was consulted over HTTPS; local saved evidence was read.

## Finding

The inspected public contracts do **not** specify a sufficient set of WDDM
2.x/3.x caps that forces creation of a GPU redirection surface for a hardware
driver with GDI acceleration disabled. They describe the participants and
several supported paths, not the current win32k/DxgKrnl/DWM selection algorithm.
Do not turn the Basic Display trace into an allocation-type requirement for AGX.
Do not treat `DriverSupportsCddDwmInterop=1` as a surface-creation request.

There is one narrower, testable declaration discrepancy: AGX says its aperture
is not declared cache-coherent, yet leaves
`PresentationCaps.NoCacheCoherentApertureMemory` zero. Testing the truthful
negative capability is a candidate, **not proof of the root cause**. The
experiment below tests its influence on this Windows build without enabling
unimplemented GDI acceleration.

## Observed evidence, independently checked

Evidence root is
`/Volumes/pwdev/public_windows/.local/experiments/EXP1906-zls-c69/evidence/`.
The following counts came from Python `csv.reader(..., skipinitialspace=True)`
over the saved CSV files, filtering provider `Microsoft-Windows-DxgKrnl` and
event ID column 2. These are trace-wide counts, not all app events.

| Trace | 287 | 166 | 184 | 215 | 104 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `EXP1906E/dxg.csv` | 0 | 0 | 392 | 11 | 6 |
| `EXP1906E-basic/dxg.csv` | 3 | 6 | 141 | 16 | 0 |

* AGX PID `0x72c`: `EXP1906E/dxg.csv:116292` records
  `DXGK_PERFORMANCE_GDI_BLT`; line 116301 records event 184,
  `Blt |RedirectedBlt`, status 3223191558 (`0xc01e0006`), source
  `0x40001200`, destination zero. Lines 118524, 120346, 181447, 183563,
  and 185334 repeat that result. The 11 event-215 records elsewhere in the
  trace are not evidence of successful app redirected blits.
* Basic PID `0x2a9c`: `EXP1906E-basic/dxg.csv:20815` is the 160x160
  `CreateShared` / `TextureCpuVisible` event 287. Line 22837 is Blit (166),
  line 22859 is history event 215 with `D3DKMT_PM_REDIRECTED_BLT`, and
  line 22865 is event 184 with status zero and destination `0x40000900`.
  A second 160x160 creation appears at line 34085; the other event 287 is
  a 1x1 allocation in another PID (line 12794).
* `EXP1906E/dxgetw-probe.txt:1` onward reports successful device creation,
  readback and API-level presents for DEFAULT and ON12. These PASS receipts
  do not prove composition. The probe's PASS is based on HRESULT at
  `drivers/apple-agx/windows/icd/agx_d3d9_probe.c:142`; it separately prints
  front-buffer pixels at line 136. The source itself notes that this pixel
  readback did not distinguish Basic and AGX (lines 106-108).

CSV SHA-256:

* AGX: `02a697bfa3155595cf1091acfe8539cc9f1dae9bbdb33a84b1193c0a881634d0`.
* Basic: `b95d6907b170cd0c0b09eb7f8d6797209014d4865075df379d3db50a8c9a7dc7`.

The supplied TASK reports the rejected interop-cap experiments EXP1903 and
EXP1908E. Their raw traces were not revalidated here. The named scratchpad
parser is absent in this worktree and at the main-root path; the CSV inspection
above does not depend on it. No complete experiment-ledger reread was needed.

## Documented Windows contracts

1. **Bitblt versus flip.** A bitblt present copies the app back buffer into a
   DWM redirection surface; flip composition shares the app buffers with DWM.
   Thus working flips do not exercise all of the blt allocation/open/copy path.
   [Microsoft: DXGI flip model](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-flip-model).

2. **CDD/DWM interop.** `DriverSupportsCddDwmInterop` describes CDD presents
   into textures created by the UMD for DWM. With GDI acceleration enabled the
   bit is ignored and that support is required. `SupportKernelModeCommandBuffer`
   describes `DxgkDdiRenderKm` processing, not ordinary 3D copies.
   `SupportSoftwareDeviceBitmaps` is reserved and must remain zero.
   `NoCacheCoherentApertureMemory` declares absence of cache-coherent aperture
   memory. None of these descriptions provides a current OS redirection-selector
   truth table.
   [Microsoft: DXGK_PRESENTATIONCAPS](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_presentationcaps).

3. **GDI acceleration requirements.** Microsoft requires a suitable coherent
   aperture and the allocation, standard-allocation and RenderKm DDIs before
   reporting this feature. Its WDDM 1.2 feature matrix actually calls GDI
   acceleration mandatory for full graphics/render-only drivers. That historical
   matrix is not evidence that a current WDDM 3.x driver with the bit clear gets
   a specified fallback. Conversely, it does not prove the bit alone causes
   today's occlusion. The no-GDI premise needs empirical confirmation on a
   comparable hardware driver.
   [GDI hardware acceleration](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gdi-hardware-acceleration),
   [WDDM driver and feature caps](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/wddm-driver-and-feature-caps).

4. **Standard allocations.** `GDISURFACE_TEXTURE` is a shared, GPU texture
   opened by the UMD for DWM composition and usable for DirectX rendering and
   GDI operations. `TEXTURE_CPUVISIBLE` is reserved for the system.
   `TEXTURE_CROSSADAPTER` is a shared cross-adapter texture with explicit pitch
   and height alignment requirements; it is not a generic switch for a
   single-adapter window. `GetStandardAllocationDriverData` returns an allocation
   description when called; implementing it does not request a new allocation.
   [GDI surface types](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmdt/ne-d3dkmdt-_d3dkmdt_gdisurfacetype),
   [standard-allocation callback](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_getstandardallocationdriverdata).

5. **Cross-adapter path.** Microsoft's hybrid-graphics example creates a
   top-level window's cross-adapter resource on the integrated GPU, opens the
   same backing on the discrete GPU, then has the runtime/UMD copy into it and
   DWM consume it. This establishes a real standard-allocation redirection path,
   but does not establish that AGX should advertise hybrid support or implement
   that allocation class to fix a same-adapter failure.
   [Rendering on a discrete GPU using cross-adapter resources](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/rendering-on-a-discrete-gpu-using-cross-adapter-resources).

6. **UMD-created textures and flags.** The interop contract explicitly allows
   UMD-created DWM textures. `D3DDDI_RESOURCEFLAGS.SharedResource` means sharing
   between devices/processes; `Primary` and `MatchGdiPrimary` describe primary
   surface properties. These are distinct flags, not a documented mandatory
   redirection recipe. On the D3D10/11 path, inspect its own BindFlags,
   MiscFlags and primary descriptor rather than substituting D3D9 flag layouts.
   Local ABI: `.local/reference/winsdk26100/shared/d3dukmdt.h:1223` (the flags);
   AGX translation: `drivers/apple-agx/mesa/winsys/agx_d3d10_windows.cpp:1009`.
   [Microsoft: resource flags](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ns-d3dukmdt-_d3dddi_resourceflags).

7. **Unknown destination is legal at the UMD boundary.**
   `DXGI_DDI_ARG_PRESENT.hDstResource` can be null because kernel mode determines
   it later. This is not equivalent to a successful final kernel present with
   an absent redirection destination. Preserve `pDXGIContext` through the
   callback. Do not invent a destination handle in the driver.
   [Microsoft: DXGI_DDI_ARG_PRESENT](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dxgiddi/ns-dxgiddi-dxgi_ddi_arg_present).

8. **DirectFlip is a different contract.** Its purpose is efficient composition
   transitions for fullscreen-covering content. `SupportDirectFlip` does not
   document creation of the separate destination needed for legacy windowed
   blits. [Microsoft: Direct flip of video memory](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/direct-flip-of-video-memory).

The historical `DwmDxGetWindowSharedSurface` documentation describes both DX
shared surfaces and GDI redirection surfaces, including the redirected-blt token.
However, Microsoft limits that contract to Windows 7 and driver/runtime callers.
It is useful architectural evidence, not a supported Windows 11 probe API.
[Microsoft: DwmDxGetWindowSharedSurface](https://learn.microsoft.com/en-us/windows/win32/dwm/dwmdxgetwindowsharedsurface).

## Current implementation and reference samples

Repository paths below refer to the snapshot at the top of this note.

| Source evidence | Observation and implication |
| --- | --- |
| `drivers/apple-agx/render-admission/src/lifecycle.c:715`, `:745`, `:752`, `:753`, `:766` | Caps are zero-initialized; FlipOnVSyncMmIo and DirectFlip are set; GpuMmu is conditional on the qualified model; GDI command buffers are explicitly disabled because RenderKm is incomplete and the aperture is not declared coherent. No assignment sets NoCacheCoherentApertureMemory. |
| `drivers/apple-agx/render-admission/src/memory_windows.c:21`, `:41` | Both segment descriptor builders zero flags and set aperture/CPU visibility/page/budget fields without declaring CacheCoherent. CPU visibility is not a cache-coherency declaration. |
| `drivers/apple-agx/render-admission/src/driver.c:71`; `src/allocation_windows.c:223`, `:248`, `:325` | The callback is registered; GDISURFACE_TEXTURE is already supported, as are primary/shadow descriptions. The GDI helper rejects unsupported types, including CROSSADAPTER and reserved TEXTURE_CPUVISIBLE. The wrapper records failure, but absence of event 287 does not prove this callback was never entered. |
| `drivers/apple-agx/mesa/scripts/build-native-asahi-state.py:4547`, `:5072` | Shared/present/primary resources enter the presentation path; OpenResource calls the shared presentation import. Thus a UMD-created DWM surface must be followed through create/open and errors, not inferred solely from CDD-standard-allocation events. |
| `drivers/apple-agx/mesa/winsys/agx_d3d10_windows.cpp:960`, `:991` | Import/create bridge to AdmissionUmdOpenResource/CreateResource and attach a render resource; failures propagate. No trace here proves a particular window's destination reached these functions. |
| `drivers/apple-agx/mesa/scripts/build-native-asahi-state.py:2905` | `_Blt` still constrains its destination rectangle to 2560x1600 and presentation-backed resources. This is downstream compatibility debt, not evidence that it caused the missing destination in these traces. Do not repair it as a claimed fix for this boundary without a call/refusal trace. |
| `drivers/apple-agx/render-admission/src/present_windows.c:61`, `:107` | KMD blt validation consumes already-open source/destination allocations; it does not create a window redirection surface. |

Microsoft's local render-only sample is particularly misleading if copied:
`.local/reference/microsoft-graphics-driver-samples/render-only-sample/roskmd/RosKmdAdapter.cpp:1001`
disables GDI buffers but sets `SupportSoftwareDeviceBitmaps=TRUE`; line 1984
returns `STATUS_NOT_IMPLEMENTED` for GDISURFACE. Those source facts conflict
with using it as proof of a complete, supported modern redirection path.
The current reserved-field documentation takes precedence for an AGX proposal.

The local sample checkout contains only render-only-sample, so KMDOD was read
from Microsoft's public repository. It registers the display-only interface,
including `PresentDisplayOnly`, and calls `DxgkInitializeDisplayOnlyDriver`;
its present routine copies dirty/moved regions into a framebuffer. It is not
a hardware rendering KMD/UMD pair that exposes the missing selector.
[KMDOD bdd_ddi.cxx, lines 28-59](https://github.com/microsoft/Windows-driver-samples/blob/main/video/KMDOD/bdd_ddi.cxx#L28-L59),
[bdd.cxx, PresentDisplayOnly](https://github.com/microsoft/Windows-driver-samples/blob/main/video/KMDOD/bdd.cxx#L400-L466).

Ownership at this boundary: Windows selects window redirection and maintains
composition history; runtime/UMD creates or opens the applicable resources and
submits rendering/copies; KMD implements advertised allocation/present contracts.
GPU initialization, interrupt routing, DMA translation, power and recovery remain
with the existing platform/KMD contracts. This research changes none of those.
Asahi, m1n1 and Mu do not implement Windows' HWND redirection selector. Their
hardware protocols were not re-audited, and no new hardware-coherency claim or
firmware change is justified by this note. The observed working flip path is a
control, not proof that every sharing or CDD path works.

## Public ETW evidence and the missing hardware control

Intel's PresentMon is primary-source evidence for how public event consumers
recognize composed copies. Its comments describe this sequence:
runtime PresentStart → DxgKrnl Blit → PresentHistoryDetailed → DxgKrnl Present
→ history propagation → DWM UpdateWindow → DWM Present/display completion.
It separately describes the CPU/Vista-blit path. Its implementation recognizes
redirected-blt history after a blit as `Composed_Copy_GPU_GDI`.
[PresentMon trace consumer header, lines 62-78](https://github.com/GameTechDev/PresentMon/blob/main/PresentData/PresentMonTraceConsumer.hpp#L62-L78),
[implementation, lines 747-752](https://github.com/GameTechDev/PresentMon/blob/main/PresentData/PresentMonTraceConsumer.cpp#L747-L752).
WDK model values are independently present in
`.local/reference/winsdk26100/shared/d3dkmthk.h:443`: BLT=3 and VISTABLT=4.

This is an expected **event family**, not a vendor-specific captured trace of
the same probe. No matching public trace with proven hardware-adapter identity,
WDDM version and disabled GDI acceleration was located in the sources inspected.
Also, PresentMon's mode name does not prove KMD GDI acceleration: the Basic
reference itself emits model 3. Event 287 is not a necessary step in PresentMon's
composed-copy recognition, and its absence alone is not a complete discriminator.

For a real control, Claude should arrange a separate Windows machine with a
vendor WDDM hardware driver. Run the same probe binary/bitness, window geometry,
DISCARD/MSAA settings and hold interval on an unobscured local desktop. Record
OS build, adapter LUID, vendor/device ID, driver version and probe hash. If
comparing the no-GDI premise, establish the actual driver cap from available
diagnostics; do not infer it from PresentMon. Capture from before HWND creation
through destruction with DxgKrnl, DWM-Core, Win32k and D3D9/DXGI providers,
including allocation/open events and event-loss counts. Preserve ETL, decoder
version/provider schema, PresentMon CSV and a screenshot. Correlate PID/TID,
HWND, allocation/resource handles, token and time; separate DWM's own flips.
Basic on J313 cannot supply this hardware-driver control. This capture was not run.

## One proposed AGX experiment

**Hypothesis (inferred):** with no GDI acceleration and no declared coherent
aperture, the zero `NoCacheCoherentApertureMemory` declaration influences Windows
to choose a redirection path that cannot acquire a usable destination. Setting
the documented negative capability may change that choice. This is a lower
confidence diagnostic candidate; the public contract does not promise it.

**WHY THIS HYPOTHESIS:**

1. `lifecycle.c:753` explicitly cites absence of a declared coherent aperture,
   and both segment builders leave CacheCoherent clear; nevertheless the caps
   zero-initialization leaves the corresponding negative declaration clear.
   This is a concrete, nearby contract discrepancy, unlike new register or
   GDI-ROP guesses.
2. The six app presents fail with destination zero before any observed Blit
   event, while flip history exists. A presentation-path declaration is closer
   to this boundary than render kernels, UAT or interrupt handling.
3. TASK reports the CDD/DWM interop-bit-only attempt already failed. This is a
   distinct declared memory property, not another run of that same hypothesis.

**Single variable:** on a separately preregistered candidate, set only
`DXGK_DRIVERCAPS.PresentationCaps.NoCacheCoherentApertureMemory` from 0 to 1.
Keep GDI acceleration disabled and all other caps, segment descriptors, UMDs,
firmware, signer and probe constant relative to the chosen control. Do not set
reserved bitmap caps or implement reserved surface types. First verify the
actual control package still has the source declarations above; if it does not,
this proposal is stale and should not run. Add an offline test of the actual
caps output and compile it before packaging; no candidate is implemented here.

**Smallest checkpoint:** capture the same visible-window D3D9 probe from before
creation for a bounded interval. The predicted discriminant is event 184 for
the app HWND changing from status `0xc01e0006`, destination zero to status zero
with a real destination, together with app-correlated Blit/history evidence
(166/215 on this captured schema). Observe any new event 287 and its type, but
do not require Basic's reserved type. A VISTABLT/CPU path would be a different
result from the target GPU redirected-blt path and must be reported separately.

**Verdicts:** unchanged six occluded/zero-destination presents reject this
single-bit hypothesis for the pinned configuration. An allocation attempt that
now fails advances the boundary and names the next failure; it does not confirm
visible presentation. A new model-3 path plus DWM consumption and visible pixels
supports the hypothesis. Adapter-start failure, event loss, changed package
identity or an actually occluded window makes the run inconclusive. Stop after
one controlled comparison; no series of guessed caps.

Claude must preregister exact artifact hashes, commands, profile and recovery
artifact using the current hardware rules before any execution. Use the existing
validated full-owner/recovery contract, collect evidence first, and restore the
exact control package/ordinary Code28 at series end as that contract requires.
This note is not a completed preregistration and provides no permission to run
G2/m1n1 changes. No new live ADT/register observation was made in this offline task.

## WHAT IS STILL UNKNOWN

* The current OS's exact selector and which failed internal operation yields
  occlusion for this HWND; public docs do not expose that decision tree.
* Whether DWM attempts UMD CreateResource/OpenResource for this destination,
  whether it fails, and whether a standard-allocation request occurs without
  a completed event 287. Trace both process sides before choosing an owner fix.
* Whether the negative aperture bit has any effect when GDI acceleration is off.
* Whether a comparable hardware driver with that feature disabled supports this
  exact workload and which history model it uses.
* Whether downstream shared-open or rectangle restrictions will become the next
  failure once Windows obtains a destination. They are not validated causes here.

## Offline verification and scope

T5's TASK says research only and no code changes needed. No confirmed driver fix
or regression test was manufactured. Source and tests remained unchanged.
The README's instruction to leave EXPERIMENTS.md and CHANGES.csv to the merger
was followed.

The required unfiltered command `cd tests && python3 run_against_baseline.py`
returned exit 1:

```text
ran 357 tests in 150 modules; 11 failing, 9 known, 2 NEW, 0 FIXED
NEW test_d3d12_texture1d.Texture1DShaders.test_actual_fxc_dimensions_load_store_through_asahi
NEW test_g4_real_broker_builder_replay.G4RealBrokerBuilderReplay.test_dwm_sized_native_frame_uses_published_process_ranges
BASELINE_NEW_FAILURES
```

Both are listed in the handoff's fresh-worktree warning. A focused rerun
confirmed the texture-header SHA mismatch (actual
`2e2a65a77165ffe4c9b48dd1a4c34cefe380c5524ca9d574c393c60fe12a8d8b`,
expected `b0f80a2ae47947004b50917cd75d72a0c775b47b806bbfa78949dfc3618d3d0a`)
and missing `m1n1_windows/src/hv_agx_gpuva_v5.c`. The exact gitlink object
`21c3648d32423def46086e258e0995c737dc9284` is absent from the local submodule
object database. No other worktree or substitute source was used. No baseline
entries or pinned fixtures were changed. The full BASELINE_OK requirement
therefore remains unmet and requires the integration environment.

Additional relevant existing modules were run: standard allocation,
render-present-blt and WDDM2 Present1 (5 tests, 3 errors). The first two modules
have extraction-fixture compile errors against the unchanged production source
(missing current helper declarations/types). These are outside the default
baseline module selection and are reported rather than hidden.
Separately, `python3 -m unittest -v test_wddm2_present1_surfaces` passes (1 test).
The scoped baseline command for that module prints:

```text
ran ? tests in 1 modules; 0 failing, 0 known, 0 NEW, 0 FIXED
BASELINE_OK
```

The runner's `Ran (\d+) tests` regex does not parse unittest's singular `test`.
The direct verbose run establishes the count as 1. This scoped pass is **not**
a replacement for the failed unfiltered baseline. Logs are in the T5 handoff
directory: `baseline.log`, `environment-failures.log`, `targeted-tests.log`,
`present1-tests.log`, `scoped-baseline.log`.
