# Standalone Runtime RAM Bound Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prevent standalone boot from advertising or mapping RAM beyond the normal-memory limit supplied by iBoot.

**Architecture:** Add a pure address-bound resolver to the m1n1 autonomous subsystem and cover it with host tests. Resolve the effective RAM end once during standalone validation, retain it in runtime state, and use that same value for both guest boot arguments and the stage-2 normal-RAM mapping.

**Tech Stack:** Freestanding C11, m1n1 autonomous runtime, POSIX shell host-test runner, J313 standalone image builder.

## Global Constraints

- Treat the manifest `ram_end` as an upper limit.
- Derive the platform end from `cur_boot_args.phys_base + cur_boot_args.mem_size` with overflow detection.
- Never hardcode the observed J313 end address.
- Use one resolved end for both advertised guest RAM and stage-2 mapping.
- Do not change low-memory aliasing, display, NVMe, USB, SMP, or Windows installation behavior.

---

### Task 1: Testable RAM-Bound Resolver

**Files:**
- Create: `m1n1_windows/src/hv_autonomous_memory.h`
- Create: `m1n1_windows/src/hv_autonomous_memory.c`
- Create: `m1n1_windows/tests/hv_autonomous_memory_test.c`
- Modify: `m1n1_windows/tests/run_host_tests.sh`
- Modify: `m1n1_windows/Makefile`

**Interfaces:**
- Consumes: guest physical base, manifest maximum end, iBoot physical base, and iBoot memory size as `u64` values.
- Produces: `bool hv_autonomous_resolve_ram_end(u64 guest_base, u64 configured_end, u64 platform_base, u64 platform_size, u64 *effective_end)`.

- [ ] **Step 1: Write the failing unit test**

Create `tests/hv_autonomous_memory_test.c` with assertions for clamping, preserving a lower configured maximum, overflow rejection, invalid range rejection, and a null output pointer:

```c
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/hv_autonomous_memory.h"

int main(void)
{
    u64 end = 0;

    assert(hv_autonomous_resolve_ram_end(0x850000000ULL, 0xa00000000ULL,
                                         0x800000000ULL, 0x1df708000ULL, &end));
    assert(end == 0x9df708000ULL);

    assert(hv_autonomous_resolve_ram_end(0x850000000ULL, 0x900000000ULL,
                                         0x800000000ULL, 0x1df708000ULL, &end));
    assert(end == 0x900000000ULL);

    assert(!hv_autonomous_resolve_ram_end(0x850000000ULL, UINT64_MAX,
                                          UINT64_MAX - 0xfff, 0x1000, &end));
    assert(!hv_autonomous_resolve_ram_end(0x850000000ULL, 0x850000000ULL,
                                          0x800000000ULL, 0x1df708000ULL, &end));
    assert(!hv_autonomous_resolve_ram_end(0x850000000ULL, 0xa00000000ULL,
                                          0x800000000ULL, 0x1df708000ULL, NULL));

    puts("hv_autonomous_memory_test: ok");
    return 0;
}
```

Add `hv_autonomous_memory_test` to `all_tests` and map it to `src/hv_autonomous_memory.c` in the runner. Add `hv_autonomous_memory.o` beside the other autonomous objects in the firmware Makefile.

- [ ] **Step 2: Run the new test and verify RED**

Run:

```bash
cd m1n1_windows
./tests/run_host_tests.sh hv_autonomous_memory_test
```

Expected: compilation fails because `hv_autonomous_memory.h` and the resolver do not exist yet.

- [ ] **Step 3: Implement the minimal pure resolver**

Declare the interface in `hv_autonomous_memory.h`, including `types.h`. Implement it in `hv_autonomous_memory.c`:

```c
bool hv_autonomous_resolve_ram_end(u64 guest_base, u64 configured_end,
                                   u64 platform_base, u64 platform_size,
                                   u64 *effective_end)
{
    u64 platform_end;
    u64 resolved;

    if (!effective_end || platform_size > UINT64_MAX - platform_base)
        return false;

    platform_end = platform_base + platform_size;
    resolved = configured_end < platform_end ? configured_end : platform_end;
    if (resolved <= guest_base)
        return false;

    *effective_end = resolved;
    return true;
}
```

- [ ] **Step 4: Run focused and complete host tests**

Run:

```bash
cd m1n1_windows
./tests/run_host_tests.sh hv_autonomous_memory_test
./tests/run_host_tests.sh
```

Expected: all selected tests print `ok` and exit zero.

- [ ] **Step 5: Commit the resolver and tests**

```bash
git add src/hv_autonomous_memory.h src/hv_autonomous_memory.c \
  tests/hv_autonomous_memory_test.c tests/run_host_tests.sh Makefile
git commit -m "fix: validate standalone RAM bounds"
```

---

### Task 2: Apply One Bound Throughout the Autonomous Runtime

**Files:**
- Modify: `m1n1_windows/src/hv_autonomous_runtime.c`

**Interfaces:**
- Consumes: `hv_autonomous_resolve_ram_end(...)` from Task 1 and `cur_boot_args` supplied by m1n1 startup.
- Produces: `struct hv_autonomous_runtime.ram_end`, the single validated end used by boot-data and stage-2 stages.

- [ ] **Step 1: Add an exact observed-size assertion**

Extend `hv_autonomous_memory_test.c` with the observed assisted inputs and assert that the returned size is `0x18f708000`:

```c
assert(hv_autonomous_resolve_ram_end(0x850000000ULL, 0xa00000000ULL,
                                     0x800000000ULL, 0x1df708000ULL, &end));
assert(end - 0x850000000ULL == 0x18f708000ULL);
```

- [ ] **Step 2: Run the focused test before runtime integration**

Run `./tests/run_host_tests.sh hv_autonomous_memory_test` from `m1n1_windows`.

Expected: PASS, proving the boundary value independently of hardware code.

- [ ] **Step 3: Store and use the effective end**

In `hv_autonomous_runtime.c`:

- include `hv_autonomous_memory.h`;
- add `u64 ram_end` to `struct hv_autonomous_runtime`;
- during `HV_AUTONOMOUS_STAGE_VALIDATE`, resolve `runtime->ram_end` from the layout limit and `cur_boot_args`;
- print configured, platform, and effective end addresses after successful resolution;
- change `prepare_boot_data()` to compute `guest_args.mem_size` from `runtime->ram_end`;
- change `map_stage2()` to accept the runtime and map only through `runtime->ram_end`;
- pass `runtime` from `HV_AUTONOMOUS_STAGE_STAGE2`.

The validation branch must print a clear standalone RAM-bound error and return `false` when resolution fails.

- [ ] **Step 4: Run regression tests and compile m1n1**

Run:

```bash
cd m1n1_windows
./tests/run_host_tests.sh
make -j4
```

Expected: host tests pass and both `build/m1n1.macho` and `build/m1n1.bin` are produced.

- [ ] **Step 5: Commit runtime integration**

```bash
git add src/hv_autonomous_runtime.c tests/hv_autonomous_memory_test.c
git commit -m "fix: clamp standalone guest RAM to iBoot limit"
```

---

### Task 3: Build and Inspect the Standalone Artifact

**Files:**
- Modify only if required by an actual build failure: root build scripts or generated artifacts already covered by the public workflow.

**Interfaces:**
- Consumes: updated `m1n1_windows` submodule and the existing J313 firmware artifact.
- Produces: a physical-display standalone `dist/j313/boot.bin` ready for reversible installation.

- [ ] **Step 1: Build the physical standalone profile**

Run the repository's existing J313 standalone build command with the physical-display profile and non-debug settings. Do not introduce a new profile or alter the manifest format.

- [ ] **Step 2: Inspect the resulting image**

Use the existing standalone image inspection command to confirm that the manifest remains supported, physical display is enabled, debug/telemetry is disabled, and payload sizes are within layout limits.

- [ ] **Step 3: Record hashes and working-tree state**

Run:

```bash
shasum -a 256 dist/j313/boot.bin dist/j313/m1n1.macho dist/j313/J313_EFI.fd
git status --short --branch
```

Expected: hashes are printed and only intentional source/submodule changes remain.

- [ ] **Step 4: Commit the public root submodule pointer if needed**

```bash
git add m1n1_windows
git commit -m "fix: bound standalone RAM to platform memory"
```

---

### Task 4: Physical Standalone Validation

**Files:**
- No source changes expected.

**Interfaces:**
- Consumes: the newly built `dist/j313/boot.bin` and the Air's reversible ESP installer/backup workflow.
- Produces: evidence that standalone reaches Windows without reset or automatic repair.

- [ ] **Step 1: Stop the assisted baseline cleanly**

Shut down or reboot Windows normally before replacing `boot.bin`; do not terminate a running guest by overwriting its ESP payload.

- [ ] **Step 2: Install the candidate with backup retained**

Copy the exact hashed candidate to the Air and use the existing ESP tool's `install` action. Preserve the known-good `boot.bin.orig` rollback file.

- [ ] **Step 3: Boot without the debug host attached**

Power on the Air normally. Confirm that the internal panel progresses from m1n1/UEFI into Windows rather than alternating between normal boot and automatic repair.

- [ ] **Step 4: Validate Windows stability**

Confirm the desktop appears, all eight logical processors are present, NVMe and USB remain available, and the system remains responsive for at least five minutes.

- [ ] **Step 5: Roll back on failure and preserve evidence**

If the candidate fails, restore `boot.bin.orig` before further code changes and record the exact final visible stage. Do not mix display-profile changes into this RAM-bound experiment.
