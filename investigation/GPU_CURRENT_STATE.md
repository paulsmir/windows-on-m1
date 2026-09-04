# GPU current state

Updated: 2026-09-04T13:33:00+02:00

## CURRENT PLATFORM

- Live J313 is back on the normal current G2 pair: accepted EXP377 m1n1
  `fae3444cc289cf52ea12b81b9db8f3d8bf24bd084f899a751321d2048d9a525a`
  plus Mu `16c177182e96b63eac852dcfb185cebba9c1d91943c6402106a640848ddc5e06`.
- Current-compatible emergency non-AGX Mu remains
  `279bd36ad3bbb1ee5e2393fa965343ea856b4c2b0dd4df2b2add6a8010e3f32c`.

## CURRENT WINDOWS BASELINE / PACKAGE STATE

- Fresh post-EXP406 current-G2 check: APPL0002 Code28/unbound; no project Display
  package, AppleAgx service or SYS; SSH, 8 CPUs, AppleInput, stornvme, USBXHCI
  and sshd healthy; no new 41/129/1001.

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

## FIRST UNKNOWN / FAILED BOUNDARY

EXP406 fixed the EXP405 `DxgkInitialize` revision mismatch. Exact receipts prove
DriverEntry and DxgkInitialize success followed by natural AddDevice/StartDevice
and a successful Type 1 QueryAdapterInfo of 592 bytes. Dxgkrnl then removed the
adapter: APPL0002 Code43, service stopped, no GPU LUID and no Type34/35/VidPn
receipt. Official Microsoft Full Graphics requirements resolve the immediate
cause: EXP406 intentionally leaves mandatory preemption, FlipOnVSyncMmIo,
per-engine TDR, DirectFlip/independent-flip and GDI kernel-command-buffer
contracts unimplemented and therefore unadvertised. The first unknown is now
implementation of that atomic mandatory-feature vertical slice; another
individual Type1 bit experiment is prohibited.

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

- EXP398–EXP406 package identities are terminal and clean.
- Explicit receipt flush removal alone is rejected; inert IRQ registration is
  the confirmed watchdog cause. Do not register ISR/DPC/ControlInterrupt before
  implementing the real AGX interrupt contract.
- Do not restore platform-version overrides, old recovery binaries, or disable
  Defender/WdFilter.

## NEXT CANDIDATE

- Execute the committed EXP407 mandatory-feature vertical-slice plan while
  preserving the EXP406 coherent vector and synthetic-only IRQ platform.
- The atomic readiness gate is implemented and wired into the real Type1 path
  in commit `52d3bf6`: the current 3/14 state publishes zero mandatory caps and
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
  and standard/create/destroy/describe/open/close allocation DDIs. The full
  memory readiness bit remains false until DXGK physical-memory/HVC ownership,
  context-63 UAT publication and BuildPagingBuffer execution are connected.
- `MEMORY_PAGING_IMPLEMENTED=YES` in commits `8cd1449`, `3380434`
  and `39f64c6`: DXGK physical owner/ADL/map, bounded HVC 0x4d31, 40-bit host
  pages, aligned local object, context-63 16-KiB UAT publication,
  BuildPagingBuffer execution and paging-only interrupt/DPC completion form one
  reverse-cleaned lifetime. `MEMORY_PAGING_HW_PROVEN=NO`: no live Windows-to-
  m1n1 HVC/host-PA/context-63-UAT result exists yet. EXP408 selected exact
  `oem5.inf` and stayed healthy but produced no binary memory proof before
  Code43/Remove; it is inconclusive, not HVC evidence. Exact cleanup restored
  a package/service/SYS-free unbound APPL0002. The next allowed run changes
  only durable production memory-start stage/status capture. Functional
  readiness is 3/14, not hardware readiness.
- Reuse current shared allocation/context/paging/scheduler/GDI/backend pieces,
  current m1n1's hardware-proven retained DCP owner and the EXP208 graph. Do not
  bind hardware until the entire mandatory group is real and offline-green.
