# Self-Chainload and Shared Guest-Boot Engine Design

## Purpose

Make standalone and assisted Windows startup enter the hypervisor guest from
the same initialized m1n1 context and through one ordered guest-boot engine.
The first production success criterion is a cold standalone J313 boot with all
eight CPUs reaching a stable Windows desktop. Startup speed is secondary until
that criterion is repeatable.

## Evidence and Problem Boundary

The passive standalone USB monitor captured the reset that previously appeared
only as a short Windows logo followed by a reboot. Firmware, the emulated NVMe
controller, GPT discovery, Windows Boot Manager, and the Windows kernel all ran
before the failure. Windows then requested PSCI CPU_ON for CPU1:

```text
PSCI DEBUG: turning on CPU1 MPIDR: 0x1
HV: Initializing secondary 1
HV: Secondary 1 published entry=0x8e6702010 x0=0x10e000
Exception: SYNC
MPIDR: 0x80000001
ESR: 0x02000000
Unhandled exception, rebooting...
```

The reset occurs inside the physical CPU1 initialization call, before
`HV: Entering guest secondary 1`. Successful assisted captures with the same
`hv.c`, `smp.c`, and `hv_psci.c` sources proceed through both `Entering` and
`consumed` for CPU1 and the remaining cores.

Two uncontrolled differences remain:

1. The working assisted `hv.o` was produced by Homebrew Clang 22.1.8, while the
   packed standalone `hv.o` was produced by GCC 13.3.0. Their
   `hv_init_secondary()` machine code differs.
2. Assisted startup uses `P_VECTOR` to reload a fresh m1n1 image after updating
   boot arguments and ADT state. The current standalone path invokes the
   autonomous guest loader directly from the first m1n1 entered by iBoot.

The design removes both variables: public artifacts use one pinned m1n1
toolchain, and standalone performs the same fresh-image transition before guest
orchestration.

## Approaches Considered

### Patch CPU1 initialization only

Instrumenting or patching the current `hv_init_secondary()` failure could make
this boot advance, but it would leave every other assisted/standalone ordering
difference intact. The next difference would become another hardware-only
failure. This is useful as diagnosis, not as the architecture.

### Continue expanding the early autonomous runtime

Porting more of `run_uefi.py` into the current early autonomous call stack would
retain two orchestration implementations. The project has already accumulated
ordering bugs around stage-2 hook replacement, RAM bounds, USB power, display
handoff, and secondary CPU state through this duplication.

### Self-chainload plus one deferred guest engine

This is the selected approach. A minimal first stage reloads a fresh m1n1. Both
the standalone manifest path and assisted proxy path then submit the same
request to a top-level dispatcher. The dispatcher invokes one C engine with one
ordering contract.

## Toolchain Control Experiment

Before changing the image format, build the current monitored standalone image
with the same pinned Clang toolchain used by the successful assisted binary.
This is a diagnostic A/B test, not the final fix.

- If Clang reaches `Entering guest secondary 1`, compiler/code-generation is
  the immediate trigger.
- If Clang fails at the same point, direct first-stage execution is the
  immediate trigger.
- Either result is recorded in the diagnostic history and the self-chainload
  design still proceeds, because one build and startup contract is required.

No source workaround is accepted solely because it hides a GCC-only failure.
Any undefined behavior exposed by the A/B test must be corrected and protected
by a host test or a narrow generated-code/build contract where appropriate.

## Two-Stage Image

The installed ESP file remains `m1n1/boot.bin`. Its outer image contains:

1. a small Stage 0 m1n1 runtime;
2. a versioned bootstrap manifest;
3. a compressed inner boot image;
4. the inner image's uncompressed size and CRC32.

The inner image contains:

1. the full Stage 1 m1n1 runtime;
2. the existing versioned guest manifest and launch-profile flags;
3. the compressed Mu firmware payload;
4. the existing guest layout and firmware integrity fields.

Stage 0 validates all outer bounds before allocation, decompresses the inner
image, verifies its exact size and CRC32, and calls the existing C chainload
mechanism. That mechanism preserves the required boot arguments, ADT, SEPFW,
and pre-OS log state and performs the same shutdown/vector boundary as the
assisted `P_VECTOR` flow.

The outer and inner formats are unambiguous. Old binaries reject the new outer
format instead of interpreting it as a guest manifest. Stage 0 never tries to
initialize the Windows hypervisor, NVMe backend, guest xHCI, or display surface.

## Deferred Guest-Boot Request

Stage 1 owns a single pending-request slot. A request contains only validated
data needed by the guest engine:

- firmware address, compressed and uncompressed sizes, and CRC32;
- guest layout version and resolved layout values;
- display and debug launch profile;
- optional preloaded RAM-disk descriptor;
- source (`standalone` or `assisted`) for diagnostics only.

The source does not select different boot behavior.

Standalone manifest processing validates the inner manifest and submits the
request. Assisted tooling uploads the firmware and optional RAM disk, then uses
a proxy command to submit the same request representation. Submission does not
run `hv_init()` or enter the guest. It causes the current action/proxy loop to
return to the m1n1 top-level dispatcher.

The dispatcher consumes the request exactly once and calls the shared engine.
This prevents guest startup from occurring inside either the early payload
parser stack or a proxy RPC handler stack.

## Shared Guest-Boot Engine

The engine owns one explicit sequence:

1. validate the request and resolve the physical RAM bound;
2. allocate/decompress and verify Mu firmware;
3. construct guest boot arguments and the copied guest ADT;
4. initialize the hypervisor and stage-2 page tables;
5. map normal RAM and the low-memory alias;
6. install broad `/arm-io` mappings;
7. install or restore reserved hooks for vUART, PCI/NVMe, and xHCI/DART after
   broad mappings so those hooks cannot be overwritten;
8. configure vGIC/PSCI and the J313 CPU topology;
9. clear and publish the guest framebuffer to the requested consumers;
10. perform the final page-table update and verify required translations;
11. enter Mu on CPU0.

The precise ordering is characterized against the successful assisted trace
before the Python implementation is retired. Python remains responsible for
transport, file upload, logging, and development UX; it no longer owns hardware
mapping order.

Secondary CPUs continue to be started on Windows PSCI requests. The engine does
not pre-enter guest secondaries. A successful hardware checkpoint requires the
monitor log to contain `Entering guest secondary N` and `consumed entry` for
every enabled CPU.

## Monitor and Production Profiles

The existing launch-profile contract remains intact:

- `debug=monitor` exposes passive console/vUART endpoints, never accepts proxy
  takeover, and automatically starts the inner image and Windows;
- `debug=uart` and `debug=full` retain interactive assisted behavior;
- `debug=off` starts without the gadget transport after the monitor build is
  proven;
- display selection remains independent (`physical`, `virtual`, `both`, or
  `none`).

Stage 0 may expose monitor output when built for diagnosis, but it cannot let a
connected host divert or mutate an automatic monitor boot.

## Failure Handling and Recovery

Every Stage 0 and guest-engine transition records a stable stage identifier and
specific error code. Validation, decompression, CRC, allocation, mapping, and
translation failures return to a safe proxy/recovery path when possible. They
must not call `flush_and_reboot()` merely because guest preparation failed.

An architectural exception remains fatal, but the passive monitor records its
CPU, ELR, ESR, FAR, and current engine stage before reset. Generation-aware host
logging continues across USB disconnect and re-enumeration.

Installation remains atomic and reversible. `install-esp.sh` keeps the scoped
backup used by `restore`; neither testing nor a failed Stage 0 modifies Windows
partitions.

## Verification Strategy

### Host and native tests

- outer image encode/decode, alignment, size, CRC, and corruption rejection;
- old/new format rejection rather than ambiguous fallback;
- Stage 0 handoff construction using a fake chainload boundary;
- single-slot request submission, duplicate rejection, and consume-once
  behavior;
- identical request normalization for standalone and assisted sources;
- shared-engine stage ordering and stop-on-first-error behavior;
- required reserved-hook ordering after broad mappings;
- unchanged profile encode/decode and monitor no-takeover behavior;
- complete root Python and m1n1 native suites;
- reproducible m1n1 toolchain identity in the public build output.

Tests use pure policies and injected operations for host execution. They do not
assert source text or mock the behavior under test.

### Hardware checkpoints

1. **Toolchain A/B:** current monitored direct standalone image, rebuilt with
   pinned Clang, records whether CPU1 passes the current failure point.
2. **Stage 0 boundary:** monitor proves Stage 0 validation, chainload, fresh
   Stage 1 initialization, and discovery of the inner guest manifest.
3. **CPU1 boundary:** monitor records both `Entering` and `consumed` for CPU1.
4. **Eight-core boundary:** the same pair is recorded for CPUs 1 through 7 with
   no EL2 exception or Windows watchdog stop.
5. **Stable desktop:** physical monitor build reaches Windows, accepts RDP, and
   remains responsive under disk and scheduler load.
6. **Production boot:** `display=physical debug=off` cold-boots without the
   development host and remains stable.

Each checkpoint uses a new immutable image SHA and preserves the complete
monitor generation logs. Passing an earlier checkpoint is not reported as a
complete standalone implementation.

## Delivery Phases

1. Record the current CPU1 capture and run the pinned-Clang control experiment.
2. Add and validate the outer bootstrap format and Stage 0 self-chainload.
3. Add the pending-request dispatcher and move autonomous startup to it.
4. Move assisted startup to the same request and engine.
5. Remove duplicated Python hardware-ordering logic only after trace parity.
6. Validate eight-core monitor and quiet production cold boots.
7. Update public build, run, debugging, recovery, and limitation documents.

## Non-Goals

- Optimizing boot duration before the stable-desktop checkpoint.
- Reclaiming the currently hardware-reserved RAM.
- Improving NVMe throughput.
- Adding internal keyboard or trackpad drivers.
- Changing the installed Windows partition layout.
- Treating a one-core boot as completion.
