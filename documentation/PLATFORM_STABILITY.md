# J313 Platform Stability Checkpoint

## Current result

Windows 11 ARM64 boots from the internal NVMe namespace on all four efficiency and all four
performance cores. The physical panel, external USB input, networking, SSH, and RDP have all
worked on the development J313. The current checkpoint reduced early watchdog crashes and made
firmware startup markedly faster.

The system is not yet stable. A quiet physical-only standalone image still intermittently stops
responding for about 20 seconds: the pointer, UI, and remote session make no progress and then
recover. Because the same symptom occurs with `debug=off` and no virtual framebuffer stream,
diagnostic printing and assisted display traffic are not the root cause by themselves.

The accepted milestone remains the Phase 0 gate in [ROADMAP.md](ROADMAP.md). Do not describe this
checkpoint as freeze-free or production-ready.

## Checkpointed m1n1 work

The `m1n1_windows` checkpoint contains:

- safe fixed performance-state initialization for both T8103 clusters;
- a fast path for ordinary Windows breakpoint exception lowering;
- a dedicated NVMe fast path and lock so synchronous ANS I/O does not hold the global hypervisor
  lock for the complete operation;
- secondary-CPU fast handling for guest timer and local-interrupt FIQ work;
- reduction of the synthetic secondary EL2 tick from 5000 Hz to 100 Hz;
- manifest-controlled runtime diagnostics, including a quiet profile;
- lockless per-CPU watchdog snapshots and host tests for the new policy helpers.

Observed hardware evidence includes all eight CPUs entering Windows, an FIQ rate reduction from
roughly 40,000/s to roughly 11,500/s, and removal of the approximately 5,000/s private synthetic
tick. Interrupt load remains uneven, and a long global pause remains reproducible.

## Preserve the standalone baseline

Before an experiment, record the exact root, m1n1, and Mu commits plus the hashes of the installed
and candidate images. Store raw output below `.local/`, which is intentionally not published.

```sh
mkdir -p .local/platform-stability/baseline
git rev-parse HEAD
git -C m1n1_windows rev-parse HEAD
git -C mu rev-parse HEAD
shasum -a 256 dist/j313/boot.bin
```

On the target, keep the ESP backup created by `scripts/install-esp.sh inspect`. Do not overwrite the
only known-good recovery payload.

## Fast assisted iteration over USB

Assisted mode avoids rewriting the target ESP for each hypothesis. It requires another Apple
Silicon Mac only for development; normal standalone boot does not.

On the host, stop any previous launcher, connect the debug USB cable, and identify the two current
ACM endpoints:

```sh
cd /path/to/windows-on-m1
ls -l /dev/cu.usbmodem*
```

Build the candidate from the pinned root, m1n1, and Mu commits, then use one wrapper command to
chainload matching m1n1 and launch Mu/Windows:

```sh
scripts/run-windows.sh \
  --execution assisted \
  --display physical \
  --debug full \
  --chainload \
  --proxy /dev/cu.PROXY \
  --vuart /dev/cu.VUART
```

For independent framebuffer evidence, use `--display both` and start:

```sh
scripts/log-assisted.sh
scripts/display-assisted.sh
```

The log viewer is at `http://127.0.0.1:8765/`; the framebuffer viewer is at
`http://127.0.0.1:8766/`. Only one process may own the proxy and only one reader may consume a
given virtual-UART stream. A stale frame is not proof that Windows stopped.

For each run, record:

- root, m1n1, and Mu commit IDs;
- m1n1 Mach-O and Mu firmware SHA-256 values;
- launch profile and CPU count;
- start and end wall-clock times;
- physical UI, SSH/RDP, UART, per-CPU progress, timer, SGI/vGIC, storage, and bugcheck evidence;
- exactly one changed variable and whether the predicted counter changed.

Reboot the target back to its proxy payload between candidates and repeat the same wrapper. Do not
install a candidate on the ESP merely because it reaches the desktop once.

## Promote a candidate to standalone

An assisted result is only a hypothesis check. To ensure standalone receives the same fix:

1. Commit the m1n1/Mu/root source revisions used by the assisted run.
2. Update the root submodule pointers to those exact revisions.
3. Build `display=physical`, `debug=off` from the same tree.
4. Record the packed image hash and validate its manifest before installation.
5. Install it with `scripts/install-esp.sh`, cold boot without the host, and repeat the same workload.
6. Repeat with `debug=monitor` only if a standalone-only failure needs passive USB capture.

Example:

```sh
scripts/build-standalone.sh --display physical --debug off
shasum -a 256 dist/j313/boot.bin
sudo scripts/install-esp.sh install --disk diskXsY --image dist/j313/boot.bin
```

The candidate is complete only when assisted and standalone runs expose the same hardware contract,
diagnostics can be disabled without changing behavior, and the full Phase 0 acceptance gate passes.

## Next investigation

The remaining pause must be classified from bounded evidence rather than another timing workaround.
The next capture should correlate, across the same host timestamps:

- per-CPU guest-entry progress and WFI state;
- physical and virtual architectural timer state;
- SGI send, receive, pending, active, and EOI transitions;
- vGIC list-register ownership and retirement;
- global-lock hold/wait time, especially NVMe and physical interrupt paths;
- ANS request latency and queue depth;
- Windows SSH liveness and any watchdog bugcheck parameters.

The first confirmed invariant violation determines the next single-variable fix. Disabling Windows
watchdogs, removing cores, dropping interrupts, or treating a recovered pause as success is not an
acceptable correction.
