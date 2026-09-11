# J313 AGX G2 Windows UAT and Initdata Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build sanitizer-tested J313 UAT and G13/V13_5 initdata codecs plus an opt-in, inert Windows SGX mapping transport without changing the stable driver path or touching hardware.

**Architecture:** Freestanding C modules own binary encoding, page-table construction and mapping-state coordination. The generated J313 contract supplies all geometry and version values. A WDK-only adapter wraps `DxgkCbMapMemory`/`DxgkCbUnmapMemory` behind a new qualification flag, maps SGX once, derives ASC as a checked subview, performs no register access and remains unreachable in the default package.

**Tech Stack:** C11, Clang ASan/UBSan host tests, Python `unittest`/`pytest`, generated C headers, WDM/WDDM ARM64, MSBuild and GitHub Actions.

**Spec:** `documentation/design/2026-08-27-j313-agx-g2-windows-uat-initdata.md`

## Global Constraints

- Work only in `/Users/pavel/public_windows` on `feature/j313-gpu-acceleration`; do not modify `/Users/pavel/windows` or create a worktree.
- Preserve the accepted eight-core, AppleInput, NVMe, xHCI and physical-display baseline; this plan performs no hardware run and installs no package.
- Pin behavior to m1n1 `bee53dc60bd160c0a64de758974af767c2970baf`, Asahi `77cb8f24c2381a8abb7272d7bbdec548d6426a8a`, J313, G13 and V13_5.
- UAT uses two canonical 39-bit TTBR halves, 40 output bits, 16-KiB pages, shifts `36/25/14`, entry counts `8/2048/2048` and 64 contexts.
- Context 0 is firmware-only, context 63 is qualification-only and contexts 1-62 are future render contexts.
- The Windows transport maps the translated SGX resource exactly once; ASC is a contained subview and is never mapped separately.
- The default WDK package cannot reach the new mapping transport. The qualification transport performs no MMIO read or write.
- Every production function begins with a failing test, every feature/correction commit is indexed in `investigation/CHANGES.csv`, and no commit includes AI attribution.
- Any implementation ambiguity stops at the pinned source evidence; do not invent structure fields, register offsets, flags or addresses.

---

### Task 1: Generate the explicit J313 UAT and initdata envelope contract

**Files:**
- Modify: `tools/generate_j313_agx_g2_contract.py`
- Modify: `tests/test_j313_agx_g2_contract.py`
- Modify: `drivers/apple-agx/shared/include/j313_agx_g2.generated.h`

**Interfaces:**
- Consumes: the existing validated G1 values `page_size=0x4000`, `address_bits=40`, `num_contexts=64`, `firmware_generation=G13`, `firmware_version=V13_5`.
- Produces: generated macros `J313_AGX_G2_UAT_INPUT_ADDRESS_BITS`, `J313_AGX_G2_UAT_OUTPUT_ADDRESS_BITS`, `J313_AGX_G2_UAT_PAGE_BITS`, `J313_AGX_G2_UAT_LEVEL_COUNT`, per-level shifts/counts, context ownership limits, `J313_AGX_G2_INITDATA_SIZE` and four V13_5 version words.

- [x] **Step 1: Write the failing contract test**

Add assertions to `test_generated_header_is_checked_in_and_deterministic`:

```python
for line in (
    "#define J313_AGX_G2_UAT_INPUT_ADDRESS_BITS 39u",
    "#define J313_AGX_G2_UAT_OUTPUT_ADDRESS_BITS 40u",
    "#define J313_AGX_G2_UAT_PAGE_BITS 14u",
    "#define J313_AGX_G2_UAT_LEVEL_COUNT 3u",
    "#define J313_AGX_G2_UAT_LEVEL0_SHIFT 36u",
    "#define J313_AGX_G2_UAT_LEVEL0_ENTRIES 8u",
    "#define J313_AGX_G2_UAT_LEVEL1_SHIFT 25u",
    "#define J313_AGX_G2_UAT_LEVEL1_ENTRIES 2048u",
    "#define J313_AGX_G2_UAT_LEVEL2_SHIFT 14u",
    "#define J313_AGX_G2_UAT_LEVEL2_ENTRIES 2048u",
    "#define J313_AGX_G2_UAT_CONTEXT_COUNT 64u",
    "#define J313_AGX_G2_UAT_FIRMWARE_CONTEXT 0u",
    "#define J313_AGX_G2_UAT_RENDER_CONTEXT_MIN 1u",
    "#define J313_AGX_G2_UAT_RENDER_CONTEXT_MAX 62u",
    "#define J313_AGX_G2_UAT_QUALIFICATION_CONTEXT 63u",
    "#define J313_AGX_G2_INITDATA_SIZE 0xbcu",
    "#define J313_AGX_G2_INITDATA_VERSION_WORD0 0x6ba0u",
    "#define J313_AGX_G2_INITDATA_VERSION_WORD1 0x1f28u",
    "#define J313_AGX_G2_INITDATA_VERSION_WORD2 0x601u",
    "#define J313_AGX_G2_INITDATA_VERSION_WORD3 0xb0u",
):
    self.assertIn(line, rendered)
```

- [x] **Step 2: Run RED**

Run:

```bash
python3 -m unittest tests.test_j313_agx_g2_contract.J313AgxG2ContractTests.test_generated_header_is_checked_in_and_deterministic -v
```

Expected: FAIL because the first UAT macro is absent.

- [x] **Step 3: Add reviewed generator constants and render them only into the Windows header**

Define immutable generator tuples:

```python
UAT_INPUT_ADDRESS_BITS = 39
UAT_PAGE_BITS = 14
UAT_LEVELS = ((36, 8), (25, 2048), (14, 2048))
UAT_FIRMWARE_CONTEXT = 0
UAT_RENDER_CONTEXTS = (1, 62)
UAT_QUALIFICATION_CONTEXT = 63
INITDATA_SIZE = 0xBC
INITDATA_VERSION_WORDS = (0x6BA0, 0x1F28, 0x0601, 0x00B0)
```

Validate that `1 << UAT_PAGE_BITS == contract.page_size`, output bits equal
`contract.address_bits`, the context classes exactly cover `0..63`, and each
level count is a power of two. Render the macros after the existing page and
address macros. Do not change ASL or the m1n1 policy header.

- [x] **Step 4: Regenerate and verify GREEN**

Run:

```bash
python3 tools/generate_j313_agx_g2_contract.py
python3 tools/generate_j313_agx_g2_contract.py --check
python3 -m unittest tests.test_j313_agx_g2_contract -v
git diff --check
```

Expected: generator check succeeds and the complete contract suite passes.

- [x] **Step 5: Commit the generated contract**

```bash
git add tools/generate_j313_agx_g2_contract.py tests/test_j313_agx_g2_contract.py drivers/apple-agx/shared/include/j313_agx_g2.generated.h
git commit -m "gpu: generate J313 UAT and initdata geometry"
```

---

### Task 2: Add the pure UAT descriptor codec

**Files:**
- Create: `drivers/apple-agx/shared/include/apple_agx_uat.h`
- Create: `drivers/apple-agx/shared/src/apple_agx_uat.c`
- Create: `drivers/apple-agx/shared/tests/apple_agx_uat_test.c`
- Create: `tests/test_apple_agx_uat.py`

**Interfaces:**
- Consumes: Task 1 generated UAT geometry macros.
- Produces:

```c
typedef enum _APPLE_AGX_UAT_RESULT {
  AppleAgxUatResultOk = 0,
  AppleAgxUatResultInvalidArgument,
  AppleAgxUatResultUnsupportedContext,
  AppleAgxUatResultMisaligned,
  AppleAgxUatResultOutOfRange,
  AppleAgxUatResultOverflow,
  AppleAgxUatResultUnsupportedProtection,
  AppleAgxUatResultAlreadyMapped,
  AppleAgxUatResultCapacity,
  AppleAgxUatResultAllocationFailed,
} APPLE_AGX_UAT_RESULT;

typedef enum _APPLE_AGX_UAT_PROTECTION {
  AppleAgxUatFirmwareDeviceReadWrite = 1,
  AppleAgxUatFirmwareSharedReadWrite,
  AppleAgxUatFirmwarePrivateReadWrite,
  AppleAgxUatFirmwareGpuPrivateReadWrite,
  AppleAgxUatFirmwareReadWriteGpuReadOnly,
  AppleAgxUatGpuSharedReadOnly,
  AppleAgxUatGpuSharedWriteOnly,
  AppleAgxUatGpuSharedReadWrite,
} APPLE_AGX_UAT_PROTECTION;

typedef enum _APPLE_AGX_UAT_HALF {
  AppleAgxUatTtbr0 = 0,
  AppleAgxUatTtbr1 = 1,
} APPLE_AGX_UAT_HALF;

APPLE_AGX_UAT_RESULT AppleAgxUatValidateRange(
    unsigned int Context, unsigned long long VirtualAddress,
    unsigned long long PhysicalAddress, unsigned long long Length,
    APPLE_AGX_UAT_PROTECTION Protection, APPLE_AGX_UAT_HALF *Half);
APPLE_AGX_UAT_RESULT AppleAgxUatEncodeTableDescriptor(
    unsigned long long PhysicalAddress, unsigned long long *Descriptor);
APPLE_AGX_UAT_RESULT AppleAgxUatEncodePageDescriptor(
    unsigned int Context, unsigned long long PhysicalAddress,
    APPLE_AGX_UAT_PROTECTION Protection,
    unsigned long long *Descriptor);
```

- [x] **Step 1: Write a host test that names the exact descriptor bits**

The first test must assert:

```c
unsigned long long descriptor = 0;
assert(AppleAgxUatEncodeTableDescriptor(0x12340000ULL, &descriptor) ==
       AppleAgxUatResultOk);
assert(descriptor == 0x12340003ULL);

assert(AppleAgxUatEncodePageDescriptor(
           0, 0x23400000ULL, AppleAgxUatFirmwareDeviceReadWrite,
           &descriptor) == AppleAgxUatResultOk);
assert(descriptor == (0x23400000ULL | (1ULL << 55) | (1ULL << 54) |
                      (1ULL << 10) | (1ULL << 6) | (1ULL << 2) | 3ULL));
```

Add separate assertions for all eight semantic protections, context 63 adding
the non-global bit, the first/last pages of both canonical TTBR halves,
rejection of the non-canonical middle and cross-half ranges, page/table
misalignment, null outputs, 40-bit overflow and an invalid enum value.

- [x] **Step 2: Add the Python sanitizer harness and run RED**

`tests/test_apple_agx_uat.py` compiles the C test with:

```python
command = [
    os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
    "-Werror", "-fsanitize=address,undefined",
    "-I", str(SHARED / "include"),
    str(SHARED / "tests" / "apple_agx_uat_test.c"),
    str(SHARED / "src" / "apple_agx_uat.c"), "-o", str(binary),
]
```

Run:

```bash
python3 -m unittest tests.test_apple_agx_uat -v
```

Expected: compiler failure because the new header/source are absent.

- [x] **Step 3: Implement the minimal freestanding codec**

Use local unsigned integer typedefs, the generated header and named masks for
PTE owner bit 55, UXN 54, PXN 53, non-global 11, access flag 10, AP bits 7:6,
attribute bits 4:2 and descriptor bits 1:0. The header must not include
`stdbool.h`, `stddef.h` or `stdint.h`.

`AppleAgxUatValidateRange` must validate the complete half-open ranges before
calling either encoder. It accepts low addresses below `0x0000008000000000`
and high addresses at or above `0xffffff8000000000`, rejects the middle and
returns which TTBR half owns the range. Context 0 accepts only
firmware-capable protections; contexts 1-62 and 63 reject firmware-only
protections. No public protection permits execution.

- [x] **Step 4: Run GREEN and mutation checks**

Run:

```bash
python3 -m unittest tests.test_apple_agx_uat -v
python3 -m unittest tests.test_apple_agx_firmware tests.test_j313_agx_g2_contract -v
git diff --check
```

Then temporarily flip the access-flag bit in `apple_agx_uat.c`, verify the
golden test fails, and restore the correct source. Expected final result: all
focused tests pass under ASan/UBSan.

- [x] **Step 5: Commit the UAT codec**

```bash
git add drivers/apple-agx/shared/include/apple_agx_uat.h drivers/apple-agx/shared/src/apple_agx_uat.c drivers/apple-agx/shared/tests/apple_agx_uat_test.c tests/test_apple_agx_uat.py
git commit -m "gpu: add J313 UAT descriptor codec"
```

---

### Task 3: Add deterministic UAT table construction and reverse inventory

**Files:**
- Create: `drivers/apple-agx/shared/include/apple_agx_uat_table.h`
- Create: `drivers/apple-agx/shared/src/apple_agx_uat_table.c`
- Create: `drivers/apple-agx/shared/tests/apple_agx_uat_table_test.c`
- Create: `tests/test_apple_agx_uat_table.py`

**Interfaces:**
- Consumes: `AppleAgxUatValidateRange`, `AppleAgxUatEncodeTableDescriptor`, `AppleAgxUatEncodePageDescriptor` from Task 2.
- Produces:

```c
typedef struct _APPLE_AGX_UAT_PAGE {
  unsigned long long PhysicalAddress;
  unsigned long long *Entries;
  unsigned int Level;
} APPLE_AGX_UAT_PAGE;

typedef struct _APPLE_AGX_UAT_MAPPING {
  unsigned int Context;
  unsigned long long VirtualAddress;
  unsigned long long PhysicalAddress;
  unsigned long long Length;
  APPLE_AGX_UAT_PROTECTION Protection;
} APPLE_AGX_UAT_MAPPING;

typedef struct _APPLE_AGX_UAT_INVENTORY {
  APPLE_AGX_UAT_PAGE *Pages;
  unsigned int PageCapacity;
  unsigned int PageCount;
  APPLE_AGX_UAT_MAPPING *Mappings;
  unsigned int MappingCapacity;
  unsigned int MappingCount;
} APPLE_AGX_UAT_INVENTORY;

typedef struct _APPLE_AGX_UAT_ALLOCATOR {
  void *Context;
  unsigned char (*AllocatePage)(void *Context, APPLE_AGX_UAT_PAGE *Page);
  void (*ReleasePage)(void *Context, const APPLE_AGX_UAT_PAGE *Page);
} APPLE_AGX_UAT_ALLOCATOR;

typedef struct _APPLE_AGX_UAT_ROOTS {
  unsigned long long Ttbr0PhysicalAddress;
  unsigned long long Ttbr1PhysicalAddress;
} APPLE_AGX_UAT_ROOTS;

APPLE_AGX_UAT_RESULT AppleAgxUatCreateAddressSpace(
    unsigned int Context, const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory,
    APPLE_AGX_UAT_ROOTS *Roots);
APPLE_AGX_UAT_RESULT AppleAgxUatMap(
    unsigned int Context, const APPLE_AGX_UAT_ROOTS *Roots,
    unsigned long long VirtualAddress, unsigned long long PhysicalAddress,
    unsigned long long Length, APPLE_AGX_UAT_PROTECTION Protection,
    const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory);
void AppleAgxUatDestroy(const APPLE_AGX_UAT_ALLOCATOR *Allocator,
                        APPLE_AGX_UAT_INVENTORY *Inventory);
```

- [x] **Step 1: Write table-walk and rollback tests**

Use a fake allocator that returns zeroed, 16-KiB-aligned arrays and monotonically
increasing 40-bit physical addresses. Assert:

```c
assert(AppleAgxUatCreateAddressSpace(0, &allocator, &inventory, &roots) ==
       AppleAgxUatResultOk);
assert(AppleAgxUatMap(0, &roots, 0xffffff8000010000ULL, 0x20000000ULL,
                     0x8000ULL,
                     AppleAgxUatFirmwarePrivateReadWrite, &allocator,
                     &inventory) == AppleAgxUatResultOk);
assert(inventory.MappingCount == 1u);
assert(inventory.Mappings[0].Length == 0x8000ULL);
```

Assert that creation allocates an explicit TTBR0/TTBR1 pair. Inspect the exact
L0/L1/L2 indices derived from shifts `36/25/14` in TTBR1, assert two leaf
descriptors, and add one low mapping proving TTBR0 selection. Reject the same
virtual page a second time, overlap, a cross-half mapping and insufficient
inventory capacity. Inject allocator failure on every allocation call. For
each failure, assert that only completed pages are released in strict reverse
order. Call destroy twice and assert no second release.

- [x] **Step 2: Add the sanitizer harness and run RED**

Compile `apple_agx_uat_table_test.c`, `apple_agx_uat_table.c` and
`apple_agx_uat.c` with the same C11 ASan/UBSan flags as Task 2.

```bash
python3 -m unittest tests.test_apple_agx_uat_table -v
```

Expected: compiler failure because `apple_agx_uat_table.h` is absent.

- [x] **Step 3: Implement allocation-free caller ownership**

The module must never allocate its inventory arrays. It appends only after
capacity checks, accepts only allocator pages whose CPU pointer and physical
address are nonzero/aligned, locates pages only through the inventory, and
publishes a parent descriptor only after the child page is valid and recorded.
On any mapping failure, restore the page and mapping counts to their entry
values and release pages added by that call in reverse order.

- [x] **Step 4: Run GREEN and the combined UAT suite**

```bash
python3 -m unittest tests.test_apple_agx_uat tests.test_apple_agx_uat_table -v
git diff --check
```

Expected: both sanitizer binaries pass with no warning or sanitizer report.

- [x] **Step 5: Commit the table owner**

```bash
git add drivers/apple-agx/shared/include/apple_agx_uat_table.h drivers/apple-agx/shared/src/apple_agx_uat_table.c drivers/apple-agx/shared/tests/apple_agx_uat_table_test.c tests/test_apple_agx_uat_table.py
git commit -m "gpu: build firmware UAT with reverse inventory"
```

---

### Task 4: Add the byte-exact G13/V13_5 initdata envelope codec

**Files:**
- Create: `drivers/apple-agx/shared/include/apple_agx_initdata.h`
- Create: `drivers/apple-agx/shared/src/apple_agx_initdata.c`
- Create: `drivers/apple-agx/shared/tests/apple_agx_initdata_test.c`
- Create: `tests/test_apple_agx_initdata.py`

**Interfaces:**
- Consumes: Task 1 generated initdata/UAT constants and Task 2 range validation rules.
- Produces:

```c
typedef struct _APPLE_AGX_INITDATA_INPUT {
  unsigned long long TaggedBufferAddress;
  unsigned long long RuntimePointersAddress;
  unsigned long long GlobalsAddress;
  unsigned long long FirmwareStatusAddress;
} APPLE_AGX_INITDATA_INPUT;

typedef struct _APPLE_AGX_INITDATA_MANIFEST {
  unsigned int EncodedSize;
  unsigned short VersionWords[4];
  unsigned long long ReferencedAddresses[4];
} APPLE_AGX_INITDATA_MANIFEST;

typedef enum _APPLE_AGX_INITDATA_RESULT {
  AppleAgxInitdataResultOk = 0,
  AppleAgxInitdataResultInvalidArgument,
  AppleAgxInitdataResultUnsupportedVersion,
  AppleAgxInitdataResultDestinationSize,
  AppleAgxInitdataResultDestinationNotZero,
  AppleAgxInitdataResultAddress,
  AppleAgxInitdataResultOverlap,
} APPLE_AGX_INITDATA_RESULT;

APPLE_AGX_INITDATA_RESULT AppleAgxInitdataEncodeG13V13_5(
    const APPLE_AGX_INITDATA_INPUT *Input, unsigned char *Destination,
    unsigned int DestinationSize, APPLE_AGX_INITDATA_MANIFEST *Manifest);
```

- [x] **Step 1: Write the golden 0xBC-byte test**

Use four distinct aligned firmware-private addresses. Assert bytes and fields
at exact offsets:

```c
assert(output[0x00] == 0xa0 && output[0x01] == 0x6b);
assert(output[0x02] == 0x28 && output[0x03] == 0x1f);
assert(output[0x04] == 0x01 && output[0x05] == 0x06);
assert(output[0x06] == 0xb0 && output[0x07] == 0x00);
assert(read_u64(output + 0x08) == input.TaggedBufferAddress);
assert(read_u64(output + 0x18) == input.RuntimePointersAddress);
assert(read_u64(output + 0x20) == input.GlobalsAddress);
assert(read_u64(output + 0x28) == input.FirmwareStatusAddress);
assert(read_u16(output + 0x30) == 0x4000);
assert(output[0x32] == 14 && output[0x33] == 3);
assert(read_u32(output + 0xa8) == 1);
```

For level records at `0x34`, `0x54`, `0x74`, assert the byte fields
`8/14/14/shift`, entry count, `0x4000`, constant one, physical mask
`0x000000ffffffc000ULL`, and the derived index masks. Assert the final size is
`0xBC` and all unspecified bytes remain zero.

Add tests for null arguments, wrong size, a pre-dirtied destination, unaligned
addresses, low-half or non-canonical object addresses,
duplicate/overlapping referenced 16-KiB objects and output preservation after
every failure. Valid objects may occupy any aligned address in the
context-zero high canonical half; they are not limited to `rtkit_private`.

- [x] **Step 2: Add sanitizer harness and run RED**

```bash
python3 -m unittest tests.test_apple_agx_initdata -v
```

Expected: compiler failure because the initdata header/source are absent.

- [x] **Step 3: Implement explicit little-endian writers**

Use `write_u16`, `write_u32` and `write_u64` helpers; do not cast the output to
a packed C structure. Validate every input and scan the full destination for
zero before writing the first byte. Build into a local `unsigned char
encoded[J313_AGX_G2_INITDATA_SIZE]`, then copy it to the caller only after all
validation succeeds. Populate the manifest from the validated input.

- [x] **Step 4: Run GREEN and mutation proof**

```bash
python3 -m unittest tests.test_apple_agx_initdata tests.test_apple_agx_uat -v
git diff --check
```

Temporarily change one level shift and verify the golden test fails, then
restore it. Expected final result: all tests pass under ASan/UBSan.

- [x] **Step 5: Commit the initdata codec**

```bash
git add drivers/apple-agx/shared/include/apple_agx_initdata.h drivers/apple-agx/shared/src/apple_agx_initdata.c drivers/apple-agx/shared/tests/apple_agx_initdata_test.c tests/test_apple_agx_initdata.py
git commit -m "gpu: encode G13 V13_5 initdata envelope"
```

---

### Task 5: Add a pure SGX mapping-state coordinator

**Files:**
- Create: `drivers/apple-agx/shared/include/apple_agx_mapping.h`
- Create: `drivers/apple-agx/shared/src/apple_agx_mapping.c`
- Create: `drivers/apple-agx/shared/tests/apple_agx_mapping_test.c`
- Create: `tests/test_apple_agx_mapping.py`

**Interfaces:**
- Consumes: generated SGX and ASC physical ranges.
- Produces:

```c
typedef struct _APPLE_AGX_MAPPING_STATE {
  unsigned char *SgxBase;
  unsigned char *AscBase;
  unsigned long long SgxPhysicalAddress;
  unsigned int SgxLength;
  unsigned char Active;
} APPLE_AGX_MAPPING_STATE;

typedef struct _APPLE_AGX_MAPPING_IO {
  void *Context;
  unsigned char (*Map)(void *Context, unsigned long long PhysicalAddress,
                       unsigned int Length, unsigned char **VirtualAddress);
  unsigned char (*Unmap)(void *Context, unsigned char *VirtualAddress);
} APPLE_AGX_MAPPING_IO;

APPLE_AGX_UAT_RESULT AppleAgxMappingStart(
    const APPLE_AGX_MAPPING_IO *Io, APPLE_AGX_MAPPING_STATE *State);
APPLE_AGX_UAT_RESULT AppleAgxMappingStop(
    const APPLE_AGX_MAPPING_IO *Io, APPLE_AGX_MAPPING_STATE *State);
```

- [x] **Step 1: Write map-once/subview/rollback tests**

The fake mapper records every request. Assert one map at
`0x204000000/0x04000000`, `AscBase == SgxBase + 0x02400000`, no second map,
and one unmap of the original SGX base. Test null callbacks, map failure,
returned null base, corrupted generated containment through a test-only helper,
unmap failure preserving active ownership, successful retry and idempotent
second stop.

- [x] **Step 2: Add sanitizer harness and run RED**

Compile the mapping test with `apple_agx_mapping.c` and
`apple_agx_uat.c`, then run:

```bash
python3 -m unittest tests.test_apple_agx_mapping -v
```

Expected: compiler failure because the mapping module is absent.

- [x] **Step 3: Implement the pure coordinator**

Before mapping, prove with checked subtraction/addition that ASC begins at or
after SGX and ends no later than SGX. Map SGX once, reject a null returned
pointer, derive ASC only from the validated offset, and set `Active` last. On
successful unmap, zero the complete state. On unmap failure, retain state so a
later teardown can retry without losing ownership.

- [x] **Step 4: Run GREEN**

```bash
python3 -m unittest tests.test_apple_agx_mapping tests.test_apple_agx_uat -v
git diff --check
```

Expected: both sanitizer suites pass.

- [x] **Step 5: Commit the mapping coordinator**

```bash
git add drivers/apple-agx/shared/include/apple_agx_mapping.h drivers/apple-agx/shared/src/apple_agx_mapping.c drivers/apple-agx/shared/tests/apple_agx_mapping_test.c tests/test_apple_agx_mapping.py
git commit -m "gpu: coordinate inert SGX mapping lifecycle"
```

---

### Task 6: Compile an opt-in inert WDDM mapping transport

**Files:**
- Create: `drivers/apple-agx/windows/src/mmio.c`
- Modify: `drivers/apple-agx/windows/include/apple_agx_driver.h`
- Modify: `drivers/apple-agx/windows/src/adapter.c`
- Modify: `drivers/apple-agx/windows/AppleAgx.vcxproj`
- Modify: `drivers/apple-agx/windows/scripts/build-driver.ps1`
- Modify: `tests/test_apple_agx_windows_package.py`

**Interfaces:**
- Consumes: `APPLE_AGX_MAPPING_STATE`, `APPLE_AGX_MAPPING_IO` and generated SGX/ASC contract.
- Produces:

```c
NTSTATUS AppleAgxQualifyMmioMapping(
    _In_ PDXGKRNL_INTERFACE DxgkInterface,
    _Out_ APPLE_AGX_MAPPING_STATE *MappingState);
NTSTATUS AppleAgxReleaseMmioMapping(
    _In_ PDXGKRNL_INTERFACE DxgkInterface,
    _Inout_ APPLE_AGX_MAPPING_STATE *MappingState);
```

and MSBuild property `AppleAgxMmioQualification=false` defining
`APPLE_AGX_G2_MMIO_QUALIFICATION=1` only when true.

- [x] **Step 1: Write package tests for the unreachable default path**

Add assertions that:

```python
self.assertIn("AppleAgxMmioQualification", project)
self.assertIn("APPLE_AGX_G2_MMIO_QUALIFICATION=1", project)
self.assertIn(r"src\mmio.c", project)
self.assertIn(r"..\shared\src\apple_agx_mapping.c", project)
self.assertIn("#ifdef APPLE_AGX_G2_MMIO_QUALIFICATION", adapter)
self.assertIn("AppleAgxQualifyMmioMapping", adapter)
self.assertNotIn("READ_REGISTER", self.read("src/mmio.c"))
self.assertNotIn("WRITE_REGISTER", self.read("src/mmio.c"))
```

Also require the default build script argument to render
`AppleAgxMmioQualification=false`, and retain zero present sources, zero
children and final `STATUS_NOT_SUPPORTED`.

- [x] **Step 2: Run RED**

```bash
python3 -m unittest tests.test_apple_agx_windows_package -v
```

Expected: FAIL because `src/mmio.c` and the new property do not exist.

- [x] **Step 3: Implement the dxgkrnl adapter**

`mmio.c` adapts `DxgkCbMapMemory` with `InIoSpace=FALSE`,
`MapToUserMode=FALSE`, `MmNonCached` and exact SGX length. It adapts
`DxgkCbUnmapMemory` without reading or writing the returned memory.

Under `APPLE_AGX_G2_MMIO_QUALIFICATION`, `StartDevice` calls the mapping
qualification only after existing resource/state validation and before the
final fail-closed return. It immediately releases the mapping. Any map or
unmap failure reinitializes adapter state and returns the failure. Stop,
remove and reset call release only when the qualification flag is enabled.

Add `APPLE_AGX_MAPPING_STATE MappingState` and a saved
`PDXGKRNL_INTERFACE` only under the qualification guard. The default compiled
DDI flow must contain no call to either mapping function.

- [x] **Step 4: Run GREEN and source audits**

```bash
python3 -m unittest tests.test_apple_agx_windows_package tests.test_apple_agx_mapping -v
rg -n "READ_REGISTER|WRITE_REGISTER" drivers/apple-agx/windows/src/mmio.c
git diff --check
```

Expected: tests pass; the register audit returns no matches.

- [x] **Step 5: Commit the WDDM wrapper**

```bash
git add drivers/apple-agx/windows/src/mmio.c drivers/apple-agx/windows/include/apple_agx_driver.h drivers/apple-agx/windows/src/adapter.c drivers/apple-agx/windows/AppleAgx.vcxproj drivers/apple-agx/windows/scripts/build-driver.ps1 tests/test_apple_agx_windows_package.py
git commit -m "gpu: compile opt-in inert SGX mapping transport"
```

---

### Task 7: Add ARM64 CI coverage and close the offline milestone

**Files:**
- Modify: `.github/workflows/apple-agx-wdk.yml`
- Modify: `tests/test_apple_agx_windows_package.py`
- Modify: `investigation/CHANGES.csv`
- Modify: `documentation/plans/2026-08-27-j313-agx-g2-windows-uat-initdata.md`

**Interfaces:**
- Consumes: all Tasks 1-6 and their commit hashes.
- Produces: three WDK jobs (`default`, `power-qualification`,
  `mmio-qualification`), complete ledger rows and a closed offline checklist.

- [x] **Step 1: Write the failing CI matrix test**

Require these matrix values and MSBuild argument:

```python
self.assertIn("name: mmio-qualification", workflow)
self.assertIn("mmio_qualification: true", workflow)
self.assertIn("artifact: AppleAgx-ARM64-MmioQualification", workflow)
self.assertIn(
    "/p:AppleAgxMmioQualification=${{ matrix.mmio_qualification }}",
    workflow,
)
```

Require default and power rows to set `mmio_qualification: false` explicitly.

- [x] **Step 2: Run RED**

```bash
python3 -m unittest tests.test_apple_agx_windows_package.AppleAgxWindowsPackageTests.test_ci_publishes_separate_default_and_power_qualification_packages -v
```

Expected: FAIL because the mmio matrix row is absent.

- [x] **Step 3: Extend CI without changing signature provenance**

Add matrix fields `qualification` and `mmio_qualification` to all three rows,
pass both MSBuild properties and run the existing signature/provenance step for
both qualification variants using:

```yaml
if: matrix.qualification || matrix.mmio_qualification
```

Retain code analysis and the same WDK packages.

- [x] **Step 4: Run the complete offline gate**

```bash
python3 tools/generate_j313_agx_g2_contract.py --check
python3 -m unittest \
  tests.test_j313_agx_g2_contract \
  tests.test_apple_agx_uat \
  tests.test_apple_agx_uat_table \
  tests.test_apple_agx_initdata \
  tests.test_apple_agx_mapping \
  tests.test_apple_agx_firmware \
  tests.test_apple_agx_state \
  tests.test_apple_agx_power \
  tests.test_apple_agx_windows_package -v
python3 -m pytest -q
git diff --check
git status --short
```

Expected: focused and complete suites pass; only the known untracked submodule
worktree state may remain and no generated file is stale.

- [x] **Step 5: Commit CI, then obtain the WDK run result**

```bash
git add .github/workflows/apple-agx-wdk.yml tests/test_apple_agx_windows_package.py
git commit -m "ci: build inert AGX mapping qualification"
git push origin feature/j313-gpu-acceleration
```

Wait for all three ARM64 jobs. Record the workflow run ID and exact job result.
Do not download, stage or install the package.

- [x] **Step 6: Index every implementation commit**

Append one RFC 4180 row for the design commit `d1166d6` and one row for each
Task 1-7 implementation commit. Each row includes the exact commit, reason,
pre-fix reproduction, implementation, verification, artifact if any, status
and the statement that no hardware changed. Verify:

```bash
python3 -m unittest tests.test_change_ledger tests.test_repository_hygiene -v
git diff --check
```

- [x] **Step 7: Close and commit the milestone**

Mark every completed checkbox, add the final focused/full test totals and WDK
run ID to this plan, then commit and push:

```bash
git add investigation/CHANGES.csv documentation/plans/2026-08-27-j313-agx-g2-windows-uat-initdata.md
git commit -m "docs: close AGX UAT and initdata offline milestone"
git push origin feature/j313-gpu-acceleration
```

The closing note must state that no hardware experiment is authorized and the
next action requires a new preregistered map/subview/unmap experiment with the
existing zero-Event-129 storage gate.

## Closure Receipt

Completed implementation commits:

- Task 1: `edf7f9a539b996c830fe75cd54450e990937c70f`
- Task 2: `016c716d9111439485272ad844c4332a2cd2f750`
- Task 3: `f35448af064cbc83b9002210b5d2decf28eebaab`
- Task 4: `150f290396fe9ab92334f08690fc596c758f0399`
- Task 5: `10a3c6fc871b8c5a76605811ecc62b106fa8735a`
- Task 6: `0d3c87348fcb4ceaa133b3957cc814f15a8bd789`
- Task 7: `32f881911b7c31695e1ce6aaf84cc249178f8f75`

Final verification on 2026-08-27:

- deterministic generated-contract check: passed;
- focused UAT, table, initdata, mapping, firmware, state, power and WDDM
  package gate: 50/50 passed;
- canonical public `tests/` suite through the repository `proxyenv`: 677 tests
  and 175 subtests passed;
- GitHub Actions ARM64 WDK run `33021179478`: `default`,
  `power-qualification` and `mmio-qualification` all succeeded, including code
  analysis, WDK test-signature provenance and package publication;
- no generated file was stale and the final diff check passed.

The unrestricted repository-root pytest command is intentionally not the
public gate because it recursively collects vendored Mu, m1n1 hardware and
archived experiment tests. The reproducible full public command is:

```bash
PYTHONPATH=.:m1n1_windows/proxyclient \
  ./proxyenv/bin/python -m pytest tests -q
```

No CI artifact was downloaded, staged or installed. No Windows, firmware,
guest, MMIO register or hardware state changed during this milestone. This
closure does **not** authorize a hardware experiment. The next action requires
a new preregistered SGX map/subview/unmap experiment and must retain the
existing zero-Event-129 storage gate.
