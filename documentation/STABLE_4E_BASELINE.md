# Validated J313 four-efficiency-core baseline

This document freezes the first reproducible public configuration that completed
a fresh Windows 11 ARM64 first boot and reached a responsive desktop on a
MacBook Air (M1, J313) on 2026-08-23.

## Status

Use this configuration as the control before changing CPU topology, timer/vGIC
delivery, NVMe interrupt delivery, display transport, or Apple input support.
It passed the observed boot gate from Windows OOBE to a responsive desktop. The
operator reported normal interactive speed and no micro-freezes during the
initial session. This is an initial hardware baseline, not yet a long-duration
stress or suspend/resume qualification.

## Exact hardware contract

- Mu exposes GICC UIDs 0 through 3: the four Icestorm efficiency cores.
- Firestorm GICC UIDs 4 through 7 remain present but disabled in MADT.
- m1n1 starts all physical CPUs for EL2 operation; Windows is intentionally
  limited to the four enabled MADT entries.
- The guest uses the 1 GiB low-memory alias required by Windows boot.
- The physical 2560x1600 DCP surface is enabled.
- USB framebuffer publication and host telemetry are disabled at runtime.
- NVMe uses the proportional one-notification-per-assertion INTx state machine.

This topology is deliberate. Earlier mixed E/P experiments produced
`CLOCK_WATCHDOG_TIMEOUT`, long global pauses, and inconsistent secondary-core
startup. Do not enable a Firestorm entry in a release baseline until an isolated
one-P-core experiment passes the same boot and stability gates.

## Hardware-tested artifacts

The files are local release outputs and are not tracked by Git:

| Role | Path | SHA-256 |
|---|---|---|
| m1n1 Mach-O | `dist/j313/debug-forensic/m1n1.macho` | `0389bc92d88f1a19049cecc564b929502f7dbce2ab05942a7e6421bef24632c9` |
| Mu firmware | `dist/j313/debug-forensic/J313_EFI.fd` | `8d95d77664346ceb95bbe7a1fca493cc1b1e876fc1acf627c385191fe4df268a` |

Source checkpoints after recording the working tree:

- `m1n1_windows`: `2fe790beebed32658eae753dee3e6d581df97197`
- `apple_silicon_platforms_mu`: `af4c9705cfd42e976bc9602c35830cc2e9072f36`
- branch in all repositories: `stable/j313-4e-baseline`

The tested m1n1 Mach-O identifies itself as `9bc8b33-dirty`: the dirty source
was host-side compatibility code and did not alter the target C image. The Mu
firmware includes the MADT change later committed as `af4c9705cf`.

## Exact assisted launch

Start from the Stage 1 `Running proxy...` state. Chainload the paired m1n1:

```sh
M1N1DEVICE=<proxy-port> \
  ./proxyenv/bin/python \
  m1n1_windows/proxyclient/tools/chainload.py \
  dist/j313/debug-forensic/m1n1.macho
```

Then launch the paired Mu firmware with only the internal display consumer:

```sh
./proxyenv/bin/python -u run_uefi.py \
  dist/j313/debug-forensic/J313_EFI.fd \
  --device <proxy-port> \
  --display-mode physical \
  --debug-mode off \
  --low-mem
```

The successful run reported `preflight validation: OK`, completed all four
launch-contract checkpoints, entered Windows user space on CPUs 0 through 3,
completed OOBE, and reached a responsive desktop.

## Known limitation of this artifact

Although runtime `debug-mode` is off, the tested m1n1 file was compiled from a
diagnostic target tree and still contains build-time UART diagnostics. It was
stable in this hardware run, but the next release task is to produce a quiet
binary from the committed source and prove it against this baseline. Until that
hardware comparison passes, do not replace the hashes above or call a different
`dist` directory the validated baseline.

## Regression rule

Every later hardware experiment must record its exact m1n1 and Mu hashes and
compare against this baseline. If boot exceeds 30 seconds, Windows does not
reach the interactive desktop, a global pause occurs, or a watchdog bugcheck is
observed, revert the single experimental variable before attempting another
change.
