"""D3D-G9: every D3D10 sampling instruction the frontend emits reaches NIR.

Bug caught: the d3d10umd frontend emits the D3D10-style TGSI sampling
opcodes (SAMPLE, SAMPLE_B, SAMPLE_C, SAMPLE_C_LZ, SAMPLE_D, SAMPLE_L,
SAMPLE_I, SAMPLE_I_MS, SVIEWINFO), and upstream tgsi_to_nir translates none
of them; the projection's ttn_d3d_sample handled only SAMPLE, SAMPLE_L,
SAMPLE_I and SAMPLE_I_MS, so a shader with SampleCmp (shadow maps),
SampleBias, SampleGrad or GetDimensions could not be compiled. The test
collects the opcodes the pinned frontend (and the projection's own
ShaderTGSI additions) emit and requires a case for each in the projected
tgsi_to_nir.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py"
SHADER_TGSI = ROOT / ".local/reference/mesa/src/gallium/frontends/d3d10umd/ShaderTGSI.c"


class SampleOpcodesTranslated(unittest.TestCase):
    def test_projected_tgsi_to_nir_handles_every_emitted_sampling_opcode(self):
        if not SHADER_TGSI.exists():
            self.skipTest("pinned Mesa reference is not present")
        projection = SOURCE.read_text()
        emitted = set(re.findall(r"sample_ureg_emit\(\s*ureg\s*,\s*(TGSI_OPCODE_\w+)",
                                 SHADER_TGSI.read_text() + projection))
        if "ureg_SVIEWINFO" in SHADER_TGSI.read_text():
            emitted.add("TGSI_OPCODE_SVIEWINFO")
        self.assertIn("TGSI_OPCODE_SAMPLE_C", emitted)
        start = projection.index("change('src/gallium/auxiliary/nir/tgsi_to_nir.c'")
        block = projection[start:projection.index("    change(", start + 10)]
        handled = set(re.findall(r"case (TGSI_OPCODE_\w+):", block))
        self.assertEqual(sorted(emitted - handled), [])


if __name__ == "__main__":
    unittest.main()
