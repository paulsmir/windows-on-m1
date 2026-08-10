# Native Apple Keyboard and Trackpad Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a test-signed ARM64 KMDF driver that talks directly to the J313 Apple SPI HID hardware, exposes the built-in keyboard and a basic cursor/click trackpad frontend to Windows, and fails locally instead of hanging the guest.

**Architecture:** m1n1 performs ADT-validated power/pinmux preservation, stage-2 pass-through, and one physical-to-virtual level interrupt route. Mu publishes a versioned ACPI resource contract. A portable C protocol core implements Apple SPI HID framing and discovery; a KMDF function driver owns SPI/GPIO MMIO and publishes keyboard and basic mouse collections through VHF.

**Tech Stack:** C11 portable protocol library, m1n1 freestanding C, Mu/ACPI ASL, Python 3 `unittest`, KMDF, VHF, Visual Studio 2022, Windows 11 WDK ARM64, PowerShell, KD over the existing virtual UART.

## Global Constraints

- Target hardware is MacBook Air M1 `j313` / T8103 only.
- Milestone 1 is Windows-only input: built-in keyboard plus cursor movement and primary click.
- Windows Precision Touchpad is the required next milestone, not part of this implementation plan.
- Initial Windows packages are test-signed; production signing is out of scope.
- m1n1 must not parse Apple input packets or emulate a keyboard, mouse, or USB device.
- No unbounded MMIO polls, SPI waits, reset loops, interrupt loops, or packet buffers.
- The Windows ISR does constant bounded work and never allocates memory or performs a complete SPI transaction.
- Every hardware write is gated by the live ADT inventory and the generated ACPI/driver contract.
- External USB keyboard and mouse remain the recovery input throughout hardware bring-up.
- Do not copy external GPL driver source into this MIT-licensed tree.
- Do not add `Co-Authored-By`, assistant attribution, session URLs, or similar trailers to commits.
- Do not commit firmware binaries, driver build output, certificates, private keys, packet captures, or machine-specific serial paths.

## File Map

### Canonical platform contract

- Create `config/j313-apple-input.json`: the single reviewed J313 input resource contract.
- Create `tools/apple_input_contract.py`: schema, validation, and render functions.
- Create `tools/generate_apple_input_contract.py`: atomically generate/check all consumers.
- Create `tools/apple_input_inventory.py`: extract the live ADT hardware inventory without writes.
- Create `tests/fixtures/j313-apple-input-adt.json`: sanitized inventory fixture.
- Create `tests/test_apple_input_contract.py`: schema, fixture, generation, and range tests.

### m1n1

- Create `m1n1_windows/src/hv_apple_input.generated.h`: generated constants.
- Create `m1n1_windows/src/hv_apple_input.h`: preflight and route interface.
- Create `m1n1_windows/src/hv_apple_input.c`: live ADT resolution, read-only validation, bring-up, and stage-2 mapping.
- Modify `m1n1_windows/src/hv_irq_routes.[ch]`: bounded runtime route registration.
- Modify `m1n1_windows/src/hv_autonomous_runtime.c`: invoke input preflight before guest entry.
- Modify `m1n1_windows/src/hv_launch_j313.c`: include the input route in the assisted launch contract.
- Modify `m1n1_windows/Makefile`: compile the new source and tests.
- Create `m1n1_windows/tests/hv_apple_input_contract_test.c`.
- Modify `m1n1_windows/tests/hv_irq_routes_test.c`.

### Mu and ACPI

- Create `mu/Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleInput.asl.inc`: generated `AINP` device.
- Modify `mu/Platform/MacBookAirMid2020Pkg/AcpiTables/DSDT.asl`: include the generated node.
- Create `tests/test_apple_input_acpi.py`: exact ACPI resource and collision checks.

### Portable Apple SPI HID protocol

- Create `drivers/apple-input/protocol/include/apple_spihid.h`: public types and functions.
- Create `drivers/apple-input/protocol/src/apple_spihid_crc.c`: CRC16-USB.
- Create `drivers/apple-input/protocol/src/apple_spihid_packet.c`: packet validation and request encoding.
- Create `drivers/apple-input/protocol/src/apple_spihid_reassembly.c`: bounded fragment assembly.
- Create `drivers/apple-input/protocol/src/apple_spihid_discovery.c`: discovery state machine and events.
- Create `drivers/apple-input/protocol/tests/apple_spihid_test.c`: native unit executable.
- Create `tests/test_apple_spihid_protocol.py`: compile/run wrapper for the native tests.

### Windows driver

- Create `drivers/apple-input/windows/AppleInput.sln` and `AppleInput.vcxproj`.
- Create `drivers/apple-input/windows/AppleInput.inf`.
- Create `drivers/apple-input/windows/include/apple_input_device.h`.
- Create `drivers/apple-input/windows/include/apple_input_ioctl.h`.
- Create `drivers/apple-input/windows/include/j313_apple_input.generated.h`.
- Create `drivers/apple-input/windows/src/driver.c`: DriverEntry and device creation.
- Create `drivers/apple-input/windows/src/device.c`: PnP, resource validation, D0 entry/exit, cleanup.
- Create `drivers/apple-input/windows/src/spi.c`: bounded polled SPI3 implementation.
- Create `drivers/apple-input/windows/src/gpio.c`: enable GPIO and nub interrupt acknowledgement.
- Create `drivers/apple-input/windows/src/transport.c`: protocol-core adapter and passive worker.
- Create `drivers/apple-input/windows/src/vhf.c`: keyboard/basic-mouse VHF frontends.
- Create `drivers/apple-input/windows/src/trackpad.c`: descriptor-driven first-contact translation.
- Create `drivers/apple-input/windows/src/diagnostics.c`: counters, WPP events, and read-only IOCTL.
- Create `drivers/apple-input/windows/scripts/build-driver.ps1`.
- Create `drivers/apple-input/windows/scripts/new-test-certificate.ps1`.
- Create `drivers/apple-input/windows/scripts/install-driver.ps1`.
- Create `drivers/apple-input/windows/scripts/uninstall-driver.ps1`.
- Create `drivers/apple-input/windows/tools/AppleInputDiag/`: small ARM64 diagnostic CLI.
- Create `tests/test_apple_input_windows_package.py`: static package/INF/script contract tests.

### KD and documentation

- Create `tools/kd/kd_apple_input.py`: devnode and symbol-backed state/counter summary.
- Create `documentation/APPLE_INPUT.md`: build, signing, install, capture, recovery, and validation runbook.
- Modify `documentation/ARCHITECTURE.md`, `DEBUGGING.md`, `LIMITATIONS.md`, and `README.md` only after the corresponding hardware checkpoint is demonstrated.

---

### Task 1: Lock the live J313 input inventory and canonical schema

**Files:**
- Create: `config/j313-apple-input.json`
- Create: `tools/apple_input_contract.py`
- Create: `tools/apple_input_inventory.py`
- Create: `tests/fixtures/j313-apple-input-adt.json`
- Create: `tests/test_apple_input_contract.py`

**Interfaces:**
- Produces: `AppleInputContract load_contract(path: Path)` and `dict extract_inventory(adt)`.
- Produces: JSON keys `contract_version`, `acpi_hid`, `spi`, `ap_gpio`, `nub_gpio`, `interrupt`, and `timings_us`.
- The interrupt contract fixes guest INTID 865 but records the live parent AIC IRQ selected from 330 through 336.

- [ ] **Step 1: Write schema and fixture tests**

```python
def test_j313_contract_has_safe_exact_resources(self):
    c = load_contract(CONTRACT)
    self.assertEqual(c.contract_version, 1)
    self.assertEqual(c.spi.base, 0x23510C000)
    self.assertEqual(c.spi.size, 0x4000)
    self.assertEqual(c.spi.bus_hz, 8_000_000)
    self.assertEqual(c.ap_gpio.pin, 195)
    self.assertEqual(c.nub_gpio.pin, 13)
    self.assertEqual(c.interrupt.guest_vintid, 865)
    self.assertEqual(c.interrupt.parent_candidates, tuple(range(330, 337)))

def test_inventory_fixture_selects_the_pin13_parent_group(self):
    inventory = extract_inventory(json.loads(FIXTURE.read_text()))
    self.assertIn(inventory["selected_parent_irq"], range(330, 337))
    self.assertEqual(inventory["spi"]["compatible"], "spi-1,spimc")
    self.assertEqual(inventory["device"]["compatible"], "hid-transport,spi")
```

- [ ] **Step 2: Run the tests and verify the missing API failure**

Run: `proxyenv/bin/python -m unittest tests.test_apple_input_contract -v`

Expected: FAIL because `tools/apple_input_contract.py` does not exist.

- [ ] **Step 3: Implement the strict schema and read-only inventory extractor**

Use immutable dataclasses, reject missing/unknown keys, reject ranges outside the J313 arm-io aperture, reject GPIO pins outside their controller counts, reject candidate IRQs outside 32..1019, and reject overlaps with guest RAM, ECAM, NVMe BAR space, and the framebuffer.

The initial JSON values are:

```json
{
  "contract_version": 1,
  "acpi_hid": "APPL0001",
  "spi": {"base": "0x23510c000", "size": "0x4000", "source_hz": 120000000, "bus_hz": 8000000},
  "ap_gpio": {"base": "0x23c100000", "size": "0x100000", "pin": 195},
  "nub_gpio": {"base": "0x23d1f0000", "size": "0x4000", "pin": 13},
  "interrupt": {"active_low": true, "parent_candidates": [330, 331, 332, 333, 334, 335, 336], "guest_vintid": 865},
  "timings_us": {"reset_high": 5000, "reset_low": 5000, "boot_wait": 50000, "cs_setup": 65, "cs_hold": 65, "cs_inactive": 250, "transfer_timeout": 200000}
}
```

`apple_input_inventory.py` must print JSON and contain no write-capable proxy call.

- [ ] **Step 4: Run focused and full tests**

Run:

```bash
proxyenv/bin/python -m unittest tests.test_apple_input_contract -v
proxyenv/bin/python -m unittest discover -s tests -v
```

Expected: PASS.

- [ ] **Step 5: Capture the live inventory without writing hardware**

Run during an assisted m1n1 proxy session:

```bash
M1N1DEVICE=/dev/cu.usbmodemPROXY \
  proxyenv/bin/python tools/apple_input_inventory.py \
  --output .local/apple-input/j313-live-inventory.json
```

Expected: SPI3, both GPIO controllers, pin indices, compatibility strings, interrupt group, and selected parent AIC IRQ match the contract. If they do not match, update the design/contract through review; do not proceed to a hardware write.

- [ ] **Step 6: Commit**

```bash
git add config/j313-apple-input.json tools/apple_input_contract.py \
  tools/apple_input_inventory.py tests/fixtures/j313-apple-input-adt.json \
  tests/test_apple_input_contract.py
git commit -m "feat: define J313 Apple input contract"
```

### Task 2: Generate identical m1n1, Mu, and Windows contract consumers

**Files:**
- Create: `tools/generate_apple_input_contract.py`
- Create: `m1n1_windows/src/hv_apple_input.generated.h`
- Create: `mu/Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleInput.asl.inc`
- Create: `drivers/apple-input/windows/include/j313_apple_input.generated.h`
- Modify: `tests/test_apple_input_contract.py`

**Interfaces:**
- Consumes: `AppleInputContract` from Task 1.
- Produces: `render_m1n1(contract)`, `render_asl(contract)`, and `render_windows(contract)`.
- Produces: CLI `python3 tools/generate_apple_input_contract.py [--check]`.

- [ ] **Step 1: Add failing generation-equality tests**

```python
def test_checked_in_consumers_are_generated(self):
    c = load_contract(CONTRACT)
    self.assertEqual(render_m1n1(c), M1N1_HEADER.read_text())
    self.assertEqual(render_asl(c), ASL_INCLUDE.read_text())
    self.assertEqual(render_windows(c), WINDOWS_HEADER.read_text())
```

- [ ] **Step 2: Verify failure**

Run: `proxyenv/bin/python -m unittest tests.test_apple_input_contract -v`

Expected: FAIL because render functions and generated files are absent.

- [ ] **Step 3: Implement deterministic atomic generation**

The m1n1 and Windows headers expose the same numeric constants with their native integer suffixes. The ASL include emits a complete `Device(AINP)` with three `QWordMemory` resources, one guest `Interrupt` resource at INTID 865, `_CCA = 1`, `_UID = 0`, and a `_DSD` package containing contract version, pins, frequency, polarity, and timing values.

- [ ] **Step 4: Generate and verify**

Run:

```bash
python3 tools/generate_apple_input_contract.py
python3 tools/generate_apple_input_contract.py --check
proxyenv/bin/python -m unittest tests.test_apple_input_contract -v
```

Expected: all commands pass and a second generation produces no diff.

- [ ] **Step 5: Commit**

```bash
git add tools/generate_apple_input_contract.py tests/test_apple_input_contract.py \
  m1n1_windows/src/hv_apple_input.generated.h \
  mu/Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleInput.asl.inc \
  drivers/apple-input/windows/include/j313_apple_input.generated.h
git commit -m "feat: generate Apple input resource contract"
```

### Task 3: Add bounded runtime physical IRQ routing in m1n1

**Files:**
- Modify: `m1n1_windows/src/hv_irq_routes.h`
- Modify: `m1n1_windows/src/hv_irq_routes.c`
- Modify: `m1n1_windows/tests/hv_irq_routes_test.c`

**Interfaces:**
- Produces: `bool hv_irq_route_register(u32 hw_irq, u32 vintid, bool level)`.
- Produces: `void hv_irq_routes_reset_dynamic(void)` for tests and new guest launches.
- Preserves: `hv_irq_route_from_hw`, `hv_irq_route_from_vintid`, incoming resolution, mask, and EOI behavior.

- [ ] **Step 1: Write failing route tests**

```c
assert(hv_irq_route_register(333, 865, true));
assert(hv_irq_route_from_hw(333)->vintid == 865);
assert(hv_irq_route_from_vintid(865)->hw_irq == 333);
assert(!hv_irq_route_register(334, 865, true));
assert(!hv_irq_route_register(333, 866, true));
assert(!hv_irq_route_register(857, 900, true));
```

Also fill the dynamic table to capacity and verify one additional insert returns false without corrupting existing routes.

- [ ] **Step 2: Verify failure**

Run: `make -C m1n1_windows test-hv-irq-routes`

Expected: compile failure for the missing registration API.

- [ ] **Step 3: Implement a fixed-capacity route table**

Keep existing static xHCI routes immutable. Use a small fixed array for dynamic routes, reject INTIDs below 32 or above 1019, reject the synthetic NVMe INTID, and reject duplicate physical or virtual keys. No heap allocation is permitted.

- [ ] **Step 4: Run route and vGIC tests**

Run:

```bash
make -C m1n1_windows test-hv-irq-routes
make -C m1n1_windows test-hv-vgic
```

Expected: PASS.

- [ ] **Step 5: Commit and push the m1n1 fork checkpoint**

```bash
git -C m1n1_windows add src/hv_irq_routes.c src/hv_irq_routes.h tests/hv_irq_routes_test.c
git -C m1n1_windows commit -m "feat: register bounded hardware IRQ routes"
```

Do not push until root tests and the launch-contract tests in Task 4 pass.

### Task 4: Add m1n1 Apple input preflight, mapping, and launch contract

**Files:**
- Create: `m1n1_windows/src/hv_apple_input.h`
- Create: `m1n1_windows/src/hv_apple_input.c`
- Create: `m1n1_windows/tests/hv_apple_input_contract_test.c`
- Modify: `m1n1_windows/src/hv_autonomous_runtime.c`
- Modify: `m1n1_windows/src/hv_launch_j313.c`
- Modify: `m1n1_windows/src/hv_launch_contract.h`
- Modify: `m1n1_windows/Makefile`

**Interfaces:**
- Produces: `bool hv_apple_input_preflight(struct hv_apple_input_state *out)`.
- Produces: `bool hv_apple_input_prepare(const struct hv_apple_input_state *state)`.
- State includes resolved bases, selected physical IRQ, guest INTID, GPIO pins, and `validated`/`prepared` flags.

- [ ] **Step 1: Write failing contract tests with injected readers**

Test exact-node success, wrong compatible string, wrong register base, GPIO pin out of range, interrupt group outside the candidate list, mapping failure, and route collision. A failure must leave `prepared == false` and must record zero hardware writes in the fake backend.

- [ ] **Step 2: Verify failure**

Run: `make -C m1n1_windows test-hv-apple-input-contract`

Expected: missing source/API failure.

- [ ] **Step 3: Implement read-only preflight**

Resolve `/arm-io/spi3`, its `hid-transport,spi` child, AP GPIO, nub GPIO, and candidate parent interrupts from the live ADT. Read the nub pin register to select its interrupt group. Compare every value with `hv_apple_input.generated.h` before returning `validated = true`.

- [ ] **Step 4: Implement prepare after validation**

Preserve/enable the required power domains and pinmux using existing m1n1 platform helpers, map only the three contract MMIO ranges with `hv_map_hw`, and register `selected_parent_irq -> 865` as a level route. Do not reset the HID device or access SPI data registers; Windows owns those operations.

- [ ] **Step 5: Integrate assisted and standalone launches**

Both launch paths call the same preflight/prepare functions before guest entry. Extend the launch snapshot so assisted and standalone captures contain the input MMIO regions and selected route. A preflight mismatch is a hard launch refusal with one concise diagnostic line.

- [ ] **Step 6: Run all relevant m1n1 tests**

Run:

```bash
make -C m1n1_windows test-hv-apple-input-contract
make -C m1n1_windows test-hv-irq-routes
make -C m1n1_windows test-hv-launch-contract
make -C m1n1_windows test-hv-launch-golden-j313
```

Expected: PASS, with assisted and standalone golden contracts equal.

- [ ] **Step 7: Amend the fork checkpoint and push**

```bash
git -C m1n1_windows add src/hv_apple_input.c src/hv_apple_input.h \
  src/hv_autonomous_runtime.c src/hv_launch_j313.c src/hv_launch_contract.h \
  src/hv_apple_input.generated.h tests/hv_apple_input_contract_test.c Makefile
git -C m1n1_windows commit -m "feat: prepare native J313 input passthrough"
git -C m1n1_windows push fork HEAD:main
```

### Task 5: Publish the ACPI input devnode

**Files:**
- Create: `tests/test_apple_input_acpi.py`
- Modify: `mu/Platform/MacBookAirMid2020Pkg/AcpiTables/DSDT.asl`
- Use: `mu/Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleInput.asl.inc`

**Interfaces:**
- Consumes: generated `Device(AINP)` from Task 2.
- Produces: `ACPI\APPL0001\0` with service initially absent, then `AppleInput` after INF installation.

- [ ] **Step 1: Write failing ACPI tests**

Assert `_HID`, `_UID`, `_CCA`, three exact noncacheable read/write memory ranges, guest INTID 865 as level-high from the guest's perspective, and every `_DSD` contract property. Assert the ranges do not overlap RAM, framebuffer, ECAM, NVMe BAR, xHCI, or UART declarations.

- [ ] **Step 2: Verify failure**

Run: `proxyenv/bin/python -m unittest tests.test_apple_input_acpi -v`

Expected: FAIL because DSDT does not include `J313AppleInput.asl.inc`.

- [ ] **Step 3: Include the generated node and build Mu**

Include the file once inside `Scope(\_SB)`. Build with the existing J313 Mu command used by `scripts/build-development.sh`; do not manually edit the generated ASL.

- [ ] **Step 4: Run tests and inspect compiled AML**

Run:

```bash
proxyenv/bin/python -m unittest tests.test_apple_input_acpi -v
scripts/build-development.sh --display physical --debug full
```

Disassemble the built DSDT with `iasl -d`, then confirm `AINP`, `APPL0001`, resources, and `_DSD` survive compilation.

- [ ] **Step 5: Commit and push Mu**

```bash
git -C mu add Platform/MacBookAirMid2020Pkg/AcpiTables/DSDT.asl \
  Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleInput.asl.inc
git -C mu commit -m "feat: describe native J313 Apple input"
git -C mu push fork HEAD:main
```

### Task 6: Implement portable packet framing and CRC

**Files:**
- Create: `drivers/apple-input/protocol/include/apple_spihid.h`
- Create: `drivers/apple-input/protocol/src/apple_spihid_crc.c`
- Create: `drivers/apple-input/protocol/src/apple_spihid_packet.c`
- Create: `drivers/apple-input/protocol/tests/apple_spihid_test.c`
- Create: `tests/test_apple_spihid_protocol.py`

**Interfaces:**
- Produces: `uint16_t ai_crc16_usb(uint16_t seed, const uint8_t *data, size_t size)`.
- Produces: `ai_status ai_packet_decode(const uint8_t raw[256], ai_packet_view *out)`.
- Produces: `ai_status ai_request_encode(const ai_request *request, uint8_t raw[256])`.
- Constants: packet 256, payload 246, maximum assembled report 2048, maximum descriptor 512.

- [ ] **Step 1: Add failing native tests**

Test empty and captured CRC vectors, exact packet field endianness, maximum lengths, bad packet CRC, bad message CRC, zero/oversized lengths, and request ID rollover.

- [ ] **Step 2: Verify failure**

Run: `proxyenv/bin/python -m unittest tests.test_apple_spihid_protocol -v`

Expected: compile failure because the protocol sources are absent.

- [ ] **Step 3: Implement minimal packet code**

Use byte loads/stores rather than packed-struct dereferences. All public functions validate null pointers and sizes. Return stable `AI_STATUS_*` values; do not print, allocate, sleep, or access hardware.

- [ ] **Step 4: Run native and sanitizer tests**

The Python wrapper compiles once with `-std=c11 -Wall -Wextra -Werror` and once with AddressSanitizer/UndefinedBehaviorSanitizer when supported.

Run: `proxyenv/bin/python -m unittest tests.test_apple_spihid_protocol -v`

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add drivers/apple-input/protocol tests/test_apple_spihid_protocol.py
git commit -m "feat: decode Apple SPI HID packets"
```

### Task 7: Implement bounded reassembly and discovery

**Files:**
- Create: `drivers/apple-input/protocol/src/apple_spihid_reassembly.c`
- Create: `drivers/apple-input/protocol/src/apple_spihid_discovery.c`
- Modify: `drivers/apple-input/protocol/include/apple_spihid.h`
- Modify: `drivers/apple-input/protocol/tests/apple_spihid_test.c`

**Interfaces:**
- Produces: `void ai_reassembler_init(ai_reassembler *state)`.
- Produces: `ai_status ai_reassembler_push(ai_reassembler *state, const ai_packet_view *packet, ai_message_view *complete)`.
- Produces: `void ai_discovery_init(ai_discovery *state, uint64_t now_ms)`.
- Produces: `ai_action ai_discovery_next(ai_discovery *state, uint64_t now_ms)` and `ai_status ai_discovery_accept(...)`.
- Discovery phases: `OFFLINE`, `WAIT_BOOT`, `DEVICE_INFO`, `INTERFACE_INFO`, `DESCRIPTORS`, `READY`, `RECOVERING`.

- [ ] **Step 1: Add failing state-machine tests**

Cover one/two/many fragments, wrong device/flags, overlap, gaps, reorder, duplicate, arithmetic overflow, descriptor limit, response-ID mismatch, deadline expiry, retry exhaustion, and the exact successful sequence boot → identity → three interface infos → keyboard/trackpad descriptors → ready.

- [ ] **Step 2: Verify failure**

Run: `proxyenv/bin/python -m unittest tests.test_apple_spihid_protocol -v`

Expected: missing symbols or failed assertions.

- [ ] **Step 3: Implement without heap allocation**

All buffers live inside caller-owned state. `ai_discovery_next` returns actions (`SEND_REQUEST`, `WAIT_PACKET`, `RESET_DEVICE`, `PUBLISH_READY`, `GO_OFFLINE`) and never calls platform code.

- [ ] **Step 4: Run sanitizer and full root tests**

Run:

```bash
proxyenv/bin/python -m unittest tests.test_apple_spihid_protocol -v
proxyenv/bin/python -m unittest discover -s tests -v
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add drivers/apple-input/protocol
git commit -m "feat: assemble and discover Apple SPI HID devices"
```

### Task 8: Scaffold the ARM64 KMDF package and resource validation

**Files:**
- Create all solution/project/INF, `driver.c`, `device.c`, headers, signing scripts, and `tests/test_apple_input_windows_package.py` listed in the file map.

**Interfaces:**
- Produces: `NTSTATUS AppleInputCreateDevice(WDFDRIVER, PWDFDEVICE_INIT)`.
- Produces: `NTSTATUS AiDeviceParseResources(WDFCMRESLIST raw, WDFCMRESLIST translated, AI_DEVICE_CONTEXT *ctx)`.
- Produces: `EvtDevicePrepareHardware`, `EvtDeviceReleaseHardware`, `EvtDeviceD0Entry`, and `EvtDeviceD0Exit` callbacks.
- INF matches `ACPI\APPL0001`, service `AppleInput`, ARM64 only, with `Vhf` as the required lower filter.

- [ ] **Step 1: Write failing static package tests**

Assert the INF hardware ID/service/copy sections, KMDF and VHF declarations, ARM64 target, project source list, generated header use, test-signing scripts, location independence, uninstall/recovery commands, and absence of private paths/certificate material.

- [ ] **Step 2: Verify failure**

Run: `proxyenv/bin/python -m unittest tests.test_apple_input_windows_package -v`

Expected: missing package files.

- [ ] **Step 3: Implement the smallest buildable KMDF function driver**

The device-add path creates queues and callbacks but performs no MMIO writes. Resource parsing requires exactly the generated three memory resources and INTID 865. Unexpected resources return `STATUS_DEVICE_CONFIGURATION_ERROR`.

- [ ] **Step 4: Build ARM64 and run InfVerif**

On the Windows WDK build machine:

```powershell
./scripts/build-driver.ps1 -Configuration Debug -Platform ARM64
InfVerif.exe /w .\x64-or-arm64-output\AppleInput.inf
```

Expected: `AppleInput.sys`, catalog inputs, and INF validation succeed. The script must discover Visual Studio with `vswhere.exe`, not embed a developer path.

- [ ] **Step 5: Commit**

```bash
git add drivers/apple-input/windows tests/test_apple_input_windows_package.py
git commit -m "feat: scaffold ARM64 Apple input driver"
```

### Task 9: Implement bounded SPI3 and GPIO primitives

**Files:**
- Create: `drivers/apple-input/windows/src/spi.c`
- Create: `drivers/apple-input/windows/src/gpio.c`
- Modify: `drivers/apple-input/windows/include/apple_input_device.h`
- Modify: `drivers/apple-input/windows/AppleInput.vcxproj`
- Modify: `tests/test_apple_input_windows_package.py`

**Interfaces:**
- Produces: `NTSTATUS AiSpiInitialize(AI_DEVICE_CONTEXT *ctx)`.
- Produces: `NTSTATUS AiSpiTransfer(AI_DEVICE_CONTEXT *ctx, const uint8_t *tx, uint8_t *rx, size_t length, uint64_t deadline_qpc)`.
- Produces: `NTSTATUS AiGpioResetInputController(AI_DEVICE_CONTEXT *ctx)`.
- Produces: `BOOLEAN AiGpioInputAsserted(AI_DEVICE_CONTEXT *ctx)` and `void AiGpioAcknowledge(AI_DEVICE_CONTEXT *ctx)`.

- [ ] **Step 1: Add compile-time/register-contract tests**

Add a host-compilable register helper test covering field masks, divider calculation (`ceil(120 MHz / 8 MHz) == 15`), FIFO bounds, deadline arithmetic, GPIO mode values, and pin-register offsets for pins 195 and 13.

- [ ] **Step 2: Verify failure**

Run the root Windows-package test and the native helper test; expect missing helpers.

- [ ] **Step 3: Implement polled SPI and narrow GPIO access**

Use only generated bases and the resource-mapped virtual addresses. Reset FIFOs before each failed retry, keep CS inactive on every exit path, transfer at most 256 bytes per call, and use QPC-based deadlines. Touch no AP GPIO pin except 195 and no nub GPIO pin except 13.

- [ ] **Step 4: Build with Code Analysis**

Run:

```powershell
./scripts/build-driver.ps1 -Configuration Debug -Platform ARM64 -CodeAnalysis
```

Expected: no warnings promoted to errors and no Static Driver Verifier rule violation in the selected KMDF ruleset.

- [ ] **Step 5: Commit**

```bash
git add drivers/apple-input/windows tests
git commit -m "feat: access J313 Apple SPI input hardware"
```

### Task 10: Integrate passive transport-only operation and diagnostics

**Files:**
- Create: `drivers/apple-input/windows/src/transport.c`
- Create: `drivers/apple-input/windows/src/diagnostics.c`
- Create: `drivers/apple-input/windows/tools/AppleInputDiag/*`
- Modify: `drivers/apple-input/windows/src/device.c`
- Modify: `drivers/apple-input/windows/include/apple_input_ioctl.h`
- Create: `tools/kd/kd_apple_input.py`
- Modify: `tests/test_apple_input_windows_package.py`

**Interfaces:**
- Produces: passive worker `AiTransportWorker` and interrupt callback `AiInputInterruptIsr`.
- Produces: versioned `AI_DIAGNOSTIC_SNAPSHOT_V1` through read-only IOCTL `IOCTL_AI_GET_SNAPSHOT`.
- Registry value `TransportOnly` defaults to 1 in Debug and 0 only after Task 11 succeeds.

- [ ] **Step 1: Add failing lifecycle/diagnostic contract tests**

Assert ISR source contains no SPI call, allocation, sleep, or VHF submission; snapshot fields include interrupt/worker/SPI/CRC/fragment/report/reset/offline counters; IOCTL rejects output buffers smaller than the versioned structure; the bounded header ring contains no packet payload.

- [ ] **Step 2: Verify failure**

Run: `proxyenv/bin/python -m unittest tests.test_apple_input_windows_package -v`

Expected: missing transport and diagnostic symbols.

- [ ] **Step 3: Implement ISR → passive worker → protocol actions**

Mask/ack the source, queue at most one worker, drain a bounded maximum of 32 packets, execute discovery actions, publish counters, then re-enable the level source only after confirming it is deasserted or another worker is queued.

- [ ] **Step 4: Implement diagnostic clients**

`AppleInputDiag.exe status` opens the device interface and prints the versioned snapshot as text or `--json`. `kd_apple_input.py` locates `ACPI\APPL0001`, prints State/Problem/Service, and reads exported symbol-backed counters while always continuing the guest in `finally`.

- [ ] **Step 5: Build and run static/root tests**

Run ARM64 WDK build plus:

```bash
proxyenv/bin/python -m unittest tests.test_apple_input_windows_package -v
proxyenv/bin/python -m unittest discover -s tests -v
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add drivers/apple-input/windows tools/kd/kd_apple_input.py tests
git commit -m "feat: run Apple input transport with bounded diagnostics"
```

### Task 11: Hardware Gate A — prove transport before publishing input

**Files:**
- Local only: `.local/apple-input/transport-only-*`
- Update after success: `documentation/APPLE_INPUT.md`

**Interfaces:**
- Consumes: test-signed Debug ARM64 package with `TransportOnly=1`.
- Produces: identity, interface metadata, keyboard descriptor, trackpad descriptor, and bounded input packet captures.

- [ ] **Step 1: Enable test-signing with an explicit recovery path**

In an elevated Windows terminal:

```powershell
bcdedit /set testsigning on
shutdown /r /t 0
```

Before reboot, document that `bcdedit /deletevalue testsigning` disables it and that `verifier /reset` is the recovery command after later verifier testing.

- [ ] **Step 2: Install the transport-only package**

```powershell
./scripts/new-test-certificate.ps1
./scripts/build-driver.ps1 -Configuration Debug -Platform ARM64 -Sign
./scripts/install-driver.ps1 -TransportOnly
pnputil /enum-devices /instanceid 'ACPI\APPL0001\0' /drivers
```

Expected: devnode Started, service `AppleInput`, no keyboard/mouse child yet.

- [ ] **Step 3: Capture each transport checkpoint**

Run `AppleInputDiag.exe status --json` after boot packet, identity, interface metadata, descriptors, and key/touch activity. Copy only explicitly sanitized descriptors/reports into test fixtures; keep full captures under `.local/`.

- [ ] **Step 4: Exercise failure recovery**

Restart the devnode with `pnputil /restart-device`, verify re-discovery, then uninstall/reinstall. Confirm USB input, system clock, RDP, and NVMe remain responsive. Any hang or bugcheck blocks VHF work.

- [ ] **Step 5: Record the proven contract and commit sanitized fixtures**

Update `documentation/APPLE_INPUT.md` with actual IDs, descriptor hashes, selected physical IRQ, timings, and counter progression. Commit only sanitized fixtures and documentation.

### Task 12: Publish the hardware keyboard through VHF

**Files:**
- Create: `drivers/apple-input/windows/src/vhf.c`
- Modify: `drivers/apple-input/windows/src/transport.c`
- Modify: `drivers/apple-input/windows/include/apple_input_device.h`
- Modify: `drivers/apple-input/protocol/tests/apple_spihid_test.c`
- Modify: `tests/test_apple_input_windows_package.py`

**Interfaces:**
- Produces: `NTSTATUS AiVhfCreateKeyboard(AI_DEVICE_CONTEXT *ctx, const uint8_t *descriptor, size_t size)`.
- Produces: `NTSTATUS AiVhfSubmitKeyboard(AI_DEVICE_CONTEXT *ctx, const uint8_t *report, size_t size)`.
- Produces: `void AiVhfReleaseAll(AI_DEVICE_CONTEXT *ctx)`.

- [ ] **Step 1: Add failing keyboard fixture tests**

Use the sanitized hardware descriptor/report fixture to verify allowed report IDs and lengths, reject mismatches, track pressed keys, and synthesize an all-released report on reset/removal.

- [ ] **Step 2: Verify failure**

Run protocol and Windows-package tests; expect missing VHF functions.

- [ ] **Step 3: Create VHF only after discovery**

Pass the exact validated keyboard report descriptor to `VhfCreate`, start it after callbacks are installed, submit only reports matching the descriptor-derived bounds, and destroy it after the worker is stopped during cleanup.

- [ ] **Step 4: Hardware Gate B**

Install with keyboard publication enabled and trackpad publication disabled. Verify typing at sign-in and desktop, modifiers, repeat, Caps Lock output, devnode restart, unplugged external USB input, and forced transport reset with no stuck keys.

- [ ] **Step 5: Commit**

```bash
git add drivers/apple-input tests documentation/APPLE_INPUT.md
git commit -m "feat: publish the native Apple keyboard"
```

### Task 13: Translate the first trackpad contact into a basic mouse

**Files:**
- Create: `drivers/apple-input/windows/src/trackpad.c`
- Modify: `drivers/apple-input/windows/src/vhf.c`
- Modify: `drivers/apple-input/windows/src/transport.c`
- Modify: `drivers/apple-input/protocol/include/apple_spihid.h`
- Modify: `drivers/apple-input/protocol/tests/apple_spihid_test.c`

**Interfaces:**
- Produces: `ai_status ai_trackpad_decode(const ai_hid_descriptor *descriptor, const uint8_t *report, size_t size, ai_contact_frame *out)`.
- Produces: `void AiTrackpadTranslate(AI_DEVICE_CONTEXT *ctx, const ai_contact_frame *frame, AI_MOUSE_REPORT *out)`.
- Translation uses one confident active contact, bounded acceleration-free deltas, and one valid press state.

- [ ] **Step 1: Add failing descriptor/report fixture tests**

Cover no contact, contact down/move/up, coordinate wrap, two contacts selecting a stable primary contact, invalid report length, confidence false, press/release, reset during press, and maximum delta clamping.

- [ ] **Step 2: Verify failure**

Run: `proxyenv/bin/python -m unittest tests.test_apple_spihid_protocol -v`

Expected: missing decoder/translator behavior.

- [ ] **Step 3: Implement descriptor-driven extraction and fixed mouse descriptor**

Do not hard-code byte offsets without a checked descriptor hash and explicit J313 fixture. Reset the motion origin on contact loss or primary-contact change. Submit zero-button state before destroying/restarting VHF.

- [ ] **Step 4: Hardware Gate C**

Verify stable cursor motion, click, click-drag, contact lift, keyboard while touching, devnode restart, and USB fallback. Run 30 minutes of mixed input while monitoring clock, RDP, NVMe I/O, interrupt rate, timeouts, and reset counters.

- [ ] **Step 5: Commit**

```bash
git add drivers/apple-input tests documentation/APPLE_INPUT.md
git commit -m "feat: add basic native Apple trackpad input"
```

### Task 14: Harden lifecycle, verifier recovery, documentation, and release

**Files:**
- Modify: Windows driver lifecycle and diagnostics sources.
- Modify: `documentation/APPLE_INPUT.md`
- Modify: `documentation/ARCHITECTURE.md`
- Modify: `documentation/DEBUGGING.md`
- Modify: `documentation/LIMITATIONS.md`
- Modify: `README.md`
- Modify: `tests/test_public_documentation.py`

**Interfaces:**
- Produces: documented install, uninstall, test-signing, capture, devnode restart, Driver Verifier, boot recovery, and transition-to-Precision-Touchpad procedures.

- [ ] **Step 1: Add failing documentation truth tests**

Require exact test-signing and rollback commands, `pnputil` install/restart/remove commands, transport-only mode, diagnostic CLI/KD commands, verifier reset, current milestone limitations, and the required Precision Touchpad follow-up.

- [ ] **Step 2: Verify failure**

Run: `proxyenv/bin/python -m unittest tests.test_public_documentation -v`

Expected: FAIL until the runbook is complete.

- [ ] **Step 3: Run selected Driver Verifier checks**

After a known-good backup and with USB input available:

```powershell
verifier /standard /driver AppleInput.sys
shutdown /r /t 0
```

Exercise start, input, devnode restart, and shutdown. Then disable verifier:

```powershell
verifier /reset
shutdown /r /t 0
```

If boot fails, use Windows Recovery Command Prompt and run `verifier /reset`; do not repeatedly hard-power-cycle into recovery.

- [ ] **Step 4: Verify clean shutdown and repeated cold boots**

Perform at least three normal shutdown/cold-boot cycles and one driver uninstall/reinstall. Confirm no recovery boot, stuck input, or unbounded counters.

- [ ] **Step 5: Run final automated verification**

Run:

```bash
proxyenv/bin/python -m unittest discover -s tests -v
git diff --check
python3 tools/generate_guest_layout.py --check
python3 tools/generate_apple_input_contract.py --check
make -C m1n1_windows test-hv-apple-input-contract
make -C m1n1_windows test-hv-irq-routes
make -C m1n1_windows test-hv-launch-golden-j313
```

On Windows, run the Debug and Release ARM64 WDK builds plus `InfVerif`.

- [ ] **Step 6: Audit and commit**

Before every push:

```bash
git diff --cached --check
git diff --cached | rg -n -i 'co-authored-by|claude-session|codex|anthropic'
git status --short
```

The attribution search must produce no match. Commit the final documentation and release metadata with a technical message, then fast-forward the public fork branches and root submodule pointers. Do not tag milestone 1 until all three hardware gates and the 30-minute input test are recorded as passing.
