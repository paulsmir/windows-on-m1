# EXP907 follow-up: VSync contract and retained ETL

This is a source-first offline audit, not a new hardware experiment or a claim that the black screen is fixed. Root worktree HEAD is `6f9b22c2e8f0ac590bbc083638d6fd8b3167bd63`, branch `integration/ad04-windows-compiler`. Existing m1n1/Mu dirty descendants and the two pre-existing untracked files were preserved. No production source, package, caps, autologon, launcher or recovery image was changed. No implementation commit or hardware build was made.

## Current versus historical state

The supervisor-owned `codex exec resume` PID59140 and `run_uefi.py` PID77502 were initially present. Only offline reads followed until both disappeared. Read-only SSH then confirmed ordinary boot `2026-10-01T00:35:31.2618250Z`, CPU8, one APPL0002 Code28, package0. Both USB endpoints C02HDNCCQ6L41/L43 existed and there was no active launcher. A second complete ordinary-contract check at `00:56:47.7490066Z` passed: no package/INF, arms, SYS/UMD/service, signer, AutoLogger or WER settings; two disks OK, USB5, TermService Running, autologon1/password present, C:5577924608 bytes free. Its receipt SHA is `23d0281a9764e9e43dea7574b5a5449fc0b0db692067ed4e937e2f26fbbc8928`. Nothing was deleted or restarted.

All 62 entries of EXP907 `final-host-manifest.json` independently passed host size and SHA checks. Original ETL is 268435456 bytes, SHA `8f9f56b6deb4be1afeb250a53441856386f3d948c47421d52af7aec573bcdb51`. Original boot was `00:04:38.3303920Z`; original resources, not fresh ordinary resources: IRQ889; MMIO204000000..207ffffff, 9fffb8000..9fffbbfff, 9fff70000..9fff73fff, broker300000000..300000fff; local reserve8e0000000..91fffffff. Manifest specifies scanout ABI2, GPUVA broker5, 1GiB reserve and Windows local segment998244352 bytes. DCP late snapshots refer to swap10/seq2/IOVA102a0000/PA8e0110000. No live ADT or display registers were read from the running ordinary guest; those must not be claimed as fresh measurements.

## Inspected primary sources

Asahi revision `77cb8f24c2381a8abb7272d7bbdec548d6426a8a`: `drivers/gpu/drm/apple/dcp.c` (page-flip timestamps, delayed vblank, DCP lifecycle), `iomfb_template.c` (D589, D208/D209, mode selection and swap submission), `iomfb_template.h` (D589 wire response), `apple_drv.c`, and `arch/arm64/boot/dts/apple/t8103.dtsi` (DCP mailbox/DART/clock/display resources). Sparse reference content was read with git show/git grep rather than changing its checkout.

Current worktree: render-admission `scanout_windows.c` Start/Stop/QueuePresent/ISR/ControlInterrupt; `interrupt.c` ISR/DPC/synchronization; `lifecycle.c` caps; `display.c` fixed mode and GetScanLine; shared `apple_agx_scanout.c/.h`, `apple_agx_fixed_panel.c/.h`; m1n1 `hv_agx_scanout_service.c`, `hv_agx_scanout_broker.c/.h`, `hv_agx_power_mmio.c`, `hv_exc.c`, `dcp.c`, `dcp_iomfb_latch.c/.h`, `dcp_iomfb_clock.c/.h`, `dcp_iomfb_mode_select.c/.h`, `dcp_iomfb_present.c`, and proxyclient `fw/dcp/ipc.py`; Mu APPL0002 ABI-admission ACPI. Exact current `scanout_windows.c`, `interrupt.c`, `lifecycle.c` bytes match EXP907 source.zip.

Pinned local WDK26100 `d3dkmddi.h`, `dispmprt.h`; official FULL GRAPHICS references:

- [ControlInterrupt](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_controlinterrupt)
- [FLIPCAPS](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_flipcaps)
- [interrupt data](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkargcb_notify_interrupt_data)
- [SetVidPnSourceAddress](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_setvidpnsourceaddress)
- [VSync power control](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/saving-energy-with-vsync-control)
- [VIDSCHCAPS](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_vidschcaps)
- [DXGI Present callback](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dxgiddi/ns-dxgiddi-dxgiddicb_present)
- [SynchronizeExecution](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/nc-dispmprt-dxgkcb_synchronize_execution)
- [KeCancelTimer](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kecanceltimer) and [DPC drain](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-keflushqueueddpcs)

## WINDOWS CONTRACT:

Enabled CRTC_VSYNC must report every vertical retrace, including unchanged primary. FlipOnVSyncMmIo uses SetVidPnSourceAddress at DIRQL, reports the effective scanned address and then performs scanout work through NotifyDpc. ControlInterrupt may preserve hardware ingress for internal latch/recovery while controlling OS reporting. FULL GRAPHICS does not inherit KMDOD's optional-VSync fallback. FlipIndependent is mandatory for WDDM1.3+. Current VSyncPowerSaveAware is zero: the OS is not promised idle disable/re-enable power saving. Documentation does not establish an initial enable callback for this exact OS lifecycle; zero-initialized subscription must not silently assume such a callback. Initial subscription policy needs explicit implementation and receipt. The callback must report the last actually latched Windows primary address, preserving its namespace, not a pending address or DCP IOVA/host PA. The UMD monitored-fence namespace is distinct from SubmissionFenceId.

## AGX/ASAHI CONTRACT:

D589 confirms a particular swap, not continuous retrace. Current m1n1 matches swap_id, then the service publishes IRQ_LATCHED once; broker ABI2 contains no vertical counter, latch timestamp or timing descriptor. Service execution is bounded outside the BHL; broker updates and MMIO are locked. Asahi timestamps D589 reception with ktime_get and explicitly documents unknown slack between flip and callback (its HDMI heuristic is not J313 evidence). Its delayed DRM vblank is a transaction fallback, not a proven continuous panel source. D589 swap_data/swap_info remain unknown bytes in the examined primary implementations. D208 returns a DCP clock rate; D209 is UTC milliseconds, backed in current m1n1 by a CNTPCT/CNTFRQ UTC anchor. Neither yields retrace phase. DT mailbox IRQs427..430 and DART445 are not evidence of a panel VSync route.

The KMD fixed signal is inherited EXP495 mode2:2560x1600, total2642x1682, rounded pixel clock266630000. Its comment also records fixed16:16 refresh3932151. Current IOMFB mode selector preserves color/timing IDs but discards timing values. A rounded declared pixel clock and historical mode comment must not be substituted for a freshly verified active DCP timing descriptor. There is no source-supported physical phase in the current ABI.

## TRANSLATION:

Three separate states are required: pending(seq,address); active(last matched latch,address); timeline(mode,generation,timebase,phase,period,OS subscription). A vertical event cannot retire pending or publish its address. A matching real latch updates active exactly once. Duplicate/stale latches do not roll active back or fault a healthy timeline. IRQ W1C must acknowledge only captured bits; any new event identity must remain observable. Disable retains latch/recovery and active; enable can report the unchanged active surface without a new swap. Stop/reset must close ingress, cancel a source, synchronize ISR, drain queued/in-flight DPCs, and only then free runtime. ISR cannot allocate, wait or perform registry I/O. NotifyInterrupt belongs at synchronized interrupt IRQL; a timer DPC cannot simply call it as if it were a physical ISR. SynchronizeExecution is available at <=DISPATCH_LEVEL, but that establishes serialization, not a truthful physical retrace timestamp.

m1n1 owns DCP, physical latch, mappings, broker synthetic IRQ889 and any host physical time source; Mu owns resource exposure and reserve; KMD owns Windows pending/active state, subscription and callback/DPC lifetime; Windows owns scheduling and fence dependencies. Ordinary/hidden recovery remains an operator/launcher process with immutable artifacts and evidence gates.

A: no periodic DCP source is confirmed by the inspected implementations/specifications. B: a modeled timeline can preserve exact rational deadlines without accumulated rounding and can conservatively publish only confirmed active scanout, but its *physical* phase cannot currently be verified. Anchoring to D589 arrival establishes a software observation phase, not physical retrace. No production timer was added and no cap was changed to bypass this missing contract. A clarification was submitted about accepting that explicitly limited software model; absent an answer, the task's stricter physical-phase requirement remains in force.

## WHAT IS STILL UNKNOWN:

A trustworthy periodic DCP event or a measured/spec-defined relation between a monotonic counter, actual active timing and physical retrace is missing. Existing D589 payload semantics do not resolve it. This is the source gate before a truthful candidate; an arbitrary60Hz timer or an invented fixed D589 latency would violate the requested evidence order. Black-screen sufficiency is a separate unknown and is not inferred from the deterministic ISR defect.

## One ETL causality pass

Builder Windows10.0.26200 tracerpt processed1024 buffers/718307 total events with reported lost0. The retained original ETL contains only DxgKrnl and Kernel-EventTracing providers; its header duration914s does not mean914s of retained detail. Nonheader event UTC window is `00:14:43.432336..00:16:32.087030Z`. Circular overwrite excludes early lifecycle; reported lost0 does not restore overwritten events. UTC labels also do not substitute for a cross-clock correlation receipt.

The builder26200 provider metadata identifies138 VSyncWaiterChange events:69 count1 and69 count0, adapterffffd78e88150000/source0; caller5148 is Explorer, not DWM1220. No VSyncInterrupt, VSyncDPC, MMIOFlip task appears in this retained window. Five Present events are not evidence of DWM's early three callbacks. DWM1220 has6996 profiler events,170 immediate sync signals,170 GPU sync signals,170 GetDeviceState, five PresentHistory stops and exactly one CPU sync wait. That wait names deviceffff9e85f9cc27e0, monitored objectffffd78e7e1c0210/value7036. The tracked private KMD contextffffd78e83467100 does not appear in the retained records, and creation/mapping to the runtime context is absent. Event551 then signals the exact same monitored object/value7036 at clock134352872961068611, 33.4595ms after the event297 wait at134352872960734016; event300 follows109*100ns later. This excludes that particular late wait as a fence left unsignaled through the retained trace. No ticket connects it to early monitored fences37/45/51 or KMD fence31348. Therefore intercontext G4 branch7/9 remains a separate candidate, not a proven cause.

Proven: current source matches EXP907; periodic-notification implementation is absent; pending state is cleared on one latch; saved late waiter activity exists; current recovery is complete. Rejected: D589 is a continuous VSync source; callback S_OK or completed fences prove display; ETL lost0 proves a complete boot trace. Not visible: early enable/latch callbacks, exact private/runtime context identity mapping and an early Present-to-fence-to-flip ticket. Single next causal boundary: a truthful VSync source/timeline to Windows notification at preserved active address, before attributing missing Present to that contract.

## Executable RED and existing GREEN

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? OS notifications must continue without Present; enable after consumed latch must retain effective scanout; duplicate latch must not fault a healthy runtime.

`/Users/pavel/public_windows/.local/analysis/EXP907-vsync/replay.py` extracts the real current runtime/ISR/ControlInterrupt bodies, links the actual shared `apple_agx_scanout.c`, uses a minimal register transport and callback shim, and compiles with `-std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -DAPPLE_AGX_GPUVA_G3_QUALIFICATION`. No production timer or modeled replacement is used. RED is functional: after latch+three idle periods notification1 versus4; enable-after-latch notification0/pending-address0; duplicate latch Faulted1. Compiler and sanitizers pass; executable exits1 for these expected failures. The idle case demonstrates missing ingress rather than inventing a broker IRQ. This replay is an analysis artifact, not an intentionally failing committed suite.

Existing `test_apple_agx_scanout.py`3/3, `test_apple_agx_fixed_panel.py`1/1 and `test_apple_agx_render_scanout.py`6/6 pass via unittest. The available python lacks pytest; unittest was used directly. No implementation GREEN, WDK build, signed candidate, new experiment, visual success or repair is claimed. Analysis files, templates, CSV, RED output and current recovery receipt are indexed by `.local/analysis/EXP907-vsync/analysis-manifest.json`.
