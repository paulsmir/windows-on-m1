"""Replay G4 virtual submit queueing and WDDM-compatible rejection paths."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "drivers/apple-agx/render-admission/src/gpuva_g3_windows.c"


def function_body(source, name):
    match = re.search(r"\b" + name + r"\s*\([^;]*?\)\s*\{", source, re.S)
    if match is None:
        raise AssertionError(f"missing production function {name}")
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
    def test_native_submit_queues_only_after_graph_and_image_bind(self):
        production = SOURCE.read_text()
        functions = "\n".join(function_body(production, name) for name in (
            "AdmissionG4GraphAccess", "AdmissionG4ResolveOutput",
            "AdmissionG4SubmitVirtualEnvelope"))
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
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)
