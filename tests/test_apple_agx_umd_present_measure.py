from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "drivers/apple-agx/render-admission/umd/src/umd_runtime_device.c"
GENERATOR = ROOT / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py"


class UmdPresentMeasureTests(unittest.TestCase):
    def test_bounded_count_sampling(self):
        source = RUNTIME.read_text()
        match = re.search(
            r"static BOOL AdmissionUmdPresentMeasureSample\(.*?^}",
            source, re.S | re.M,
        )
        self.assertIsNotNone(match)
        program = """
#include <assert.h>
typedef int BOOL;
typedef unsigned int ULONG;
#define TRUE 1
#define FALSE 0
""" + match.group(0) + """
int main(void) {
  assert(!AdmissionUmdPresentMeasureSample(0));
  for (ULONG count = 1; count <= 256; ++count)
    assert(AdmissionUmdPresentMeasureSample(count) ==
           ((count & (count - 1u)) == 0u));
  return 0;
}
"""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "measure.c"
            executable = Path(temp) / "measure"
            path.write_text(program)
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined", str(path),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            subprocess.run([str(executable)], check=True, cwd=ROOT)

    def test_native_edges_and_flush_branch_are_instrumented(self):
        generator = GENERATOR.read_text()
        winsys = (ROOT / "drivers/apple-agx/mesa/winsys/agx_d3d10_windows.cpp").read_text()
        umd = (ROOT / "drivers/apple-agx/render-admission/umd/src/umd.c").read_text()
        for name in (
            "AdmissionUmdMeasureNativeDevice",
            "AdmissionUmdMeasureNativePresentEntry",
            "AdmissionUmdMeasureNativePresentReturn",
            "AdmissionUmdMeasureNativeBltEntry",
            "AdmissionUmdMeasureNativeBltReturn",
            "AdmissionUmdMeasureNativeFlushStage",
        ):
            self.assertTrue(name in generator, name)
        self.assertIn("AdmissionUmdMeasureNativeSubmitEntry", winsys)
        self.assertIn("AdmissionUmdMeasureNativeSubmitReturn", winsys)
        self.assertIn("AdmissionUmdMeasurePresentCallbackEnter", umd)
        self.assertIn("AdmissionUmdMeasurePresentCallbackReturn", umd)
        self.assertIn("AdmissionUmdMeasureLegacyPresentEntry", umd)
        self.assertIn("AdmissionUmdMeasureLegacyPresent1Entry", umd)


if __name__ == "__main__":
    unittest.main()
