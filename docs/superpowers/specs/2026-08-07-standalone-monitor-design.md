# Standalone USB Monitor Design

## Purpose

Capture the complete cold standalone boot over USB without allowing an attached
development host to divert the target into the interactive proxy loop.  The
capture must cover m1n1/EL2 output and the guest Mu/Windows virtual UART through
the reset that currently occurs shortly after the Windows logo appears.

This is a diagnostic milestone.  Its evidence will be used to replace the
duplicated assisted and autonomous orchestration with a shared C guest-boot
engine in the following milestone.

## Launch profile

Add `monitor` as a fourth debug value beside `off`, `uart`, and `full`.
`monitor` receives a distinct manifest flag so existing modes retain their
current behavior:

- initialize the platform USB hardware and m1n1 USB iodevs;
- expose the m1n1 console and guest virtual-UART ACM endpoints;
- start the standalone guest automatically even when a host has opened USB;
- never transfer control to `uartproxy_run()`;
- omit framebuffer streaming and periodic telemetry unless the display profile
  independently requests the virtual display.

Old binaries continue to reject the new flag as unknown rather than silently
using the wrong behavior.  A monitor image and its matching m1n1 binary are
therefore distributed together, as all standalone images already are.

## Target data flow

1. iBoot enters the packed m1n1 image.
2. m1n1 validates the manifest and decodes `debug=monitor`.
3. m1n1 initializes Type-C/DRD power and both USB debug endpoints.
4. USB connection state is serviced for output but ignored as a proxy-takeover
   command.
5. The autonomous guest preparation starts immediately; it does not wait for a
   host and does not use a fixed timing delay.
6. m1n1 and guest-vUART output remain available while Mu and Windows run.
7. A guest reset, hypervisor exception, or USB re-enumeration is recorded by the
   host logger before it reopens the endpoints for the next generation.

## Host capture tool

Add a standalone monitor wrapper that can be started before powering on the
target.  It will:

- accept explicit console and vUART device paths;
- otherwise wait for and group the newly appearing m1n1 ACM endpoints;
- reject ambiguous endpoint sets instead of guessing;
- write raw logs and host-timestamped logs for both channels;
- mark disconnect/reconnect generations and continue waiting across a reboot;
- never send proxy requests or bytes to the target.

The wrapper will reuse the existing UART reader where its reconnect and raw-log
contracts are sufficient.  Endpoint discovery will use serial metadata rather
than a developer-specific device name.

## Compatibility

- `debug=off` remains the production profile with no debug transport.
- `debug=uart` and `debug=full` retain interactive proxy takeover.
- assisted launch behavior is unchanged.
- display selection remains independent; the immediate diagnostic build uses
  `display=physical`.

## Failure behavior

Manifest/profile validation fails before guest setup on unknown or conflicting
flags.  Monitor endpoint ambiguity fails on the host with the discovered device
metadata printed.  USB disconnect does not terminate capture; it closes the
current generation and waits for re-enumeration.  Existing log files are never
silently overwritten without a new run directory or explicit user choice.

## Verification

Automated verification covers:

- Python profile encode/decode round trips for `monitor`;
- C manifest and profile decoding of the new flag;
- a connected host selecting guest launch rather than proxy takeover in monitor
  mode;
- unchanged proxy takeover for `uart` and `full`;
- host endpoint discovery, ambiguity, and reconnect-generation behavior;
- the complete existing host-test suite and standalone image validation.

Hardware verification uses a cold boot with the monitor host already listening.
Success for this milestone is not a successful Windows desktop: it is an
uninterrupted pair of logs that identifies the last hypervisor and guest events
before the observed Windows reset.

## Follow-up architecture

After the trace identifies the remaining assisted/standalone divergence, the
next milestone will introduce a bootstrap/self-chainload stage and a shared C
guest-boot engine.  Proxy RPC handlers and standalone manifest launch will call
the same engine so memory layout, ADT construction, stage-2 mapping, vGIC,
PCI/NVMe, xHCI/DART, display handoff, and guest entry have one implementation and
one ordering contract.
