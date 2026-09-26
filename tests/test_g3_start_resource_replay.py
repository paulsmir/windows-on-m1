"""Run the production KMD platform resource validator with EXP831 inputs."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "drivers/apple-agx/render-admission/src/backend_platform_windows.c"
PHYSICAL = ROOT / "drivers/apple-agx/render-admission/src/physical_memory_windows.c"
LIFECYCLE = ROOT / "drivers/apple-agx/render-admission/src/lifecycle.c"
RECEIPTS = ROOT / "drivers/apple-agx/render-admission/src/receipts.c"
MEMORY = ROOT / "drivers/apple-agx/render-admission/src/memory_runtime_windows.c"
INCLUDE = ROOT / "drivers/apple-agx/shared/include"


def function_body(source, name, return_type="NTSTATUS"):
    match = re.search(r"(?:static )?" + return_type + r"\s*" + name + r"\s*\([^;]*?\)\s*\{", source, re.S)
    if match is None:
        raise AssertionError(f"missing production function {name}")
    depth = 1
    cursor = match.end()
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[match.start():cursor]


class G3StartResourceReplay(unittest.TestCase):
    def test_exp833_exact_eleven_descriptors_and_fail_closed_cases(self):
        validator = function_body(SOURCE.read_text(), "AdmissionPlatformValidateResources")
        borrow = function_body(PHYSICAL.read_text(), "AdmissionPhysicalBorrowLocal")
        release_raw = function_body(PHYSICAL.read_text(), "AdmissionPhysicalReleaseRaw", "VOID")
        physical_free = function_body(PHYSICAL.read_text(), "AdmissionPhysicalFree")
        gate = function_body(LIFECYCLE.read_text(), "AdmissionG3FirmwareResourcesPresent", "BOOLEAN")
        capture = function_body(RECEIPTS.read_text(), "AdmissionFillTranslatedResources", "void")
        memory_source = MEMORY.read_text()
        get_runtime = function_body(memory_source, "AdmissionMemoryGetRuntime", r"ADMISSION_MEMORY_RUNTIME \*")
        scanout_view = function_body(memory_source, "AdmissionMemoryRuntimeScanoutView")
        shim = (ROOT / "tests/fixtures/g3_start_resource_replay.c").read_text()
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "replay.c"
            binary = Path(directory) / "replay"
            source.write_text(shim.replace("/* PRODUCTION_BORROW */", borrow)
                              .replace("/* PRODUCTION_RELEASE_RAW */", release_raw)
                              .replace("/* PRODUCTION_PHYSICAL_FREE */", physical_free)
                              .replace("/* PRODUCTION_G3_GATE */", gate)
                              .replace("/* PRODUCTION_RESOURCE_CAPTURE */", capture)
                              .replace("/* PRODUCTION_MEMORY_GET_RUNTIME */", get_runtime)
                              .replace("/* PRODUCTION_SCANOUT_VIEW */", scanout_view)
                              .replace("/* PRODUCTION_VALIDATOR */", validator))
            built = subprocess.run(
                ["clang", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-Wno-unused-function",
                 "-I", str(INCLUDE), str(source),
                 str(ROOT / "drivers/apple-agx/shared/src/apple_agx_residency.c"),
                 str(ROOT / "drivers/apple-agx/shared/src/apple_agx_uat_memory.c"),
                 str(ROOT / "drivers/apple-agx/shared/src/apple_agx_uat_table.c"),
                 str(ROOT / "drivers/apple-agx/shared/src/apple_agx_uat.c"),
                 str(ROOT / "drivers/apple-agx/shared/src/apple_agx_memory.c"),
                 str(ROOT / "drivers/apple-agx/shared/src/apple_agx_uat_publication.c"),
                 "-o", str(binary)],
                capture_output=True, text=True,
            )
            self.assertEqual(built.returncode, 0, built.stderr)
            replay = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(replay.returncode, 0, replay.stdout + replay.stderr)
            self.assertIn("EXP831 exact resources PASS", replay.stdout)


if __name__ == "__main__":
    unittest.main()
