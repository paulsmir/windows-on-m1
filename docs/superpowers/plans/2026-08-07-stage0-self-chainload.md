# Stage 0 Self-Chainload Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make an installed standalone image reload a validated inner m1n1 image through the normal next-stage boundary before the existing autonomous guest loader initializes the hypervisor.

**Architecture:** The installed `boot.bin` becomes an outer Stage 0 m1n1 plus a versioned bootstrap manifest containing a compressed copy of the existing standalone image. Stage 0 validates and decompresses the inner image, prepares the same preserved-state handoff as assisted `chainload.py`, sets `next_stage` through `chainload_image()`, and returns to the top-level shutdown/vector path. Stage 1 remains the current autonomous image and is byte-identical to the direct Clang control apart from its embedding in the outer container.

**Tech Stack:** Python 3.10+ packer/parser and unittest, freestanding C11, XZ/minilzma, m1n1 chainload stub and ADT, native C host tests, J313 passive USB monitor.

## Global Constraints

- Work only in `/Users/pavel/public_windows`; keep `/Users/pavel/windows` read-only as assisted evidence.
- Use the approved two-stage architecture in `docs/superpowers/specs/2026-08-07-self-chainload-shared-guest-engine-design.md`.
- Preserve the inner m1n1, Mu FD, guest manifest flags `0x11`, and guest layout from the Clang control.
- The outer magic must differ from `ASIWINGU`; old and new formats must reject one another rather than fall back ambiguously.
- Stage 0 must not call `hv_init()`, map guest devices, initialize NVMe, or enter Mu.
- Monitor mode may expose passive USB output but must not accept proxy takeover or wait indefinitely.
- Guest preparation still occurs only in Stage 1 during this phase; the shared guest-engine refactor is a later plan.
- Chainload preservation must cover SEPFW, optional `preoslog`, the ADT `BootArgs` property, boot arguments, and secondary CPU RVBAR preparation.
- Validation or decompression failure falls back to the existing proxy/recovery path; it must not reboot.
- Generated images and hardware logs remain under ignored `.local/`; commits contain source, tests, documentation, and submodule pointers only.
- Do not add co-author or session trailers.

---

### Task 1: Define and test the outer bootstrap image format

**Files:**
- Create: `bootstrap_image.py`
- Modify: `tools/pack_boot.py`
- Create: `tests/test_bootstrap_image.py`
- Modify: `tests/test_standalone_image.py`

**Interfaces:**
- Consumes: an aligned Stage 0 raw m1n1 image and a complete inner image returned by `standalone_image.pack_image()`.
- Produces: `BootstrapManifest`, `pack_bootstrap(stage0: bytes, inner: bytes, flags: int) -> bytes`, and `parse_bootstrap(image: bytes) -> tuple[BootstrapManifest, bytes]`.
- Binary ABI: magic `b"ASIBOOT0"`, version `1`, header size `64`, alignment `0x4000`, struct `<8sHHIIQQQII12s`.

- [ ] **Step 1: Write failing Python format tests**

Create `tests/test_bootstrap_image.py` with cases that assert:

```python
image = api.pack_bootstrap(b"s" * 0x4000, b"inner-image", flags=0x11)
manifest, inner = api.parse_bootstrap(image)
self.assertEqual(manifest.flags, 0x11)
self.assertEqual(manifest.manifest_offset, 0x4000)
self.assertEqual(manifest.payload_offset, 0x4000)
self.assertEqual(inner, b"inner-image")
```

Add corruption, CRC mismatch after valid decompression, zero sizes, overflow/bounds, nonzero reserved fields, unsupported flags, and multiple-magic rejection. Assert that `standalone_image.parse_image(outer)` raises `ImageError("multiple|standalone manifest")` and `parse_bootstrap(inner)` raises `BootstrapImageError("bootstrap manifest")`.

- [ ] **Step 2: Run the focused tests and verify failure**

Run:

```bash
python3 -m unittest tests.test_bootstrap_image tests.test_standalone_image -v
```

Expected: import failure for missing `bootstrap_image`; existing standalone tests remain green when run separately.

- [ ] **Step 3: Implement the pure outer format**

In `bootstrap_image.py`, define:

```python
BOOTSTRAP_MAGIC = b"ASIBOOT0"
BOOTSTRAP_FORMAT_VERSION = 1
BOOTSTRAP_ALIGNMENT = 0x4000
BOOTSTRAP_HEADER_SIZE = 64
_BOOTSTRAP = struct.Struct("<8sHHIIQQQII12s")

@dataclass(frozen=True)
class BootstrapManifest:
    manifest_offset: int
    format_version: int
    header_size: int
    flags: int
    payload_offset: int
    compressed_size: int
    uncompressed_size: int
    crc32: int
```

`pack_bootstrap()` validates nonempty inputs, Stage 0 alignment, and launch flags with `profile_from_manifest_flags()`; compresses the complete inner image as XZ; emits zero reserved fields/padding; and returns Stage 0 + header + padding + payload. `parse_bootstrap()` finds exactly one aligned bootstrap magic, validates every field before addition/slicing, requires the compressed payload to end exactly at EOF, decompresses with `lzma.FORMAT_XZ`, and verifies size and CRC32.

- [ ] **Step 4: Extend the packer CLI without breaking direct-image inspection**

Add optional `--stage0-m1n1` and rename the existing semantic input to `--stage1-m1n1`, retaining `--m1n1` as a deprecated alias for direct-image tests during this phase. When `--stage0-m1n1` is present:

```python
inner = pack_image(stage1, firmware, layout_version=layout.layout_version,
                   flags=profile.manifest_flags)
image = pack_bootstrap(stage0, inner, flags=profile.manifest_flags)
outer, decoded_inner = parse_bootstrap(image)
inner_manifest, decoded_firmware = parse_image(decoded_inner)
assert decoded_firmware == firmware
```

Reject simultaneous `--m1n1` and `--stage1-m1n1`. Print both manifest offsets, both CRC values, the final SHA-256, and the profile.

- [ ] **Step 5: Run focused and complete Python tests**

Run:

```bash
python3 -m unittest tests.test_bootstrap_image tests.test_standalone_image tests.test_build_standalone -v
python3 -m unittest discover -s tests -v
```

Expected: all tests pass and no existing direct-image fixture becomes ambiguous.

- [ ] **Step 6: Commit the host format contract**

Run:

```bash
git add bootstrap_image.py tools/pack_boot.py tests/test_bootstrap_image.py tests/test_standalone_image.py
git commit -m "feat: define standalone bootstrap image"
```

### Task 2: Add the native bootstrap manifest parser

**Files:**
- Create: `m1n1_windows/src/hv_bootstrap_manifest.h`
- Create: `m1n1_windows/src/hv_bootstrap_manifest.c`
- Create: `m1n1_windows/tests/hv_bootstrap_manifest_test.c`
- Modify: `m1n1_windows/tests/run_host_tests.sh`
- Modify: `m1n1_windows/Makefile`
- Modify: `tests/test_bootstrap_image.py`

**Interfaces:**
- Consumes: the 64-byte little-endian ABI from Task 1 at m1n1 `_payload_start`.
- Produces: `bool hv_bootstrap_manifest_parse(const void *image_end, size_t available, struct hv_bootstrap_payload *out, enum hv_bootstrap_error *error)`.
- Output fields: `compressed`, `compressed_size`, `uncompressed_size`, `crc32`, and `flags`.
- Shared flag predicate: `bool hv_autonomous_flags_valid(uint32_t flags)`, consumed by both autonomous and bootstrap parsers.

- [ ] **Step 1: Write the failing C parser test**

Model `hv_bootstrap_manifest_test.c` on `hv_autonomous_manifest_test.c`. Test a valid `0x11` manifest and exact errors for null, truncated, magic, version, header size, flags, alignment, zero size, integer overflow, payload bounds, and nonzero reserved data. Add `hv_bootstrap_manifest_test` to `all_tests` and map it to `src/hv_bootstrap_manifest.c`.

- [ ] **Step 2: Run the focused native test and verify failure**

Run:

```bash
m1n1_windows/tests/run_host_tests.sh hv_bootstrap_manifest_test
```

Expected: compilation fails because the header and implementation do not exist.

- [ ] **Step 3: Implement the ABI and parser**

Define in `hv_bootstrap_manifest.h`:

```c
#define HV_BOOTSTRAP_MAGIC "ASIBOOT0"
#define HV_BOOTSTRAP_FORMAT_VERSION 1u
#define HV_BOOTSTRAP_IMAGE_ALIGNMENT 0x4000u
#define HV_BOOTSTRAP_MANIFEST_SIZE 64u

struct hv_bootstrap_manifest {
    uint8_t magic[8];
    uint16_t format_version;
    uint16_t header_size;
    uint32_t flags;
    uint32_t reserved;
    uint64_t payload_offset;
    uint64_t compressed_size;
    uint64_t uncompressed_size;
    uint32_t crc32;
    uint32_t reserved2;
    uint8_t reserved_tail[12];
};
```

Use a static-size assertion. The parser follows the checked-addition and `SIZE_MAX` rules of `hv_autonomous_manifest_parse()`, and validates flags by calling a shared pure launch-flag predicate exported from `hv_autonomous_profile.c` rather than duplicating the debug-mask rules.

- [ ] **Step 4: Add a Python/C ABI parity test**

Extend `tests/test_bootstrap_image.py` to read `hv_bootstrap_manifest.h` and compare magic, version, alignment, and size constants to Python. Do not assert source formatting or implementation text.

- [ ] **Step 5: Run native and Python tests**

Run:

```bash
m1n1_windows/tests/run_host_tests.sh hv_bootstrap_manifest_test hv_autonomous_profile_test
python3 -m unittest tests.test_bootstrap_image -v
```

Expected: all pass.

- [ ] **Step 6: Commit the native parser**

Commit inside the submodule:

```bash
git add src/hv_bootstrap_manifest.h src/hv_bootstrap_manifest.c src/hv_autonomous_profile.h src/hv_autonomous_profile.c tests/hv_bootstrap_manifest_test.c tests/hv_autonomous_profile_test.c tests/run_host_tests.sh Makefile
git commit -m "feat: validate standalone bootstrap manifest"
```

Then commit the root test/submodule pointer:

```bash
git add m1n1_windows tests/test_bootstrap_image.py
git commit -m "feat: integrate native bootstrap manifest"
```

### Task 3: Make the C chainload boundary preserve assisted state

**Files:**
- Create: `m1n1_windows/src/chainload_layout.h`
- Create: `m1n1_windows/src/chainload_layout.c`
- Create: `m1n1_windows/tests/chainload_layout_test.c`
- Modify: `m1n1_windows/src/chainload.c`
- Modify: `m1n1_windows/tests/run_host_tests.sh`
- Modify: `m1n1_windows/Makefile`

**Interfaces:**
- Produces: `bool chainload_layout_compute(size_t image_and_vars, size_t sepfw_size, size_t preoslog_size, size_t stub_size, struct chainload_layout *out)`.
- `struct chainload_layout` contains aligned `sepfw_offset`, `preoslog_offset`, `bootargs_offset`, `stub_offset`, `copy_size`, and `allocation_size`.
- `chainload_image()` continues to set `next_stage`; its public signature does not change.

- [ ] **Step 1: Write failing checked-layout tests**

Cover aligned/unaligned image sizes, absent/present preoslog, 16 KiB bootargs, stub placement outside the copied region, and overflow at every addition/alignment boundary. Expected layout order is image/variables, SEPFW, preoslog, bootargs, then chainload stub.

- [ ] **Step 2: Run the focused test and verify failure**

Run:

```bash
m1n1_windows/tests/run_host_tests.sh chainload_layout_test
```

Expected: compilation fails for the missing interface.

- [ ] **Step 3: Implement the pure checked layout and use it in chainload.c**

Replace unchecked `size_t` arithmetic in `chainload_image()` with `chainload_layout_compute()`. Read optional `preoslog` from `/chosen/memory-map`; copy it when present; update its ADT tuple. Always update both `SEPFW` and `BootArgs` ADT tuples to the new physical addresses before copying the boot args.

- [ ] **Step 4: Prepare secondary RVBARs using the assisted rule**

Add a private `chainload_prepare_rvbar(u64 entry)` in `chainload.c`. Walk `/cpus`, skip the node whose state is `running`, resolve each `cpu-impl-reg`, and write `entry & ~0xfffULL` followed by `dmb sy`. Treat a missing CPU implementation register as a chainload error before `next_stage` is published.

- [ ] **Step 5: Validate failure atomicity**

Order all fallible ADT lookups, layout validation, and allocation before assigning `next_stage.entry`. If an ADT property update fails after allocation, free the allocation and return `-1`; no caller may observe a partially published next stage.

- [ ] **Step 6: Run native tests and build m1n1**

Run:

```bash
m1n1_windows/tests/run_host_tests.sh chainload_layout_test
make -C m1n1_windows -j8
```

Expected: focused test and firmware build pass.

- [ ] **Step 7: Commit chainload parity**

Commit inside the submodule:

```bash
git add src/chainload_layout.h src/chainload_layout.c src/chainload.c tests/chainload_layout_test.c tests/run_host_tests.sh Makefile
git commit -m "fix: preserve state across native chainload"
```

Then commit the root submodule pointer:

```bash
git add m1n1_windows
git commit -m "fix: update native chainload boundary"
```

### Task 4: Decompress and dispatch Stage 0 without entering the hypervisor

**Files:**
- Create: `m1n1_windows/src/hv_bootstrap.h`
- Create: `m1n1_windows/src/hv_bootstrap.c`
- Create: `m1n1_windows/tests/hv_bootstrap_test.c`
- Modify: `m1n1_windows/src/main.c`
- Modify: `m1n1_windows/tests/run_host_tests.sh`
- Modify: `m1n1_windows/Makefile`

**Interfaces:**
- Produces: `enum hv_bootstrap_attempt hv_bootstrap_chainload_if_present(bool *usb_up)` with `ABSENT`, `HANDLED`, and `ATTEMPT_FAILED`.
- Pure test seam: `enum hv_bootstrap_result hv_bootstrap_prepare_with_ops(const struct hv_bootstrap_payload *payload, const struct hv_bootstrap_ops *ops, void *opaque)`.
- Operations: `void *(*allocate)(size_t size, size_t alignment, void *opaque)`, `bool (*decompress)(const void *source, u32 *source_size, void *destination, u32 *destination_size, void *opaque)`, `u32 (*crc32)(const void *data, size_t size, void *opaque)`, and `int (*chainload)(void *image, size_t size, void *opaque)`.

- [ ] **Step 1: Write failing runtime-policy tests**

Test that valid input calls allocate, decompress, CRC, and chainload exactly once in that order. Test allocation failure, decoder partial consumption/output, CRC mismatch, and chainload failure; each stops immediately and returns its specific result. Test absent outer magic separately from malformed outer format.

- [ ] **Step 2: Run the focused test and verify failure**

Run:

```bash
m1n1_windows/tests/run_host_tests.sh hv_bootstrap_test
```

Expected: compilation fails for the missing runtime.

- [ ] **Step 3: Implement the injected preparation engine**

The production operations use `heapblock_alloc_aligned(size, SZ_16K)`, `XzDecode()`, `tinf_crc32()`, and `chainload_image(inner, inner_size, NULL, 0)`. Require complete input consumption and exact output size. Print stable stages `BOOTSTRAP_VALIDATE`, `BOOTSTRAP_DECOMPRESS`, `BOOTSTRAP_VERIFY`, and `BOOTSTRAP_CHAINLOAD`.

- [ ] **Step 4: Add passive monitor handling at Stage 0**

Decode the outer flags. For monitor, initialize USB only when not already up, configure console/vUART iodevs, service them for the existing three-second enumeration window, and never call `uartproxy_run()`. The chainload top-level shutdown disconnects Stage 0 cleanly; Stage 1 re-enumerates and starts its own monitor window.

- [ ] **Step 5: Dispatch bootstrap before the guest manifest**

In `run_actions()`, call `hv_bootstrap_chainload_if_present(&usb_up)` before `hv_autonomous_boot_if_present()`:

```c
enum hv_bootstrap_attempt bootstrap = hv_bootstrap_chainload_if_present(&usb_up);
if (bootstrap == HV_BOOTSTRAP_HANDLED)
    return;
if (bootstrap == HV_BOOTSTRAP_ATTEMPT_FAILED)
    goto proxy_fallback;
```

An absent outer magic continues to direct inner/autonomous handling, preserving assisted and direct-image compatibility.

- [ ] **Step 6: Run the complete native suite and build**

Run:

```bash
m1n1_windows/tests/run_host_tests.sh
make -C m1n1_windows -j8
```

Expected: all native tests pass and m1n1 links with the new objects.

- [ ] **Step 7: Commit Stage 0 runtime**

Commit inside the submodule:

```bash
git add src/hv_bootstrap.h src/hv_bootstrap.c src/main.c tests/hv_bootstrap_test.c tests/run_host_tests.sh Makefile
git commit -m "feat: self-chainload standalone stage one"
```

Then commit the root pointer:

```bash
git add m1n1_windows
git commit -m "feat: integrate standalone self-chainload"
```

### Task 5: Build and validate the two-stage hardware checkpoint

**Files:**
- Modify: `scripts/build-standalone.sh`
- Modify: `scripts/install-esp.sh`
- Modify: `tests/test_build_standalone.py`
- Modify: `tests/test_install_esp.py`
- Modify after hardware evidence: `documentation/DEVELOPMENT_HISTORY.md`
- Create (ignored): `.local/stage0-control/boot.bin`
- Create (ignored): `.local/stage0-control/inner.bin`
- Create (ignored): `.local/stage0-control/m1n1.elf`
- Create (ignored): `.local/stage0-monitor/generation-*/console.tlog`

**Interfaces:**
- Consumes: Stage 0/Stage 1 m1n1 from Task 4, the unchanged Mu FD SHA `64763cc61e0fdba693438386ea2125d3fe750ee1c1ff8845b8d62f63e7ea462a`, and profile flags `0x11`.
- Produces: an installed two-stage image plus a trace proving outer validation, vector handoff, fresh Stage 1, inner manifest discovery, and the CPU1 boundary.

- [ ] **Step 1: Make the build pipeline pack two stages**

Update the dry-run and real pipeline to build m1n1 once with the pinned supported toolchain, copy that raw binary as both Stage 0 and Stage 1 inputs for this checkpoint, and call:

```bash
python3 tools/pack_boot.py --stage0-m1n1 m1n1_windows/build/m1n1.bin --stage1-m1n1 m1n1_windows/build/m1n1.bin --firmware mu/Build/MacBookAirMid2020-AARCH64/DEBUG_CLANGPDB/FV/J313MACBOOKAIRMID2020_EFI.fd --layout config/j313-guest-layout.json --output dist/j313/boot.bin --display physical --debug monitor
```

Add ordered dry-run assertions for both m1n1 arguments and both manifest validations.

- [ ] **Step 2: Run all host tests and a full build**

Run:

```bash
python3 -m unittest discover -s tests -v
m1n1_windows/tests/run_host_tests.sh
scripts/build-standalone.sh --display physical --debug monitor
```

Expected: every suite passes; parsing the outer image yields an inner image whose autonomous manifest has flags `0x11` and whose decoded firmware has the unchanged Mu SHA.

- [ ] **Step 3: Preserve immutable local evidence**

Copy the final outer image, decoded inner image, and exact ELF into `.local/stage0-control/`; record root/submodule commits, compiler/linker versions, sizes, CRC values, and SHA-256 values in `.local/stage0-control/BUILD.txt`.

- [ ] **Step 4: Transfer and install reversibly**

Transfer the outer image to the Air, verify SHA remotely, and run locally on the Air:

```bash
sudo ./scripts/install-esp.sh install --disk disk0s4 --image ~/boot-stage0-control.bin
```

The installer must be extended only enough to validate the outer and nested inner manifests before atomic replacement; restore behavior remains unchanged.

- [ ] **Step 5: Record a cold boot and require both-stage evidence**

Start `scripts/log-standalone.sh` before boot. The trace must contain:

```text
BOOTSTRAP_VALIDATE
BOOTSTRAP_DECOMPRESS
BOOTSTRAP_VERIFY
BOOTSTRAP_CHAINLOAD
Preparing to run next stage
Vectoring to next stage
Initialization complete.
Standalone: image valid
```

A USB generation transition between Stage 0 and Stage 1 is expected. Lack of a Stage 1 line is a Stage 0 failure, not a Windows failure.

- [ ] **Step 6: Evaluate the CPU1 checkpoint**

Require `HV: Entering guest secondary 1` and `HV: Secondary 1 consumed entry`. If the same pre-entry exception remains, preserve the exact Stage 1 ELF and exception state and stop before the shared guest-engine refactor; Stage 0 did not eliminate the remaining startup state. If CPU1 passes, continue observing CPUs 2 through 7 without declaring stable standalone complete.

- [ ] **Step 7: Record evidence, run checks, and commit integration**

Update `documentation/DEVELOPMENT_HISTORY.md` with immutable hashes and exact trace lines. Run:

```bash
python3 -m unittest discover -s tests -v
m1n1_windows/tests/run_host_tests.sh
git diff --check
```

Commit root build/test/installer/docs changes and the final submodule pointer:

```bash
git add scripts/build-standalone.sh scripts/install-esp.sh tests/test_build_standalone.py tests/test_install_esp.py documentation/DEVELOPMENT_HISTORY.md m1n1_windows
git commit -m "feat: validate standalone self-chainload"
```

Do not push until the hardware evidence and commit contents have been reviewed.
