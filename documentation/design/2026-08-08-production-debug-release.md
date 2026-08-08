# Production and Debug Release Workflow

## Goal

Publish one reproducible Windows-on-M1 implementation that supports a quiet autonomous boot for
normal use and a separately built USB-monitor profile for firmware, hypervisor, driver, and
Windows debugging. Keep the public repository authoritative while preserving the private
repository as a laboratory history without publishing runtime artifacts.

## Release profiles

The supported production artifact is built with `display=physical` and `debug=off`. It boots from
the installed ESP without a host computer, exposes no USB debug endpoints, and performs no
continuous diagnostic printing.

The supported standalone diagnostic artifact is built with `display=physical` and
`debug=monitor`. It follows the same Stage0, Stage1, Mu, guest-layout, CPU, NVMe, USB, and display
path as production. The only intentional difference is passive USB console/vUART availability and
diagnostic telemetry. Host attachment must never gate guest entry.

Assisted launch remains a separate development workflow. It chainloads m1n1 from another Mac and
starts Mu through the Python hypervisor client. It is useful for interactive proxy work, KD tools,
virtual-display capture, and experiments that cannot yet run autonomously.

## Operator workflow

Documentation will provide exact commands for:

1. building production and monitor artifacts without overwriting the only known-good copy;
2. recording the SHA-256, packed profile, layout, and firmware metadata before installation;
3. installing either artifact on the m1n1 ESP and restoring the original Asahi `boot.bin`;
4. starting the passive USB recorder before cold boot, or attaching it after Windows starts;
5. interpreting preflight checkpoints, CPU-entry records, transport loss, Windows resets, and a
   live guest with no new console output;
6. switching back to production after diagnosis; and
7. using assisted mode, KD helpers, and the virtual display for driver development.

The monitor documentation must warn that verbose synchronous USB logging is diagnostic overhead.
If the host is not draining the endpoint, buffering or backpressure may create visible guest
latency. Production validation must therefore use `debug=off`, not infer production performance
from a monitor build.

## Repository publication

The public `windows-on-m1` repository is the canonical distribution and documentation source.
Implementation commits are made once in the public m1n1 and Mu forks. The public root repository
then records those exact submodule commits together with scripts, tests, and English documentation.

The private repository keeps its historical experiments. It advances its m1n1 and Mu references
to the same published commits and records where the canonical public instructions live. Generated
logs, PID files, framebuffer metadata, temporary baselines, local firmware, images, credentials,
and machine-specific state are never staged.

Commit messages contain technical rationale only. They must not contain `Co-Authored-By`, Codex or
Claude attribution, session URLs, or generated-assistant trailers.

## Acceptance criteria

- Host tests for the standalone packer, monitor, launch profile, and m1n1 hypervisor pass.
- The monitor artifact reports all required preflight checkpoints, CPU entry for CPUs 0 through 7,
  and no unhandled EL2 exception during the observed Windows boot.
- The production artifact parses as `display=physical`, `debug=off` and cold-boots without a debug
  host; the user confirms visible Windows progress beyond the static boot logo.
- Both artifacts have preserved copies and recorded SHA-256 hashes.
- Public and private staged diffs contain no runtime artifacts or assistant-attribution trailers.
- Published root repositories point to reachable commits in the two fork repositories.
