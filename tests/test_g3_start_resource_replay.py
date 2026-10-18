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
SCANOUT = ROOT / "drivers/apple-agx/render-admission/src/scanout_windows.c"
HEADER = ROOT / "drivers/apple-agx/render-admission/include/render_admission.h"
RENDER = ROOT / "drivers/apple-agx/render-admission"
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
        translate = function_body(PHYSICAL.read_text(), "AdmissionPhysicalTranslate")
        release_raw = function_body(PHYSICAL.read_text(), "AdmissionPhysicalReleaseRaw", "VOID")
        physical_free = function_body(PHYSICAL.read_text(), "AdmissionPhysicalFree")
        gate = function_body(LIFECYCLE.read_text(), "AdmissionG3FirmwareResourcesPresent", "BOOLEAN")
        capture = function_body(RECEIPTS.read_text(), "AdmissionFillTranslatedResources", "void")
        memory_source = MEMORY.read_text()
        get_runtime = function_body(memory_source, "AdmissionMemoryGetRuntime", r"ADMISSION_MEMORY_RUNTIME \*")
        local_view = function_body(memory_source, "AdmissionMemoryRuntimeLocalView")
        scanout_view = function_body(memory_source, "AdmissionMemoryRuntimeScanoutView")
        backend_view = function_body(memory_source, "AdmissionMemoryRuntimeBackendView")
        scanout_start = function_body(SCANOUT.read_text(), "AdmissionScanoutStart")
        scanout_get = function_body(SCANOUT.read_text(), "AdmissionScanoutGet", r"ADMISSION_SCANOUT_RUNTIME \*")
        queue_present = function_body(SCANOUT.read_text(), "AdmissionScanoutQueuePresent")
        scanout_stop = function_body(SCANOUT.read_text(), "AdmissionScanoutStop")
        view_type = re.search(r"typedef struct _ADMISSION_SCANOUT_MEMORY_VIEW.*?} ADMISSION_SCANOUT_MEMORY_VIEW;", HEADER.read_text(), re.S).group()
        constants = "\n".join(re.findall(r"^#define ADMISSION_(?:LOCAL|PRIVATE|BACKEND)[^\n]+", memory_source, re.M))
        queries = (ROOT / "tests/fixtures/g3_segment_query_replay.inc").read_text().replace(
            "/* PRODUCTION_SEGMENT_QUERIES */",
            (RENDER / "src/memory_windows.c").read_text().replace('#include "render_admission.h"', ''))
        lifecycle = (ROOT / "tests/fixtures/g3_memory_lifecycle_replay.inc").read_text()
        lifecycle_functions = "\n".join(function_body(memory_source,n,t) for n,t in [
            ("AdmissionGpuRegionAssigned","BOOLEAN"),
            ("AdmissionMemoryAllocateContiguous","unsigned char"),
            ("AdmissionMemoryFreeContiguous","unsigned char"),
            ("AdmissionMemoryFreeInventories","VOID"),
            ("AdmissionMemoryRuntimeDestroy","NTSTATUS"),
            ("AdmissionMemoryRuntimeStart","NTSTATUS"),
            ("AdmissionMemoryRuntimeStop","NTSTATUS")])
        lifecycle_functions = lifecycle_functions.replace("AdmissionMemoryMarkUatReady(", "lifecycle_mark_ready(")
        stage_enum = re.search(r"typedef enum _ADMISSION_MEMORY_START_STAGE.*?} ADMISSION_MEMORY_START_STAGE;",HEADER.read_text(),re.S).group()
        alias_source=(RENDER / "src/render_dynamic_overlay.c").read_text().split("static void overlay_zero",1)[0].replace('#include "render_dynamic_overlay.h"','')
        alias_header=(RENDER / "include/render_dynamic_overlay.h").read_text()
        alias_types=re.search(r"typedef struct _ADMISSION_DYNAMIC_OVERLAY_ALIAS.*?} ADMISSION_DYNAMIC_OVERLAY_ALIAS;",alias_header,re.S).group()+"\n"+re.search(r"#define ADMISSION_DYNAMIC_OVERLAY_SHADER_ALIAS_COUNT.*",alias_header).group()
        lifecycle=lifecycle.replace("/* PRODUCTION_ALIAS_TYPES */",alias_types)
        lifecycle=lifecycle.replace("/* PRODUCTION_STAGE_ENUM */",stage_enum).replace("/* PRODUCTION_ALIAS_DATA */",alias_source).replace("/* PRODUCTION_MEMORY_LIFECYCLE */",lifecycle_functions)
        shim = (ROOT / "tests/fixtures/g3_start_resource_replay.c").read_text()
        scanout_source=(RENDER / "src/scanout_windows.c").read_text()
        runtime=re.search(r'typedef struct _ADMISSION_SCANOUT_RUNTIME.*?} ADMISSION_SCANOUT_RUNTIME;',scanout_source,re.S).group()
        control=re.search(r'_Use_decl_annotations_ static BOOLEAN AdmissionScanoutVsyncControl\(.*?^}',scanout_source,re.S|re.M).group()
        shim=shim.replace("/* PRODUCTION_SCANOUT_RUNTIME */",runtime).replace("/* PRODUCTION_VSYNC_CONTROL */",control)
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "replay.c"
            binary = Path(directory) / "replay"
            source.write_text(shim.replace("/* PRODUCTION_BORROW */", borrow)
                              .replace("/* PRODUCTION_PHYSICAL_TRANSLATE */", translate)
                              .replace("/* PRODUCTION_RELEASE_RAW */", release_raw)
                              .replace("/* PRODUCTION_PHYSICAL_FREE */", physical_free)
                              .replace("/* PRODUCTION_G3_GATE */", gate)
                              .replace("/* PRODUCTION_RESOURCE_CAPTURE */", capture)
                              .replace("/* PRODUCTION_MEMORY_GET_RUNTIME */", get_runtime)
                              .replace("/* PRODUCTION_LOCAL_VIEW */", local_view)
                              .replace("/* PRODUCTION_SCANOUT_VIEW */", scanout_view)
                              .replace("/* PRODUCTION_MEMORY_CONSTANTS */", constants)
                              .replace("/* PRODUCTION_VIEW_TYPE */", view_type)
                              .replace("/* PRODUCTION_BACKEND_VIEW */", backend_view)
                              .replace("/* PRODUCTION_SCANOUT_START */", scanout_start)
                              .replace("/* PRODUCTION_SCANOUT_STOP */", scanout_stop)
                              .replace("/* PRODUCTION_QUEUE_PRESENT */", scanout_get + "\n" + queue_present)
                              .replace("/* PRODUCTION_VALIDATOR */", validator)
                              .replace("/* SEGMENT_QUERY_REPLAY */", queries)
                              .replace("/* MEMORY_LIFECYCLE_REPLAY */", lifecycle))
            built = subprocess.run(
                ["clang", "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-Wno-unused-function",
                 "-I", str(INCLUDE), "-I", str(RENDER / "include"),
                 "-I", str(ROOT / "m1n1_windows/src"), str(source),
                 str(ROOT / "m1n1_windows/src/hv_agx_scanout_broker.c"),
                 str(RENDER / "src/render_memory.c"),
                 *[str(INCLUDE.parent / "src" / (name + ".c")) for name in (
                     "apple_agx_scanout", "apple_agx_fixed_panel",
                     "apple_agx_software_aperture", "apple_agx_local_segment",
                     "apple_agx_physical_topology", "apple_agx_physical_paging",
                     "apple_agx_aperture")],
                 str(ROOT / "drivers/apple-agx/shared/src/apple_agx_residency.c"),
                 str(ROOT / "drivers/apple-agx/shared/src/apple_agx_render_template.generated.c"),
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
