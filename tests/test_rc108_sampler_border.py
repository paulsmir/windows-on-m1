"""RC108: D3D12 BORDER samplers return their BorderColor, not transparent black.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? The owner's create_sampler
never set pipe_sampler_state::border_color_format, so Asahi packed every
D3D12 BORDER sampler (slot or descriptor heap) with "Transparent black"
(agx_state.c agx_create_sampler_state) whatever its BorderColor: an opaque
white shadow-map border read as fully shadowed (rc105 side finding). The
fix maps the D3D12 border colours to AGX's built-in ones
(agx_win32_sampler_border.h) and rewrites only the packed border field. This
test checks (1) the five D3D12 static border colours map to the cmdbuf.xml
enum and only BORDER address modes count, (2) the field patch, derived from
Asahi's generated AGX_SAMPLER_pack, changes the border colour and no other
sampler field, and (3) both Asahi forms (slot desc and heap
desc_without_custom_border) and create_sampler take it.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_dxbc_to_nir import local_tree, host_flags, ROOT

WINSYS = ROOT / "drivers/apple-agx/mesa/winsys"


class SamplerBorder(unittest.TestCase):
    def test_built_in_border_colours_pack_into_the_agx_sampler(self):
        build = local_tree("accelerated-desktop-ad03", "mesa-build")
        mesa = local_tree("reference", "mesa")
        if not build or not mesa or not (build / "src/asahi/genxml/agx_pack.h").exists():
            self.skipTest("host Mesa build with the Asahi genxml headers not available")
        with tempfile.TemporaryDirectory(prefix="rc108-border-") as tmp:
            binary = Path(tmp) / "border"
            subprocess.run(["clang", "-std=c11", "-Werror=implicit-function-declaration", *host_flags(build),
                            "-I", str(build / "src"), "-I", str(mesa / "src"), "-I", str(mesa / "include"),
                            "-I", str(WINSYS), str(ROOT / "tests/agx_sampler_border_fixture.c"), "-o", str(binary)],
                           check=True, capture_output=True)
            r = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("SAMPLER_BORDER_PASS", r.stdout, r.stdout)

    def test_both_asahi_sampler_forms_and_the_owner_take_the_border(self):
        scene = (WINSYS / "agx_win32_asahi_scene.c").read_text()
        body = scene[scene.index("void AgxWin32AsahiSamplerSetBorder(void *cso"):]
        body = body[:body.index("\n}\n")]
        self.assertIn("AgxWin32SamplerSetBorder(&so->desc,", body)
        self.assertIn("AgxWin32SamplerSetBorder(&so->desc_without_custom_border,", body)
        owner = (WINSYS / "agx_d3d12_windows.cpp").read_text()
        create = owner[owner.index("static void *create_sampler("):]
        create = create[:create.index("\n}\n")]
        self.assertIn("AgxWin32SamplerBorderColour(s->AddressU, s->AddressV, s->AddressW, s->BorderColor)", create)
        self.assertIn("AgxWin32AsahiSamplerSetBorder(cso, border)", create)


if __name__ == "__main__":
    unittest.main()
