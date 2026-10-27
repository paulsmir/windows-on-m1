"""RC78: a D3D shader's texture descriptor crawls may run in the preamble.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
winsys/agx_win32_d3d_texture_crawl.h marks a crawl load (libagx reading a
texture descriptor: array-layer clamp, size query, txf robustness)
ACCESS_CAN_SPECULATE when its handle indexes Gallium's per-draw descriptor
table below the table size; it then runs in the preamble, also when no
invocation would take its branch. That is safe only while the pinned
Gallium driver keeps the premises it was proven on:
- agx_upload_textures fills AGX_TEXTURE_LENGTH bytes for each of
  nr_bindful_textures + 2 * nr_bindful_images descriptors, a null
  descriptor for every unbound slot;
- agx_nir_lower_bindings clamps a bindless texture index to the shader's
  textures, and a handle is 24 bytes apart (AGX_TEXTURE_DESC_STRIDE, the
  header's AGX_WIN32_TEXTURE_DESCRIPTOR_BYTES).
A Mesa bump that changes one of them must fail here. The projection must
call the pass for D3D contexts only, after agx_nir_lower_texture (which
emits the crawls) and before agx_preprocess_nir, with the table size
agx_upload_textures uses (an OpenGL context keeps Asahi's placement).
"""
from pathlib import Path
import re
import tempfile
import unittest

from test_d3d10_shader_fields_init import function_body, local_tree, project

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "drivers/apple-agx/mesa/winsys/agx_win32_d3d_texture_crawl.h"


class CrawlPremises(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        mesa = local_tree("reference", "mesa")
        if not mesa:
            raise unittest.SkipTest("reference Mesa tree not available")
        cls.asahi = mesa / "src/gallium/drivers/asahi"

    def test_descriptor_stride_matches_gallium(self):
        stride = re.search(r"#define AGX_TEXTURE_DESC_STRIDE (\d+)",
                           (self.asahi / "agx_nir_lower_sysvals.c").read_text())
        ours = re.search(r"#define AGX_WIN32_TEXTURE_DESCRIPTOR_BYTES (\d+)u", HEADER.read_text())
        self.assertTrue(stride and ours)
        self.assertEqual(stride.group(1), ours.group(1))

    def test_every_descriptor_of_the_table_is_written(self):
        state = (self.asahi / "agx_state.c").read_text()
        count = function_body(state, "agx_nr_tex_descriptors_without_spilled_rts")
        self.assertRegex(count, r"return cs->so->info\.nr_bindful_textures \+\s+"
                                r"\(2 \* cs->so->info\.nr_bindful_images\);")
        upload = function_body(state, "agx_upload_textures")
        self.assertIn("AGX_TEXTURE_LENGTH * nr_tex_descriptors", upload)
        self.assertIn("for (unsigned i = nr_active_textures; i < nr_textures; ++i)\n"
                      "      agx_set_null_texture(&textures[i]);", upload)
        self.assertIn("agx_set_null_texture(&textures[i]);\n         continue;", upload)

    def test_bindless_texture_indices_are_clamped(self):
        bindings = (self.asahi / "agx_nir_lower_bindings.c").read_text()
        self.assertRegex(bindings, r"index = nir_umin\(\s+b, index,\s+nir_imm_intN_t\(b, "
                                   r"b->shader->info\.num_textures - 1, index->bit_size\)\);")


class CrawlProjection(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="d3d-texture-crawl-projection-")
        out = project(Path(cls.tmp.name) / "out")
        if out is None:
            cls.tmp.cleanup()
            raise unittest.SkipTest("reference Mesa tree not available")
        cls.state = (out / "src/gallium/drivers/asahi/agx_state.c").read_text()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_d3d_shaders_speculate_their_crawls_after_texture_lowering(self):
        self.assertIn('#include "agx_win32_d3d_texture_crawl.h"', self.state)
        body = function_body(self.state, "agx_shader_initialize")
        sizes = body.index("so->info.nr_bindful_images = BITSET_LAST_BIT(nir->info.images_used);")
        lower = body.index("NIR_PASS(_, nir, agx_nir_lower_texture);")
        call = re.search(r"if \(d3d_robust\)\s+NIR_PASS\(_, nir, agx_win32_speculate_texture_crawls,\s+"
                         r"so->info\.nr_bindful_textures \+ 2 \* so->info\.nr_bindful_images\);", body)
        self.assertTrue(call, "crawl speculation call")
        self.assertLess(sizes, lower)
        self.assertLess(lower, call.start())
        self.assertLess(call.end(), body.index("agx_preprocess_nir(nir);"))
        self.assertEqual(body.count("agx_win32_speculate_texture_crawls"), 1)


if __name__ == "__main__":
    unittest.main()
