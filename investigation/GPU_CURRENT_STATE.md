# GPU current state

Updated: 2026-09-04T15:13:00+02:00

## CURRENT PLATFORM

- Live J313 is back on the normal current G2 pair: accepted EXP377 m1n1
  `fae3444cc289cf52ea12b81b9db8f3d8bf24bd084f899a751321d2048d9a525a`
  plus Mu `16c177182e96b63eac852dcfb185cebba9c1d91943c6402106a640848ddc5e06`.
- Current-compatible emergency non-AGX Mu remains
  `279bd36ad3bbb1ee5e2393fa965343ea856b4c2b0dd4df2b2add6a8010e3f32c`.

## CURRENT WINDOWS BASELINE / PACKAGE STATE

- Fresh post-EXP412 current-G2 check: APPL0002 Code28/unbound with null
  INF/service; no project Display package, AppleAgx service, SYS or loaded
  module; SSH, 8 CPUs, AppleInput, stornvme, USBXHCI and sshd healthy.
  Two stornvme Event129 records 6528/6529 occurred before the EXP412 bind
  during synthetic-platform boot; no new 41/129/1001 occurred after bind,
  through exact cleanup, or after ordinary-G2 restoration.

## HARDWARE PROVEN

OBSERVED:
- EXP404 natural current-G2 bind produced APPL0002 Code0, exact running service
  and 2560x1600x32 Windows video controller.
- StartDevice stage 7, POST ownership, one source/target, child/topology,
  VidPn enumeration/commit/visibility, pointer and PresentDisplayOnly all
  recorded SUCCESS.
- Code0, exact package and all system health remained stable through a separate
  180-second post-bind window with no 41/129/1001.
- EXP404 runner never enabled AGX routes 880/881 and had no watchdog/reset.

INTERPRETATION:
- Registering the inert ISR/DPC pair made Dxgkrnl connect/unmask current-G2
  level AGX IRQs and caused the earlier cumulative-DPC watchdogs.
- A synchronous no-VSync KMDOD must leave ISR/DPC unregistered until a real AGX
  status/ack/completion handler exists.
- EXP403 also proved SystemDisplay callbacks cannot perform registry I/O on the
  any-IRQL bugcheck path; EXP404 contains that correction.
- EXP412 hardware-proved the complete production memory qualification seam:
  Windows DXGK physical objects/ADLs/maps, 70 real HVC 0x4d31 batches through
  current m1n1, 4152 translated sub-40-bit host pages, context-63 four-page
  16-KiB UAT, one exact 16-MiB mapping at GPU VA 0x1500000000, gpu-region TTBR
  publication/readback, deterministic first/last leaf translation and reverse
  cleanup. Its 128-byte proof has StartStage 10 and both statuses zero.
- Therefore MEMORY_PAGING_IMPLEMENTED=YES and
  MEMORY_PAGING_HW_PROVEN=YES. BuildPagingBuffer encode/worker/DMA completion
  remain implementation-proven; they were not separately exercised by the
  qualification branch.

## FIRST UNKNOWN / FAILED BOUNDARY

Memory hardware qualification is closed by EXP412. Commits `4d539ea`,
`421a1ac` and `ec214ae` now provide the offline-green one-node substrate:
one monotonic queued/active/completed interval, exact active-fence completion,
queued-work removal at DMA-buffer-boundary preemption, dispatch blocking until
one preemption notification, and reset reporting the active fence or completed
boundary while clearing outstanding work. The first unknown is now connection
of a real non-paging render packet and backend completion to that interval.
No enqueue-only completion, AGX queue, physical IRQ or capability was added.

## LAST KNOWN GOOD FOR THIS BOUNDARY

- EXP404 source/test composite / SYS:
  `1fe3c34ec6da571b89fe7aaa8524f89dd57cdfa0de04d548ff83375616315468` /
  `9a4b43a6de0c7422324daf2f8a193c24363209389b5c47417cbeba545cc9b139`.
- Phase-B / health / display inventory evidence SHA-256:
  `37a4fc5f4ad082153204ed96f24f3a870f0cb4205e64db1b4a10240324efecb7` /
  `e2c9c0d359582b5b06fce9d6f7954f96455edd5ce7965595d7e8acdeb33a7096` /
  `9e92b5f3ae3fe5c265e3182861286e68a5841a156b01e7e78eeba824ae776b2e`.
- EXP214 remains the byte-exact Full Graphics build/admission control:
  source/test composite
  `25718ba071971c8cb94a6847908f0a722ffdb4f6767b9ca4d548514ea6713d63`.

## REJECTED / DO NOT REUSE

- EXP398–EXP412 package identities are terminal and clean.
- Explicit receipt flush removal alone is rejected; inert IRQ registration is
  the confirmed watchdog cause. Do not register ISR/DPC/ControlInterrupt before
  implementing the real AGX interrupt contract.
- Do not restore platform-version overrides, old recovery binaries, or disable
  Defender/WdFilter.

## NEXT CANDIDATE

- Continue the committed EXP407 mandatory-feature vertical-slice plan at
  render fence/progress while preserving the EXP406 coherent vector and
  synthetic-only IRQ platform for any later admission hardware run.
- The atomic readiness gate is implemented and wired into the real Type1 path
  in commit `52d3bf6`: the current 4/14 state publishes zero mandatory caps and
  an accidental premature all-ready state fails closed. All later layers must
  earn their readiness bit through deterministic tests; the complete
  capability writer is installed only at 14/14.
- Typed nonpaged device/context ownership is implemented and pinned-WDK ARM64
  KMD+UMD build, analysis, Universal validation, Inf2Cat and signing pass with
  zero warnings/errors. This offline package is not an EXP407 hardware
  candidate and must not be staged.
- Segment1/Segment2 translation and allocation contracts are implemented in
  commits `32cd23e` and `1e76707`: 4-KiB software aperture, 64-KiB local
  GPU-VA segment, 16-KiB UAT prerequisite, paging plans, gated QuerySegment4,
  and standard/create/destroy/describe/open/close allocation DDIs. The
  functional memory readiness bit is now true; EXP412 supplies its required
  physical-owner/HVC/UAT hardware proof while BuildPagingBuffer remains an
  explicit later Windows-driven exercise.
- `MEMORY_PAGING_IMPLEMENTED=YES` and
  `MEMORY_PAGING_HW_PROVEN=YES`. EXP411 first proved physical-owner/HVC/local
  allocation and isolated a false CPU-VA 16-KiB alignment guard at UAT stage 5.
  Commit `4779f02` retained physical 16-KiB alignment while allowing a
  naturally aligned kernel mapping; EXP412 then reached stage 10 with exact
  PA/UAT/TTBR/readback/cleanup proof. Exact `oem5.inf` cleanup and stale
  APPL0002 devnode removal restored the package-free ordinary-G2 baseline.
- One-node lifecycle and paging-backed fence primitives exist in
  `3df8e82` and the unified progress/preemption/reset substrate exists in
  `4d539ea`, `421a1ac` and `ec214ae`. The current functional readiness
  mask is 4/14
  (WDDM3 identity, one-node topology, memory/paging, device/context).
  SCHEDULER, DMA_BOUNDARY_PREEMPTION and PER_ENGINE_TDR remain false until a
  real non-paging backend publishes/completes work and supplies quiesce and
  responsiveness evidence. Do not infer readiness merely from registered
  callbacks or pure state transitions.
- Next implementation boundary: GDI/RenderKm immutable command admission and
  patch records feeding the common render interval, followed by the retained
  owner scanout and existing AGX backend vertical slice. Continue to publish
  zero mandatory Type1 caps until the complete 14/14 group is real.
- Reuse current shared allocation/context/paging/scheduler/GDI/backend pieces,
  current m1n1's hardware-proven retained DCP owner and the EXP208 graph. Do not
  bind hardware until the entire mandatory group is real and offline-green.
