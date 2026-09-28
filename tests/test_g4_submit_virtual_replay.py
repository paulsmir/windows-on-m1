"""Replay G4 virtual submit queueing and WDDM-compatible rejection paths."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "drivers/apple-agx/render-admission/src/gpuva_g3_windows.c"
PAGING = ROOT / "drivers/apple-agx/render-admission/src/gpuva_g3_paging_windows.c"


def function_body(source, name):
    matches = list(re.finditer(
        r"(?m)^(?:_Use_decl_annotations_\s+)?(?:static\s+)?"
        r"(?:NTSTATUS|BOOLEAN|int|void|VOID|ADMISSION_G3_PRIVATE_SCENE \*)\s*" + name +
        r"\s*\([^;]*?\)\s*\{", source, re.S))
    if not matches:
        raise AssertionError(f"missing production function {name}")
    match = matches[-1]
    depth = 1
    pos = match.end()
    while depth:
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
        pos += 1
    return source[source.rfind("\n", 0, match.start()) + 1:pos]


class G4SubmitVirtualReplay(unittest.TestCase):
    def test_64k_replacement_retires_old_cpu_shadow(self):
        update = function_body(PAGING.read_text(), "AdmissionG3UpdateLeaf")
        self.assertIn("AppleAgxGpuvaG3InvalidateLogical64K(shadow->LogicalPtes", update)

    def test_native_submit_queues_only_after_graph_and_image_bind(self):
        production = SOURCE.read_text()
        branches = production.split("/* Branch IDs are a stable diagnostic ABI", 1)[1]
        branches = "enum {" + branches.split("enum {", 1)[1].split("};", 1)[0] + "};"
        functions = branches + "\n" + "\n".join(function_body(production, name) for name in (
            "AdmissionG4SubmitReject", "AdmissionGpuvaG3PrivatePreempt",
            "AdmissionG4GraphAccess", "AdmissionG4LogicalEnvelopeAccess",
            "AdmissionG4GraphAccessTyped", "AdmissionG4FindPrivateScene","AdmissionG4FindPrivateResubmission","AdmissionG4PrivateGraphAccess","AdmissionG4PrivateGeometry","AdmissionG4PrivateUnqueue","AdmissionGpuvaG3PrivateContextBusy",
            "AdmissionG4SnapshotFailure", "AdmissionG4ResolveOutput",
            "AdmissionG4SubmitVirtualEnvelope",
            "AdmissionG3OutputMatchesLocal", "AdmissionGpuvaG3BeginJob",
            "AdmissionDdiSubmitCommandVirtual"))
        functions = function_body(production, "AdmissionG4SubmitRejectDetail") + "\n" + functions
        with tempfile.TemporaryDirectory(prefix="g4-submit-virtual-") as tmp:
            tmp = Path(tmp)
            (tmp / "g4_submit_virtual_functions.inc").write_text(functions)
            binary = tmp / "replay"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined", "-I", str(tmp),
                "-I", str(ROOT / "drivers/apple-agx/shared/include"),
                str(ROOT / "tests/g4_submit_virtual_replay.c"),
                str(ROOT / "drivers/apple-agx/shared/src/apple_agx_g4_submit.c"),
                str(ROOT / "drivers/apple-agx/shared/src/apple_agx_scheduler.c"),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)
