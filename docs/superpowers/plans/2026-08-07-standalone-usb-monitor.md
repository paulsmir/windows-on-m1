# Standalone USB Monitor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a cold-boot standalone monitor profile that always starts Windows while an attached host records m1n1/EL2 and guest-vUART output across resets.

**Architecture:** Extend the packed-image profile ABI with one explicit monitor flag instead of changing `uart` or `full`.  Decode the flag identically in Python and C, initialize the existing USB console/vUART transport, and make proxy takeover a profile policy consumed by the autonomous boot state machine.  A host-only Python recorder groups the two m1n1 ACM endpoints, writes raw and timestamped logs, and reopens them after re-enumeration.

**Tech Stack:** C11 m1n1 runtime and host tests, Python 3.10+ standard library plus pyserial, POSIX shell wrappers, `unittest`, Project Mu standalone packer.

## Global Constraints

- `debug=off`, `debug=uart`, `debug=full`, and assisted execution retain their existing behavior.
- Monitor mode never invokes `uartproxy_run()` because a host opened USB.
- Monitor mode does not enable virtual framebuffer streaming or periodic telemetry by itself.
- The recorder never sends bytes or proxy requests to the target.
- Unknown or combined debug flag values fail manifest/profile validation.
- No developer-specific usernames, device paths, or USB serial numbers enter public files.
- Every production change follows a witnessed red/green test cycle.

---

### Task 1: Public monitor profile and packed-image ABI

**Files:**
- Modify: `launch_profile.py`
- Modify: `tools/pack_boot.py`
- Modify: `scripts/build-standalone.sh`
- Modify: `tests/test_launch_profile.py`
- Modify: `tests/test_standalone_image.py`
- Modify: `tests/test_build_standalone.py`

**Interfaces:**
- Produces: `Debug.MONITOR`, encoded as manifest flag `0x10`.
- Produces: `LaunchProfile.proxy_takeover: bool`, false only for `off` and `monitor`.
- Produces: `LaunchProfile.capture_uart: bool`, true for `uart`, `full`, and `monitor`.
- Consumes: existing display bits `0x0..0x3`; monitor physical profile is `0x11`.

- [ ] **Step 1: Write failing Python profile tests**

Extend the literal expectations in `tests/test_launch_profile.py`:

```python
expected = {
    "off": (False, False, False, 0x1),
    "uart": (True, False, True, 0x5),
    "full": (True, True, True, 0x9),
    "monitor": (True, False, False, 0x11),
}

for name, want in expected.items():
    profile = api.parse_profile(debug=name)
    self.assertEqual(
        (profile.capture_uart, profile.telemetry,
         profile.proxy_takeover, profile.manifest_flags),
        want,
    )
```

Include `monitor` in the display/debug round-trip product.  Change invalid
manifest cases to literal conflicting or unknown values `(0xC, 0x14, 0x18,
0x1C, 0x20, 0xFFFFFFFF)`.

- [ ] **Step 2: Run the focused tests and witness RED**

Run:

```bash
python3 -m unittest tests.test_launch_profile -v
```

Expected: errors for the missing `Debug.MONITOR`/`proxy_takeover`, or rejection
of `debug="monitor"`.

- [ ] **Step 3: Implement the Python profile contract**

In `launch_profile.py`, add the enum/mapping and make decoding accept exactly
one of the four literal debug values:

```python
class Debug(str, Enum):
    OFF = "off"
    UART = "uart"
    FULL = "full"
    MONITOR = "monitor"

_DEBUG_FLAGS = {
    Debug.OFF: 0x0,
    Debug.UART: 0x4,
    Debug.FULL: 0x8,
    Debug.MONITOR: 0x10,
}
_KNOWN_FLAGS = 0x1F

@property
def capture_uart(self) -> bool:
    return self.debug in (Debug.UART, Debug.FULL, Debug.MONITOR)

@property
def proxy_takeover(self) -> bool:
    return self.debug in (Debug.UART, Debug.FULL)
```

Keep `telemetry` true only for `Debug.FULL`.  Decode the debug field by exact
lookup in `_DEBUG_FLAGS.values()` rather than treating `0x1C` as a valid mask.

- [ ] **Step 4: Add image and build-script tests**

In `tests/test_standalone_image.py`, pack/parse a physical monitor image and
assert `manifest.flags == 0x11`; keep `0x14`, `0x18`, `0x1C`, and `0x20` in the
rejection table.  In `tests/test_build_standalone.py`, invoke the hardware-free
dry run with `--debug monitor` and assert the packer receives that exact value.

- [ ] **Step 5: Update the standalone CLI allow-list**

Change only `scripts/build-standalone.sh` usage and validation from
`off|uart|full` to `off|uart|full|monitor`.  `run-assisted.sh` and
`run_uefi.py` must not gain monitor because assisted behavior is outside this
profile.

- [ ] **Step 6: Run focused and complete root tests**

Run:

```bash
python3 -m unittest tests.test_launch_profile tests.test_standalone_image tests.test_build_standalone -v
python3 -m unittest discover -s tests -v
```

Expected: all tests pass.

- [ ] **Step 7: Commit the public ABI change**

```bash
git add launch_profile.py tools/pack_boot.py scripts/build-standalone.sh \
  tests/test_launch_profile.py tests/test_standalone_image.py \
  tests/test_build_standalone.py
git commit -m "feat: add standalone monitor profile"
```

---

### Task 2: Native monitor policy and autonomous launch

**Files:**
- Modify: `m1n1_windows/src/hv_autonomous_manifest.h`
- Modify: `m1n1_windows/src/hv_autonomous_profile.h`
- Modify: `m1n1_windows/src/hv_autonomous_profile.c`
- Modify: `m1n1_windows/src/hv_autonomous_boot_runtime.c`
- Modify: `m1n1_windows/tests/hv_autonomous_manifest_test.c`
- Modify: `m1n1_windows/tests/hv_autonomous_profile_test.c`

**Interfaces:**
- Produces: `HV_AUTONOMOUS_DEBUG_MONITOR = 0x10u` and known flags `0x1fu`.
- Produces: `profile.monitor` and `profile.proxy_takeover` booleans.
- Produces: `hv_autonomous_profile_accept_proxy(profile, host_connected)`.
- Consumes: `hv_autonomous_profile_usb_plan`; monitor requires both platform power and debug transport.

- [ ] **Step 1: Write failing native manifest/profile tests**

Add literal assertions:

```c
assert(hv_autonomous_profile_decode(HV_AUTONOMOUS_DISPLAY_PHYSICAL |
                                        HV_AUTONOMOUS_DEBUG_MONITOR,
                                    &profile));
assert(profile.debug_host);
assert(profile.monitor);
assert(!profile.telemetry);
assert(!profile.proxy_takeover);
assert(!hv_autonomous_profile_accept_proxy(&profile, true));

hv_autonomous_profile_usb_plan(&profile, &usb);
assert(usb.power_platform);
assert(usb.start_debug_transport);
```

For UART, assert `proxy_takeover` and
`hv_autonomous_profile_accept_proxy(&profile, true)` are true.  Add conflicting
flag cases `0x14`, `0x18`, and `0x1c` to both native rejection tests.

- [ ] **Step 2: Run native focused tests and witness RED**

Run:

```bash
cd m1n1_windows
tests/run_host_tests.sh hv_autonomous_manifest_test hv_autonomous_profile_test
```

Expected: compile failure for the missing monitor constant/fields/function.

- [ ] **Step 3: Implement exact native decoding**

Define `HV_AUTONOMOUS_DEBUG_MONITOR` and expand known flags.  Replace mask-only
validation with exact accepted debug values `0`, `0x4`, `0x8`, and `0x10`.
Populate:

```c
out->monitor = debug == HV_AUTONOMOUS_DEBUG_MONITOR;
out->debug_host = debug != 0;
out->telemetry = debug == HV_AUTONOMOUS_DEBUG_FULL;
out->proxy_takeover = debug == HV_AUTONOMOUS_DEBUG_UART ||
                      debug == HV_AUTONOMOUS_DEBUG_FULL;
```

Implement the null-safe policy boundary:

```c
bool hv_autonomous_profile_accept_proxy(
    const struct hv_autonomous_profile *profile, bool host_connected)
{
    return profile && host_connected && profile->proxy_takeover;
}
```

- [ ] **Step 4: Route host detection through the profile policy**

Add a profile pointer to `struct boot_runtime_io`.  Make `runtime_command()`
return `HV_AUTONOMOUS_COMMAND_PROXY` only through
`hv_autonomous_profile_accept_proxy()`.  For monitor, initialize USB iodevs and
the virtual UART exactly as UART mode does, then call
`hv_autonomous_boot_poll()` with a zero deadline.  Its first iteration services
USB once, ignores connected-host takeover by policy, and calls the normal guest
launch without a fixed delay.  Keep the three-second deadline for UART/FULL.

Print one unambiguous line before entry:

```c
printf("Standalone: USB monitor active; proxy takeover disabled\n");
```

- [ ] **Step 5: Run native tests and compile m1n1**

Run:

```bash
cd m1n1_windows
tests/run_host_tests.sh hv_autonomous_manifest_test hv_autonomous_profile_test
tests/run_host_tests.sh
make -j4
```

Expected: all 19 host tests pass and `build/m1n1.bin` plus
`build/m1n1.macho` are produced.

- [ ] **Step 6: Commit m1n1 and the root submodule pointer**

```bash
cd m1n1_windows
git add src/hv_autonomous_manifest.h src/hv_autonomous_profile.h \
  src/hv_autonomous_profile.c src/hv_autonomous_boot_runtime.c \
  tests/hv_autonomous_manifest_test.c tests/hv_autonomous_profile_test.c
git commit -m "feat: keep standalone booting with USB monitor"
cd ..
git add m1n1_windows
git commit -m "feat: integrate standalone USB monitor"
```

---

### Task 3: Reconnecting two-channel host recorder

**Files:**
- Create: `tools/standalone_monitor.py`
- Create: `scripts/log-standalone.sh`
- Create: `tests/test_standalone_monitor.py`
- Modify: `tests/test_public_scripts.py`

**Interfaces:**
- Produces CLI: `tools/standalone_monitor.py [--console DEVICE] [--vuart DEVICE] [--output DIR] [--once]`.
- Produces files per generation: `console.raw`, `console.tlog`, `vuart.raw`, `vuart.tlog`, and `events.log` beneath `DIR/generation-NNN/`.
- Consumes pyserial `serial.tools.list_ports.comports()` and VID/PID `1209:316d`.

- [ ] **Step 1: Write failing endpoint-selection tests**

Use complete fake port records with `device`, `vid`, `pid`, `serial_number`,
and `location`.  Test that two m1n1 ports sharing serial/location are selected,
unrelated serial devices are ignored, explicit paths win, and multiple m1n1
groups raise an ambiguity error listing every candidate.  The stable role
ordering is the sorted pair within an already metadata-matched m1n1 device:
first console, second vUART.

```python
console, vuart = select_monitor_ports(ports)
self.assertEqual(console.device, "/dev/cu.usbmodem-test1")
self.assertEqual(vuart.device, "/dev/cu.usbmodem-test3")
```

- [ ] **Step 2: Run the focused test and witness RED**

Run:

```bash
python3 -m unittest tests.test_standalone_monitor -v
```

Expected: import failure for missing `tools.standalone_monitor`.

- [ ] **Step 3: Implement pure discovery and generation paths**

Create immutable `MonitorPort` and `MonitorPair` dataclasses, a
`select_monitor_ports()` pure function, and `generation_directory(root,
number)`.  Candidate ports must have VID `0x1209`, PID `0x316d`, and matching
non-empty serial/location metadata unless both devices were explicitly given.
Never embed `/dev/cu.usbmodem` or a machine serial number as a selected device.

- [ ] **Step 4: Add failing recorder/reconnect tests**

Drive `capture_generation()` with fake serial objects that return two chunks
then raise `SerialException`.  Assert raw bytes are exact, timestamped lines
contain both chunks, `events.log` records open/disconnect, and generation 2
uses a different directory without overwriting generation 1.

- [ ] **Step 5: Implement capture and reconnect loop**

Open both serial endpoints read-only in effect: call only `read()`, never
`write()`.  Use one reader thread per endpoint and a shared stop event.  On
disconnect, close both, append the event, increment the generation, and return
to discovery.  `--once` exits after the first disconnect for automated tests;
normal mode waits indefinitely and preserves every generation.

- [ ] **Step 6: Add the shell wrapper and dry-run contract**

`scripts/log-standalone.sh` resolves the repository root, chooses
`proxyenv/bin/python` when available, forwards explicit endpoints/output, and
supports `--dry-run`.  Its dry run prints the exact Python command without
touching USB.  Add it to the public-script hygiene list and assert no private
path or device identifier appears.

- [ ] **Step 7: Run focused and root tests, then commit**

Run:

```bash
python3 -m unittest tests.test_standalone_monitor tests.test_public_scripts -v
python3 -m unittest discover -s tests -v
```

Then commit:

```bash
git add tools/standalone_monitor.py scripts/log-standalone.sh \
  tests/test_standalone_monitor.py tests/test_public_scripts.py
git commit -m "feat: capture standalone USB monitor logs"
```

---

### Task 4: Documentation, image verification, and cold capture

**Files:**
- Modify: `documentation/CONFIGURATION.md`
- Modify: `documentation/RUN.md`
- Modify: `documentation/DEBUGGING.md`
- Modify: `documentation/BUILD.md`
- Modify: `tests/test_public_documentation.py`
- Generated and ignored: `dist/j313/boot.bin`, `dist/j313/SHA256SUMS`

**Interfaces:**
- Documents build command: `scripts/build-standalone.sh --display physical --debug monitor`.
- Documents recorder command: `scripts/log-standalone.sh --output standalone-monitor-logs`.
- Produces the paired logs required to plan the shared C guest-boot engine.

- [ ] **Step 1: Add failing documentation contract tests**

Assert public documentation names `debug=monitor`, states that it always
autoboots rather than entering proxy, shows the build and recorder commands,
and warns that the monitor image must match its m1n1 manifest ABI.

- [ ] **Step 2: Run documentation tests and witness RED**

Run:

```bash
python3 -m unittest tests.test_public_documentation -v
```

Expected: failure because monitor documentation is absent.

- [ ] **Step 3: Document operation and recovery**

Update the four documents with one consistent workflow: start recorder on the
host, install the monitor image on `disk0s4`, cold power on with USB attached,
wait through reset/re-enumeration, stop the recorder, and restore/rebuild
`debug=off` after capture.  State that monitor is diagnostic and not the final
production profile.

- [ ] **Step 4: Run all automated verification**

Run:

```bash
python3 -m unittest discover -s tests -v
cd m1n1_windows && tests/run_host_tests.sh && make -j4
cd .. && git diff --check
```

Expected: root tests and all 19 m1n1 host tests pass; m1n1 compiles; no diff
errors.

- [ ] **Step 5: Build and inspect the monitor image**

Run:

```bash
scripts/build-standalone.sh --display physical --debug monitor
shasum -a 256 dist/j313/boot.bin
cat dist/j313/SHA256SUMS
python3 -c 'from pathlib import Path; from standalone_image import parse_image; m,fw=parse_image(Path("dist/j313/boot.bin").read_bytes()); print(m); print(len(fw))'
```

Expected: build succeeds, hashes match, manifest flags are `0x11`, and payload
decompression/CRC validation succeeds.

- [ ] **Step 6: Commit documentation**

```bash
git add documentation/CONFIGURATION.md documentation/RUN.md \
  documentation/DEBUGGING.md documentation/BUILD.md \
  tests/test_public_documentation.py
git commit -m "docs: explain standalone USB monitoring"
```

- [ ] **Step 7: Perform the physical cold capture**

Start `scripts/log-standalone.sh --output standalone-monitor-logs` before
powering the Air.  Install the verified monitor `boot.bin` to Asahi ESP
`disk0s4`, power off, attach only the debug-host cable, and cold power on.
Keep recording across the Windows-triggered reset and the next USB
re-enumeration.  Verify both channel logs are non-empty and report the final 200
timestamped lines from each channel together with reset/exception markers.

- [ ] **Step 8: Record the evidence boundary**

Add a dated diagnostic note under `documentation/` containing the tested image
SHA, the last successful boot stage, the reset origin observed in the paired
logs, and the exact assisted/standalone divergence that the shared guest-boot
engine must eliminate.  Do not claim Windows standalone is fixed until a later
production cold boot reaches and remains at the desktop.
