"""EXP1287: D3D pixel shaders export once, at the end.

EXP1287 sample smoke (PACKAGE1287): the ARM64 UMD aborted compiling a pixel
shader that returns early ("Invalid block form after IR translation", AGX
exports in non-terminal blocks) and the x86 UMD drew values that changed
every iteration. tgsi_to_nir stores every output before each RET; after
nir_lower_returns those stores sit in branches, while agx_validate requires
a fragment export to end the shader. Honeykrisp lowers shader outputs to
temporaries copied once at the end (hk_shader.c); the D3D (TGSI) path of
agx_create_shader_state now does the same before agx_shader_initialize.
Bug caught: dropping or misplacing the lowering (exports back in branches).
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py"
HK = ROOT / ".local/reference/mesa/src/asahi/vulkan/hk_shader.c"


class FragmentOutputsToTemporaries(unittest.TestCase):
    def test_tgsi_fragment_outputs_are_lowered_after_translation(self):
        source = GENERATOR.read_text()
        start = source.index("tgsi_to_nir(cso->tokens, pctx->screen, false);'''")
        block = source[start:source.index("scan_anchor=", start)]
        guard = "if (cso->type == PIPE_SHADER_IR_TGSI &&\n       nir->info.stage == MESA_SHADER_FRAGMENT)"
        self.assertIn(guard, block)
        passes = ["nir_lower_io_vars_to_temporaries,\n               nir_shader_get_entrypoint(nir), nir_var_shader_out",
                  "nir_lower_global_vars_to_local", "nir_split_var_copies",
                  "nir_lower_var_copies", "nir_lower_vars_to_ssa"]
        offsets = [block.index(p) for p in passes]
        self.assertEqual(offsets, sorted(offsets))
        self.assertIn("state_text=state_text.replace(shader_ir,shader_ir+", block)

    @unittest.skipUnless(HK.exists(), "pinned Mesa reference not present")
    def test_the_reference_driver_does_the_same(self):
        hk = HK.read_text()
        self.assertIn("nir_lower_io_vars_to_temporaries,\n            nir_shader_get_entrypoint(nir), nir_var_shader_out", hk)


if __name__ == "__main__":
    unittest.main()
