"""EXP835: R105 diagnostic requests must reopen with the description stored at Create."""

from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "drivers/apple-agx/render-admission"
SHARED = ROOT / "drivers/apple-agx/shared"
SOURCE = DRIVER / "src/allocation_windows.c"


def function_body(source, name):
    match = re.search(r"static\s+\w+\s+" + name + r"\s*\([^;]*?\)\s*\{", source, re.S)
    if match is None:
        raise AssertionError(f"missing production function {name}")
    depth, cursor = 1, match.end()
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[match.start():cursor]


class R105OpenNormalization(unittest.TestCase):
    def test_open_uses_the_create_normalization(self):
        """EXP835: Open validated raw R105 bytes and failed every matrix row."""
        source = SOURCE.read_text()
        open_body = function_body(source, "AdmissionOpenAllocationImpl")
        self.assertIn("AdmissionR105ParsePrivate", open_body)
        self.assertIn("AdmissionR105ParsePrivate",
                      function_body(source, "AdmissionCreateAllocationImpl"))

    def test_r105_request_parses_to_the_ordinary_description(self):
        helper = function_body(SOURCE.read_text(), "AdmissionR105ParsePrivate")
        program = r'''
#include "render_allocation_probe.h"
#include "render_allocation.h"
#include <assert.h>
#include <string.h>
typedef int BOOLEAN;
typedef void VOID;
typedef unsigned int UINT;
#define TRUE 1
#define FALSE 0
''' + helper + r'''
static ADMISSION_WIN32_ALLOCATION_CREATE make(unsigned class_id) {
  ADMISSION_WIN32_ALLOCATION_CREATE create;
  memset(&create, 0, sizeof(create));
  create.Magic = ADMISSION_WIN32_ALLOCATION_MAGIC;
  create.Version = ADMISSION_WIN32_ALLOCATION_VERSION;
  create.Bytes = sizeof(create);
  create.ClassId = class_id;
  create.Flags = class_id ? (AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead) : 0u;
  assert(AdmissionAllocationDescribe(0x10000u, 1u, 1u,
      ADMISSION_WIN32_ALLOCATION_STAGING_CPUVISIBLE,
      ADMISSION_WIN32_ALLOCATION_FORMAT_A8, 1u, &create.Allocation));
  return create;
}
int main(void) {
  for (unsigned class_id = 0; class_id <= 1u; ++class_id) {
    ADMISSION_WIN32_ALLOCATION_CREATE plain = make(class_id ? AgxWin32BufferClassShader : 0u);
    ADMISSION_WIN32_ALLOCATION_CREATE probe = plain;
    ADMISSION_ALLOCATION_DESCRIPTION expected, actual;
    APPLE_AGX_U32 expected_class = 0, expected_flags = 0, actual_class = 9, actual_flags = 9;
    ADMISSION_R105_OVERRIDE override;
    ADMISSION_WIN32_TRANSPORT_RESULT result = (ADMISSION_WIN32_TRANSPORT_RESULT)-1;
    if (class_id == 0u)
      assert(AdmissionWin32AllocationCreateValidate(&plain.Allocation, sizeof(plain.Allocation),
          &expected, &expected_class, &expected_flags) == AdmissionWin32TransportSuccess);
    else
      assert(AdmissionWin32AllocationCreateValidate(&plain, sizeof(plain),
          &expected, &expected_class, &expected_flags) == AdmissionWin32TransportSuccess);
    probe.Reserved[0] = ADMISSION_R105_MAGIC;
    probe.Reserved[1] = (7u << 8) | 2u;
    /* The pre-EXP835 Open path validated these raw bytes. */
    assert(AdmissionWin32AllocationCreateValidate(&probe, sizeof(probe),
        &actual, &actual_class, &actual_flags) != AdmissionWin32TransportSuccess);
    assert(AdmissionR105ParsePrivate(&probe, sizeof(probe), &override,
        &actual, &actual_class, &actual_flags, &result));
    assert(result == AdmissionWin32TransportSuccess);
    assert(memcmp(&expected, &actual, sizeof(expected)) == 0);
    assert(actual_class == expected_class && actual_flags == expected_flags);
    assert(override.Token == 7u && override.ReadMode == 2u);
    assert(!AdmissionR105ParsePrivate(&plain, sizeof(plain), &override,
        &actual, &actual_class, &actual_flags, &result));
  }
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "r105_open.c"
            binary = Path(directory) / "r105_open"
            source.write_text(program)
            build = subprocess.run(
                [os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(DRIVER / "include"), "-I", str(SHARED / "include"), str(source),
                 str(DRIVER / "src/render_win32_transport.c"),
                 str(DRIVER / "src/render_allocation.c"),
                 str(SHARED / "src/apple_agx_win32_abi.c"), "-o", str(binary)],
                capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
