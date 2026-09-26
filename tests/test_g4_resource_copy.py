"""EXP844: DWM received 892 E_NOTIMPL from ResourceCopy (overlay line 1154),
which accepted only one-level, one-layer 2D BGRA/RGBA pairs. The D3D10 DDI
contract copies every subresource of identically shaped resources."""

from pathlib import Path
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py"


def body():
    source = SCRIPT.read_text()
    marker = "'ResourceCopy','''"
    start = source.index(marker) + len(marker)
    return source[start:source.index("''')", start)]


class ResourceCopyOverlay(unittest.TestCase):
    def test_every_subresource_is_copied(self):
        b = body()
        self.assertNotIn("destination->MipLevels != 1", b)
        self.assertNotIn("dst->array_size!=1", b)
        self.assertNotIn("dst->target!=PIPE_TEXTURE_2D", b)
        self.assertIn("level<=dst->last_level", b)
        self.assertIn("layer<layers", b)
        self.assertIn("u_minify(src->width0,level)", b)

    def test_same_format_and_views_are_raw(self):
        b = body()
        self.assertIn("src->format==dst->format", b)
        self.assertIn("util_format_linear(src->format)", b)
        self.assertIn("!util_format_is_compressed(src->format)", b)
        self.assertIn("PIPE_TEX_FILTER_NEAREST", b)

    def test_shape_contract_and_diagnostic_remain(self):
        b = body()
        for rule in ("dst->width0!=src->width0", "dst->last_level!=src->last_level",
                     "dst->array_size!=src->array_size", "destination->owner_device != device",
                     "destination->usage == D3D10_DDI_USAGE_IMMUTABLE"):
            self.assertIn(rule, b)
        self.assertIn('"reject-resource-copy"', b)
        self.assertIn("destination->transfers[i]", b)


if __name__ == "__main__":
    unittest.main()
