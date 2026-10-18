"""EXP1225: the KMD's 64-bit-only answer follows the installed WOW64 UMD.

dxgkrnl reads DXGKQAITYPE_64BITONLYCAPS once at adapter start and leaves an
adapter that answers SupportsOnly64Bit out of every WOW64 process's adapter
enumeration (dxgkrnl.sys: FilteredAdapterFromWow64Process). This KMD answered
1 unconditionally, so 32-bit DXGI saw only WARP although the package installs
AppleAgxRenderAdmissionUmd32.dll as UserModeDriverNameWow. The answer must
describe the installed package:
- a REG_MULTI_SZ or REG_SZ UserModeDriverNameWow whose first string is
  non-empty and terminated inside the value means 32-bit processes are
  supported (SupportsOnly64Bit = 0);
- anything else -- no value, another type, an empty first string, a string
  running past the data, an odd byte count -- keeps the adapter 64-bit-only,
  so a broken registration hides the adapter instead of loading nothing;
- the KMD reads the value from its driver (software) key, where the INF's
  HKR AddReg writes it, and answers the query from that reading.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
ADMISSION = ROOT / "drivers/apple-agx/render-admission"
INCLUDE = ADMISSION / "include"
SOURCE = ADMISSION / "src/render_wow64.c"
LIFECYCLE = ADMISSION / "src/lifecycle.c"
PROJECT = ADMISSION / "AppleAgxRenderAdmission.vcxproj"
INF = ADMISSION / "AppleAgxRenderAdmission.inf"

PROGRAM = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "render_wow64.h"
static unsigned short name[] = {'U','m','d','3','2','.','d','l','l',0,'U',0,0};
int main(void) {
  unsigned short empty[] = {0, 0};
  unsigned short open[] = {'U','m','d'};
  assert(AdmissionWow64UmdNamed(ADMISSION_REG_MULTI_SZ, name, sizeof(name)) == 1);
  assert(AdmissionWow64UmdNamed(ADMISSION_REG_SZ, name, 20u) == 1);
  /* The first string's terminator is the last character in the data. */
  assert(AdmissionWow64UmdNamed(ADMISSION_REG_MULTI_SZ, name, 20u) == 1);
  assert(AdmissionWow64UmdNamed(ADMISSION_REG_MULTI_SZ, name, 18u) == 0);
  assert(AdmissionWow64UmdNamed(ADMISSION_REG_MULTI_SZ, open, sizeof(open)) == 0);
  assert(AdmissionWow64UmdNamed(ADMISSION_REG_MULTI_SZ, empty, sizeof(empty)) == 0);
  assert(AdmissionWow64UmdNamed(ADMISSION_REG_MULTI_SZ, name, 0u) == 0);
  assert(AdmissionWow64UmdNamed(ADMISSION_REG_MULTI_SZ, name, 19u) == 0);
  assert(AdmissionWow64UmdNamed(ADMISSION_REG_MULTI_SZ, NULL, sizeof(name)) == 0);
  assert(AdmissionWow64UmdNamed(3u /* REG_BINARY */, name, sizeof(name)) == 0);
  assert(AdmissionWow64UmdNamed(4u /* REG_DWORD */, name, 4u) == 0);
  assert(AdmissionWow64Only64Bit(1) == 0u);
  assert(AdmissionWow64Only64Bit(0) == 1u);
  puts("PASS");
  return 0;
}
'''


class Wow64SupportTest(unittest.TestCase):
    def test_registration_parser(self):
        with tempfile.TemporaryDirectory() as tmp:
            program = Path(tmp) / "wow64.c"
            binary = Path(tmp) / "wow64"
            program.write_text(PROGRAM)
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                            "-Werror", "-I", str(INCLUDE), str(program), str(SOURCE),
                            "-o", str(binary)], check=True)
            self.assertEqual(subprocess.run([str(binary)], check=True, capture_output=True,
                                            text=True).stdout.strip(), "PASS")

    def test_query_answers_from_the_driver_key(self):
        lifecycle = LIFECYCLE.read_text()
        case = lifecycle.split("case DXGKQAITYPE_64BITONLYCAPS:", 1)[1].split("break;", 1)[0]
        self.assertIn("AdmissionWow64Only64Bit(", case)
        self.assertNotIn("SupportsOnly64Bit = 1", lifecycle)
        reader = lifecycle.split("AdmissionReadWow64Umd(", 1)[1].split("\n}\n", 1)[0]
        self.assertIn("PLUGPLAY_REGKEY_DRIVER", reader)
        self.assertIn("KEY_READ", reader)
        self.assertIn('L"UserModeDriverNameWow"', reader)
        self.assertIn("KeyValuePartialInformation", reader)
        self.assertIn("AdmissionWow64UmdNamed(", reader)
        self.assertIn("ZwClose(", reader)
        self.assertIn('<ClCompile Include="src\\render_wow64.c" />', PROJECT.read_text())

    def test_inf_registers_the_value_the_kmd_reads(self):
        inf = INF.read_text()
        self.assertIn("HKR,,UserModeDriverNameWow,%REG_MULTI_SZ%,AppleAgxRenderAdmissionUmd32.dll", inf)


if __name__ == "__main__":
    unittest.main()
