# Assisted debugging

For the current SMP freeze baseline, artifact identities, capture rules, and the assisted-to-
standalone promotion gate, read [Platform stability](PLATFORM_STABILITY.md) first.

The optional second Mac exposes four independent observations: hypervisor log, virtual UART,
virtual framebuffer, and Windows KD. Treat them as separate signals. Losing one does not
prove that the target or the other transports stopped.

## Choose the observation level

- Use the production profile (`display=physical`, `debug=off`) for ordinary autonomous boot and
  performance measurements. It exposes no USB debug stream.
- Use standalone monitor (`display=physical`, `debug=monitor`) when the failure exists only in
  the installed cold-boot path. It records console/vUART but never accepts proxy takeover.
- Use assisted full mode (`display=both`, `debug=full`) while developing firmware, ACPI, virtual
  devices, or Windows drivers. It provides the widest set of independent observations and makes
  it easy to chainload a replacement without rewriting the ESP.

Monitor output is diagnostic overhead. The current hypervisor can produce verbose synchronous
USB logging; if the host is not draining it, USB backpressure can add latency visible inside the
guest. A temporary UI pause while using monitor mode does not prove that Windows crashed. Repeat
performance or stability conclusions with the production profile.

## Autonomous reset capture

Assisted mode owns the proxy and therefore changes the startup path. To observe a failure that
only occurs from the installed `boot.bin`, build `debug=monitor` and start the passive recorder
before cold power-on:

```sh
scripts/build-standalone.sh --display physical --debug monitor
scripts/log-standalone.sh --output standalone-monitor-logs
```

Unlike `debug=uart` and `debug=full`, monitor mode never enters the proxy loop when a host is
connected. It always starts Windows after initializing the two USB ACM channels. The recorder
does not issue proxy commands and never writes to either serial endpoint.

It is valid to attach after Windows has started:

```sh
scripts/log-standalone.sh --output standalone-monitor-logs-late
```

This captures future messages only. Start before cold power-on when boot-stage checkpoints or the
first reset boundary matter.

Each USB lifetime is stored separately:

```text
standalone-monitor-logs/
  generation-001/
    console.raw
    console.tlog
    vuart.raw
    vuart.tlog
    events.log
  generation-002/
    ...
```

The raw files preserve exact bytes. The `.tlog` files add host UTC timestamps, while
`events.log` identifies open, disconnect, and close boundaries. A new generation proves USB
re-enumeration; it does not by itself distinguish a guest-requested reboot from an EL2 exception
or platform reset. Compare the final console and vUART lines immediately before the boundary.

The monitor flag is part of the packed manifest ABI. The packer, installed m1n1 parser, and host
recorder must come from matching repository revisions. An older target correctly rejects the
new flag rather than guessing its meaning.

## Port ownership

List current endpoints after every target reboot:

```sh
ls -l /dev/cu.usbmodem*
```

Pass the selected endpoints to public scripts or set `M1N1DEVICE` and `M1N1VUART`. Device
names change across machines and re-enumeration. There must be one proxy owner: normally the
single `run_uefi.py` event loop. Opening another proxy client while it runs can interleave
replies, produce command mismatches, or starve framebuffer events.

The virtual UART is different: Mu console readers and KD tools use it at different boot
phases, but two simultaneous consumers still split bytes unpredictably.

## Live logs

Start the browser log viewer:

```sh
scripts/log-assisted.sh
```

It serves `hv.log` at `http://127.0.0.1:8765/` and follows file replacement on a new run.
`guest-uart.log` contains firmware or KDCOM traffic. `guest-uart.tlog` adds host timestamps,
which remain meaningful when guest timers are the object of investigation.

If the browser repeats old lines, compare inode, size, and timestamps on `hv.log`; do not
assume repeating UI text is new target output.

## Virtual framebuffer

Start the framebuffer viewer:

```sh
scripts/display-assisted.sh
```

The server publishes metadata, the most recent complete B8G8R8X8 frame, CRC32, generation,
frame rate, and frame age at `http://127.0.0.1:8766/`.

Interpret frame rate carefully:

- positive frame rate proves new complete framebuffer generations arrive;
- zero frame rate can mean a static screen, proxy backpressure, a dead observer, or a hung
  guest;
- a moving hardware mouse cursor can be independent of GDI progress;
- the last good frame intentionally remains visible when the link becomes stale.

Use KD liveness and EL2 counters before classifying a zero frame rate as a Windows hang.

Save the most recent complete generation as a PNG:

```sh
extra/screenshot.sh screenshots/guest.png
```

The helper rechecks metadata and CRC before publishing the output, so it never encodes a
partially replaced frame.

## Matching target and proxy binaries

`m1n1.proxy.ProxyCommandError: Reply error: Bad Command` means the target does not implement
the opcode requested by the host-side Python code. Chainload `dist/j313/m1n1.macho` built
from the same checkout, wait for re-enumeration, and then start the guest.

A timeout on the first NOP usually means the wrong endpoint, a stale target, or another
process already owns the proxy. It is not evidence that the firmware image is bad.

## Windows KD workflow

Configure the virtual UART endpoint before running a KD utility:

```sh
export M1N1VUART=/dev/cu.VUART
```

All host-side KD tools live in `tools/kd/`. They are intentionally small and build-specific:

- `tools/kd/kd_liveness.py` — break in, prove the kernel responds, and always continue;
- `tools/kd/kd_devnodes.py` — walk PnP devnodes and show state, problem code, and service;
- `tools/kd/kd_modules.py` — list loaded modules such as `pci.sys`, `stornvme.sys`, and USB drivers;
- `tools/kd/kd_acpi.py` — read the live RSDP/XSDT/table chain and validate MCFG/ECAM exposure;
- `tools/kd/kd_proclist.py` — enumerate processes from `PsActiveProcessHead`;
- `tools/kd/kd_threads.py` and `tools/kd/kd_stack.py` — inspect wait state and kernel stacks;
- `tools/kd/kd_peek.py` — read a selected physical address;
- `tools/kd/kd_wait_bugcheck.py` — decode bugcheck state-change packets;
- `tools/kd/kd_continue.py` — resume a target already stopped in KD;
- `tools/kd/kd_reboot.py` — request a Windows reboot without forcing Recovery;
- `tools/kd/kd_diag.py` — diagnose KDCOM ACK/resend behavior;
- `tools/kd/kd_watchdog.py` — bound diagnostics so a failed script does not leave Windows paused.

Run through the repository virtual environment when it contains `pyserial`:

```sh
proxyenv/bin/python tools/kd/kd_liveness.py
proxyenv/bin/python tools/kd/kd_devnodes.py
proxyenv/bin/python tools/kd/kd_reboot.py
```

For a repeatable driver-development session, use assisted full mode:

```sh
scripts/run-windows.sh \
  --execution assisted \
  --display both \
  --debug full \
  --chainload \
  --proxy /dev/cu.PROXY \
  --vuart /dev/cu.VUART
export M1N1VUART=/dev/cu.VUART
proxyenv/bin/python tools/kd/kd_liveness.py
```

Replace the two device names with the current pair reported by `ls -l /dev/cu.usbmodem*`.
Only one process may read the vUART at a time: stop `scripts/log-standalone.sh` before attaching a
KD helper to the same endpoint.

Every inspection that breaks into the kernel must resume it on success, error, and timeout.
A paused KD target looks exactly like a frozen Windows UI from the framebuffer side.

## Storage diagnosis

The expected device path is an ACPI PCI root followed by a synthetic NVMe endpoint bound to
`stornvme`. Useful evidence includes:

```text
ACPI\PNP0A08 ... service=pci
PCI\VEN_1B36&DEV_0010 ... service=stornvme
```

If the PCI endpoint is absent, inspect ECAM hook ordering and live ACPI. If it exists with
`FAILED_START`, inspect controller-ready transitions, queue IPA translation, completion
creation, and interrupt injection. If it starts but I/O stalls, correlate SQ doorbells,
physical ANS completion, CQE write, and guest interrupt/EOI.

## USB diagnosis

Separate firmware enumeration from Windows ownership:

1. Mu sees the hub/device: PHY, power, xHCI, and firmware DART setup are working.
2. Windows creates the ACPI xHCI devnode and loads USBXHCI: ACPI resources are accepted.
3. Interrupt counters and device changes continue: AIC-to-vGIC routing and level semantics
   work after `ExitBootServices`.
4. HID input works: hub and HID class stacks completed enumeration.

A flashing USB drive proves bus traffic but not keyboard interrupt delivery.

## Hang triage

Use this order:

1. Does `hv.log` continue receiving new EL2 samples or counters?
2. Does `tools/kd/kd_liveness.py` break in and continue the target?
3. Does the virtual framebuffer generation change?
4. Do NVMe/xHCI interrupt and completion counters advance?
5. Is Windows intentionally stopped at a bugcheck or KD break?

Classify findings rather than guessing:

- **observer failure:** KD and hardware counters advance but framebuffer/proxy output stalls;
- **device stall:** kernel responds but one queue/interrupt path stops;
- **guest deadlock/watchdog:** KD reports stuck cores or Windows emits
  `CLOCK_WATCHDOG_TIMEOUT`/`IPI_WATCHDOG_TIMEOUT`;
- **proxy loss:** all host observations stop while physical USB input or display still changes;
- **target reset:** USB endpoints disappear and later re-enumerate.

## Safe reboot and recovery

Use `tools/kd/kd_reboot.py` when Windows responds. Use `scripts/reset-assisted.sh` only from the UEFI
shell. A hard power-off can send the next Windows boot into Recovery. Killing the host
runner is not a target reset and can leave the protocol unusable until a physical reboot.
