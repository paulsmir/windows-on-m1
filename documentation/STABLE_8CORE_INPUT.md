# Validated J313 eight-core and native-input checkpoint

This document identifies the first public J313 checkpoint that exposes all
eight M1 CPU cores while retaining the validated built-in keyboard and Windows
Precision Touchpad stack. It was validated on a 2020 M1 MacBook Air on
2026-08-25.

## Validated scope

- Windows sees eight logical processors.
- Icestorm UIDs 0 through 3 use efficiency and scheduling class 0.
- Firestorm UIDs 4 through 7 use efficiency and scheduling class 1.
- Windows boots to the lock screen inside the 30-second hardware gate.
- The internal Apple keyboard and Precision Touchpad are published by the
  native `AppleInput` driver and remain usable together.
- The synthetic NVMe, physical USB, internal display, SSH and assisted
  framebuffer remained healthy during the bounded test.
- An eight-worker 20-second CPU load completed while six independent SSH
  probes returned in 0.72 to 1.26 seconds.
- After 6418 seconds of uptime Windows reported no new BugCheck, WHEA,
  stornvme, storage-reset or related System events.

This is an assisted-boot hardware checkpoint. It does not claim suspend,
hibernate, long-duration thermal stress, GPU acceleration, or a separately
qualified standalone cold boot.

## Exact source and artifacts

Use branch `feature/j313-4e4p-cpu-stability` in the root repository and Mu
submodule. The m1n1 submodule remains on `stable/j313-4e-baseline`. Always use
the submodule revisions pinned by the root commit; do not combine binaries from
different checkouts.

The validated topology source revisions are:

- root topology commit: `4375edd43c993d379e2a14438bf2202bc275b9eb`;
- Mu: `8b4dc4b4e3ff8606d0af36163acf9de79b7b4737`;
- m1n1: `9cd80ac652ac404e92ae279deeaec8c629d7d184`.

The hardware-tested assisted artifacts have these SHA-256 hashes:

| Artifact | SHA-256 |
| --- | --- |
| `m1n1.macho` | `3b81d82176b9853228b39eb3bb56ceff018cd0542248e872dd1bc1304c32b82e` |
| `J313_EFI.fd` | `4c5e068f664d8ccc94823880de4226e3f7842e08841bc10fea19cbe9e05a519b` |
| `boot.bin` | `6ab28c09ced56db4e03ad54d755d0f2caae76ca9ff97f2b9fe0d6e71fec5bc30` |

The exact experiment and recovery point are recorded as `EXP-20260825-072` in
[`investigation/EXPERIMENTS.md`](../investigation/EXPERIMENTS.md).

## Prepare the checkout

```sh
git fetch origin
git switch feature/j313-4e4p-cpu-stability
git pull --ff-only
git submodule update --init --recursive
git submodule status --recursive
```

Create the host environment once if it is not already present:

```sh
python3 -m venv proxyenv
proxyenv/bin/pip install -r m1n1_windows/requirements.txt
```

## Build the validated assisted profile

```sh
scripts/build-standalone.sh --debug-build --display both --debug monitor
python3 tools/artifact_manifest.py verify \
  dist/j313/debug-monitor/MANIFEST.json --profile debug
shasum -a 256 dist/j313/debug-monitor/m1n1.macho \
  dist/j313/debug-monitor/J313_EFI.fd \
  dist/j313/debug-monitor/boot.bin
```

The manifest must report eight guest CPUs, `display=both`, `debug=monitor`, and
the pinned component revisions. Stop if the hashes or manifest do not match the
checkout that was just built.

## Boot the exact validated configuration

Leave the Air in m1n1 `Running proxy...`, connect both USB ACM endpoints to the
development host, and run:

```sh
M1N1DEVICE=/dev/cu.PROXY \
M1N1VUART=/dev/cu.VUART \
scripts/run-assisted.sh \
  --proxy /dev/cu.PROXY \
  --vuart /dev/cu.VUART \
  --display both \
  --debug monitor \
  --chainload \
  --foreground
```

Replace the two placeholder device names with the actual proxy and vUART
devices from `ls -l /dev/cu.usbmodem*`. The wrapper chainloads the matching
m1n1 before launching the paired Mu firmware. Do not invoke a legacy
`run_uefi.py` from another checkout.

In two other terminals, start the observer pages:

```sh
scripts/log-assisted.sh
scripts/display-assisted.sh
```

Logs are served at `http://127.0.0.1:8765/` and the live guest framebuffer at
`http://127.0.0.1:8766/`.

## Install and verify native input

The firmware exposes `ACPI\\APPL0001\\0`; Windows still needs the test-signed
ARM64 `AppleInput` package. Follow the staged certificate, driver and publication
procedure in [`APPLE_INPUT.md`](APPLE_INPUT.md). The accepted state is:

```text
AppleInput service: RUNNING
ACPI\APPL0001\0: OK
TransportOnly=0
PublishKeyboard=1
PublishTrackpad=1
HID keyboard child: OK
HID-compliant touch pad child: OK
```

Keep external USB input available until those checks pass. The built-in
keyboard and trackpad package is installed inside Windows and is not embedded
in `boot.bin`.

## Standalone use

The same pinned sources can create a quiet physical-display image:

```sh
scripts/build-standalone.sh --release --display physical --debug off
python3 tools/artifact_manifest.py verify \
  dist/j313/release/MANIFEST.json --profile release
```

Install it only after inspecting and confirming the target ESP as documented in
[`INSTALL.md`](INSTALL.md):

```sh
sudo scripts/install-esp.sh install --disk diskXsY \
  --image dist/j313/release/boot.bin
```

This standalone image is built from the same eight-core source contract, but
the release profile requires its own cold-boot hardware qualification. Until
that gate is recorded, the exact reproducible stable reference is the assisted
`both/monitor` configuration above.

## Rollback

The previously published 4E+3P checkpoint is branch
`feature/j313-4e3p-cpu-stability`. Its exact local binary recovery directory is
`.local/recovery/EXP-20260825-071-4e3p/`. If the eighth core causes a regression,
stop the assisted guest and relaunch that preserved pair; do not rewrite the
ESP merely to compare CPU topology.
