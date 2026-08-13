# Windows launch profiles

Windows-on-M1 exposes three independent launch decisions: how the guest starts, where its
framebuffer is consumed, and how much diagnostic work runs beside it. Keeping these decisions
orthogonal makes a quiet standalone boot possible without removing the assisted development
path.

## Profile axes

### Execution

- `standalone` starts the firmware embedded in the installed `boot.bin` after the bounded
  maintenance window. It does not require a debugging host.
- `assisted` chainloads the selected m1n1 build from a host and starts a replaceable Mu firmware
  image through `run_uefi.py`.

### Display

- `physical` attaches the guest BGRA framebuffer to the internal J313 DCP scanout.
- `virtual` publishes the guest framebuffer through the asynchronous USB stream for the web
  viewer.
- `both` enables the physical DCP scanout and asynchronous USB stream for the same framebuffer.
- `none` keeps the guest GOP framebuffer allocated but enables neither consumer. This supports
  headless Windows and RDP without removing the firmware display contract.

Mu and Windows always use one reserved 2560x1600, 10240-byte-stride,
`PixelBlueGreenRedReserved8BitPerColor` framebuffer. Physical display support maps this buffer as
the DCP surface; it does not expose iBoot's possibly 30-bit framebuffer to Windows and does not
copy complete frames in EL2. Virtual display support reads the same guest buffer. Therefore
`both` does not add a second guest framebuffer or a frame-copy loop.

An explicitly requested `physical` or `both` profile is strict. If DCP cannot map or present the
guest surface, m1n1 must report the failing stage and remain recoverable through its proxy
instead of silently changing the requested profile.

### Debug

- `off` disables the virtual-UART reader, persistent host logs, telemetry polling, KD helpers,
  and optional framebuffer transport not requested by the display profile.
- `uart` records the host/hypervisor output and opens the guest virtual UART before Mu starts,
  producing `hv.log`, `guest-uart.log`, and `guest-uart.tlog`.
- `full` includes `uart` and enables hang telemetry plus the diagnostic hooks used by the KD and
  device bring-up tools.
- `monitor` is a standalone-only diagnostic profile. It exposes the m1n1 console and guest
  virtual UART over USB, but it disables proxy takeover and telemetry. A `debug=monitor` image
  always starts Windows even when the recording host already has both USB endpoints open.

Display and debug remain independent. For example, `--display virtual --debug off` still sends
frames because the user requested a virtual display, but it does not run telemetry or retain
text logs.

## User interface

The host-assisted entry point is:

```sh
scripts/run-windows.sh \
  --execution assisted \
  --display both \
  --debug full \
  --chainload
```

`run-windows.sh --execution assisted` chainloads the matching profile by
default. This fail-closed behavior prevents a Mu image from being launched on
top of an unknown or stale m1n1 that merely happens to be waiting in the proxy.
Use `--reuse-proxy` only for an intentional fast iteration after independently
verifying that the running m1n1 came from the same artifact manifest. The
`--chainload` and `--reuse-proxy` options are mutually exclusive.

A low-overhead assisted launch is:

```sh
scripts/run-windows.sh \
  --execution assisted \
  --display physical \
  --debug off \
  --chainload
```

The installed image defaults to:

```text
execution=standalone
display=physical
debug=off
```

Standalone display and debug choices are encoded in the packed-image manifest by the build
command:

```sh
scripts/build-standalone.sh --display physical --debug off
scripts/build-standalone.sh --debug-build --display physical --debug monitor
```

Profiles that require an attached host remain explicit and never become a hidden runtime
dependency of a normal power-on boot.

`monitor` is the exception to the legacy maintenance-window behavior: it services USB once,
never enters the proxy loop, and proceeds directly into the same autonomous guest launch. This
makes it suitable for observing a cold-boot reset without changing the timing by waiting three
seconds or handing control to `uartproxy_run()`.

`--dry-run` resolves and prints the complete profile without touching USB, starting a guest, or
writing the ESP. Invalid combinations are rejected before any target state changes.

## Runtime ordering

For assisted mode the launcher performs these stages:

1. Parse and validate the complete profile.
2. Discover or validate the proxy and virtual-UART endpoints required by that profile.
3. With `--chainload`, load `dist/j313/debug/m1n1.macho` and wait for USB re-enumeration.
4. Open the virtual UART only for `uart` or `full`.
5. Construct the guest boot arguments for the shared BGRA framebuffer.
6. Ask m1n1 to attach the framebuffer to DCP for `physical` or `both`.
7. Enable asynchronous framebuffer events for `virtual` or `both`.
8. Enable telemetry and full diagnostics only for `full`.
9. Enter Mu and Windows.

Standalone mode consumes the same validated display/debug values from its manifest before guest
entry. Physical-display preparation happens before the final boot arguments are handed to Mu.
On J313, the GOP source rectangle matches the native 2560x1600 internal timing rectangle. This
is required for the Windows Basic Display handoff; advertising a smaller GOP mode allowed
winload output but produced a black display after the installed OS took ownership. The web
viewer consumes the same native framebuffer.

## Performance and failure rules

- `debug=off` must not open the virtual-UART endpoint, poll telemetry, or write continuously
  growing log files.
- `debug=monitor` may expose console and vUART, but host presence must never hold guest entry or
  transfer control to the proxy.
- Monitor diagnostics may use verbose synchronous USB logging. USB backpressure from an endpoint
  that is not being drained can perturb timing, so the production profile is required for normal
  performance and stability measurements.
- `physical` must not enable USB framebuffer events.
- `virtual` must not initialize or reconfigure DCP for guest scanout.
- `both` uses one buffer and enables two consumers; it must not mirror frames in software.
- USB backpressure may skip a virtual frame but must never pause Windows or the physical DCP
  scanout.
- A physical handoff failure aborts before guest entry and leaves proxy recovery available.
- A missing optional web viewer does not stop a guest whose framebuffer stream is otherwise
  valid.

## Verification contract

Host tests cover profile parsing, defaults, rejected combinations, dry-run behavior, manifest
flags, DCP surface validation, strict physical failure, and the absence of debug workers in the
quiet profile. Hardware acceptance requires all four display profiles, both assisted debug
extremes, standalone physical boot, correct colors and geometry on the internal panel, live web
frames in `both`, and an extended `physical + off` run without USB diagnostic load.
