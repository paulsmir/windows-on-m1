"""Every D3D10 shader-creation entry point initializes the projection's fields.

EXP1291: DWM crashed twice in the ARM64 UMD (0xc0000005 in
_mesa_hash_table_next_entry) after D3D-G10e added Shader::so_vs_handle,
so_vs_serial and agx_serial.  Only CreateGeometryShaderWithStreamOutput set
them; DestroyShader deletes so_vs_handle for every shader, and the runtime
hands the driver uninitialized private shader memory, so destroying an
ordinary VS/PS deleted a garbage shader state.
Invariant: in the projected Shader.cpp every Create*Shader entry point
assigns each of these fields before it creates any pipe state.

The projection runs exactly as the build does (prepare-only) against the
reference Mesa tree; the test is skipped where that tree is absent.
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py"
FIELDS = ("so_vs_handle", "so_vs_serial", "agx_serial")
CREATORS = ("CreateVertexShader", "CreateGeometryShader",
            "CreateGeometryShaderWithStreamOutput", "CreatePixelShader")


def local_tree(*parts):
    candidates = [Path(os.environ[v]) for v in ("AGX_LOCAL_TREE",) if v in os.environ]
    candidates += [ROOT / ".local", ROOT.parents[1] / ".local"]
    for base in candidates:
        path = base.joinpath(*parts)
        if path.exists():
            return path
    return None


def project(output):
    mesa = local_tree("reference", "mesa")
    libagx = local_tree("accelerated-desktop-ad03", "mesa-build", "src", "asahi", "lib", "libagx_shaders.h")
    if not mesa or not libagx:
        return None
    text = SCRIPT.read_text()
    text, n1 = re.subn(r"(?m)^mesa = Path\(r'C:.Users.pauls.AD04-d3d10-frontend-build.mesa'\)",
                       lambda m: f"mesa = Path({str(mesa)!r})", text)
    text, n2 = re.subn(r"Path\(r.C:\\Users\\pauls\\AD04-umd-owner-004-generated\\src\\asahi\\lib\\libagx_shaders.h.\)",
                       lambda m: f"Path({str(libagx)!r})", text)
    if n1 != 1 or n2 != 1:
        raise AssertionError("projection reference paths moved")
    runner = Path(output).parent / "projection.py"
    runner.write_text(text)
    subprocess.run([sys.executable, str(runner), "--output", str(output),
                    "--windows-platform-declarations", "--native-batch-lifecycle", "--gpuva",
                    "--prepare-only", "--architecture", "arm64", "--project", str(ROOT)],
                   check=True, capture_output=True, timeout=300)
    return Path(output)


def function_body(text, name):
    m = re.search(r"\n" + name + r"\(", text)
    start = text.index("{", m.end())
    depth, pos = 1, start + 1
    while depth:
        depth += {"{": 1, "}": -1}.get(text[pos], 0)
        pos += 1
    return text[start:pos]


class ShaderFieldsInit(unittest.TestCase):
    def test_creators_initialize_destroyed_fields(self):
        with tempfile.TemporaryDirectory(prefix="d3d10-shader-fields-") as tmp:
            out = project(Path(tmp) / "out")
            if out is None:
                self.skipTest("reference Mesa tree not available")
            shader = (out / "src/gallium/frontends/d3d10umd/Shader.cpp").read_text()
            self.assertIn("pShader->so_vs_handle", function_body(shader, "DestroyShader"))
            for name in CREATORS:
                body = function_body(shader, name)
                first_state = min(i for i in (body.find("create_vs_state"), body.find("create_gs_state"),
                                              body.find("create_fs_state"),
                                              body.find("AgxD3d10CreateShaderState(")) if i >= 0)
                for field in FIELDS:
                    with self.subTest(creator=name, field=field):
                        m = re.search(r"pShader->" + field + r"\s*=", body)
                        self.assertTrue(m and m.start() < first_state,
                                        f"{name} leaves {field} uninitialized")


if __name__ == "__main__":
    unittest.main()
