"""The UMD sources must compile without APPLE_AGX_GPUVA_WINSYS.

AppleAgxRenderAdmissionUmd.vcxproj compiles them with EnableGpuvaWinsys=false
unless build-driver.ps1 gets -GpuvaG3Qualification, and UmdContractTest
(the Windows contract test of the UMD runtime) always does.  0dd8bb24
(EXP1131) read the GPUVA-only Direct and Resident slot fields in
AdmissionUmdScreenForgetAllocation outside that guard, so both builds failed
with C2039 and the Windows contract test stopped running.  Slot fields that
exist only under APPLE_AGX_GPUVA_WINSYS may be used only under it.
"""
from pathlib import Path
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
UMD = ROOT / "drivers/apple-agx/render-admission/umd"
HEADER = UMD / "src/umd_internal.h"
# Compiled by the UMD and by UmdContractTest without APPLE_AGX_GPUVA_WINSYS.
SOURCES = ("umd.c", "umd_runtime_device.c", "umd_win32_screen.c",
           "umd_resource_lifetime.c", "umd_draw_composer.c",
           "direct_flip_contract.c")


def without_gpuva(text):
    result = subprocess.run(["unifdef", "-UAPPLE_AGX_GPUVA_WINSYS"],
                            input=text, capture_output=True, text=True)
    if result.returncode not in (0, 1):  # 1: output differs from input
        raise AssertionError(result.stderr)
    return result.stdout


def uncommented(text):
    # Keep newlines so reported line numbers match the preprocessed source.
    return re.sub(r"/\*.*?\*/|//[^\n]*",
                  lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)


def slot_fields(header):
    body = re.search(r"typedef struct _ADMISSION_UMD_SCREEN_BUFFER \{(.*?)\}"
                     r" ADMISSION_UMD_SCREEN_BUFFER;", header, re.S).group(1)
    return set(re.findall(r"(\w+)\s*(?:\[[^\]]*\])?\s*;", uncommented(body)))


def gpuva_slot_uses(source, gpuva_only):
    text = uncommented(without_gpuva(source))
    pointers = set(re.findall(
        r"ADMISSION_UMD_SCREEN_BUFFER\s*\*\s*(?:const\s+)?(\w+)", text))
    accesses = [r"\bScreenBuffers\s*\[[^\]]*\]\s*\.\s*(\w+)"]
    if pointers:
        accesses.append(r"\b(?:%s)\s*->\s*(\w+)" % "|".join(sorted(pointers)))
    uses = []
    for pattern in accesses:
        for match in re.finditer(pattern, text):
            if match.group(1) in gpuva_only:
                line = text.count("\n", 0, match.start()) + 1
                uses.append("line %d: %s" % (line, match.group(0)))
    return uses


class UmdNonGpuvaScreenFieldTests(unittest.TestCase):
    def test_gpuva_only_slot_fields_stay_under_the_guard(self):
        header = HEADER.read_text()
        gpuva_only = slot_fields(header) - slot_fields(without_gpuva(header))
        # The fields 0dd8bb24 used unguarded must be among those checked.
        self.assertTrue({"Direct", "Resident"} <= gpuva_only, gpuva_only)
        for name in SOURCES:
            with self.subTest(source=name):
                self.assertEqual(
                    [], gpuva_slot_uses((UMD / "src" / name).read_text(),
                                        gpuva_only))


if __name__ == "__main__":
    unittest.main()
