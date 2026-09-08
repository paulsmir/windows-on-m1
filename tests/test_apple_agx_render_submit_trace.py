import re
import json
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


class SubmitTraceContractTests(unittest.TestCase):
    def test_trace_word_is_exact_and_monotonic(self):
        source = r'''
#include <assert.h>
#include "render_submit_trace.h"
int main(void) {
  unsigned long long a = AdmissionSubmitTraceWord(1u, 0xffffffffu);
  unsigned long long b = AdmissionSubmitTraceWord(2u, 0u);
  unsigned long long c = AdmissionSubmitTraceWord(41u, 0xc000000du);
  unsigned long long d = AdmissionGdiSubmitTraceWord(9u, 0xc000000du);
  assert(a == 0x50700001ffffffffULL);
  assert(b == 0x5070000200000000ULL);
  assert(c == 0x50700029c000000dULL);
  assert(d == 0x50900009c000000dULL);
  assert(a < b && b < c);
  assert(AdmissionSubmitTraceField(a) == 1u);
  assert(AdmissionSubmitTraceValue(a) == 0xffffffffu);
  assert(AdmissionSubmitPacketFailureStatus(1u) == 0xe5390001u);
  assert(AdmissionSubmitPacketFailureStatus(8u) == 0xe5390008u);
  assert(AdmissionSubmitPacketFailureStatus(10u) == 0xe539000au);
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "trace.c"
            exe = Path(tmp) / "trace"
            src.write_text(source)
            subprocess.run([
                "clang", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(RENDER / "include"), str(src), "-o", str(exe)
            ], check=True)
            subprocess.run([str(exe)], check=True)

    def test_qualification_profile_is_explicit_and_normal_build_is_clean(self):
        project = (RENDER / "AppleAgxRenderAdmission.vcxproj").read_text()
        build = (RENDER / "scripts" / "build-driver.ps1").read_text()
        self.assertIn("AppleAgxSubmitQualification", project)
        self.assertIn("APPLE_AGX_SUBMIT_QUALIFICATION=1", project)
        self.assertIn("SubmitQualification", build)
        self.assertIn(
            "SubmitQualification must be a full-production-only discriminator",
            build
        )
        unconditional = re.search(
            r"<ItemDefinitionGroup>\s*<ClCompile>.*?</ClCompile>\s*</ItemDefinitionGroup>",
            project, re.S
        ).group(0)
        self.assertNotIn("APPLE_AGX_SUBMIT_QUALIFICATION", unconditional)

    def test_host_log_decoder_rejects_stale_and_reconstructs_u64(self):
        words = [
            0x5070000100000001,
            0x5070000301234567,
            0x5070000489ABCDEF,
            0x5070002700000001,
            0x5070002900000000,
        ]
        log = "\n".join(
            f"TTY> HV: AGX power receipt seq={word} cmd=0 state=3 result=0"
            for word in words
        )
        log += (
            "\nTTY> HV: AGX power receipt seq="
            f"{words[-1]} cmd=0 state=3 result=2\n"
        )
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "hardware.log"
            path.write_text(log)
            result = subprocess.run([
                "python3", str(RENDER / "scripts" / "decode-submit-trace.py"),
                str(path)
            ], check=True, text=True, capture_output=True)
            decoded = json.loads(result.stdout)
        self.assertEqual(decoded["version"], 1)
        self.assertEqual(decoded["args"], 0x89ABCDEF01234567)
        self.assertEqual(decoded["route"], 1)
        self.assertEqual(decoded["status"], 0)
        self.assertEqual(decoded["word_count"], 5)

    def test_gdi_submit_decoder_preserves_exact_status(self):
        values = {1: 1, 2: 0, 3: 253, 4: 0, 5: 96,
                  6: 0, 7: 0, 8: 8192, 9: 0xC000000D}
        log = "\n".join(
            "TTY> HV: AGX power receipt "
            f"seq={0x5090000000000000 | (field << 32) | value} "
            "cmd=0 state=3 result=0"
            for field, value in values.items()
        )
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "hardware.log"
            path.write_text(log)
            result = subprocess.run([
                "python3", str(RENDER / "scripts" / "decode-gdi-submit-trace.py"),
                str(path)
            ], check=True, text=True, capture_output=True)
            decoded = json.loads(result.stdout)
        self.assertEqual(decoded["fence"], 253)
        self.assertEqual(decoded["private_end"], 0)
        self.assertEqual(decoded["status"], 0xC000000D)


if __name__ == "__main__":
    unittest.main()
