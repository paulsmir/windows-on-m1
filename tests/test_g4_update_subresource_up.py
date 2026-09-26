"""EXP840: DWM restarted 525 times because UpdateSubresourceUP accepted only
whole constant-buffer updates. The overlay must follow the D3D10 DDI contract
for boxed buffer ranges and texture subresources."""

from pathlib import Path
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py"


def overlay_body():
    source = SCRIPT.read_text()
    marker = "'ResourceUpdateSubResourceUP','''"
    start = source.index(marker) + len(marker)
    return source[start:source.index("''')", start)]


class UpdateSubresourceUpOverlay(unittest.TestCase):
    def test_textures_and_boxes_are_supported(self):
        body = overlay_body()
        self.assertNotIn("resource->constant_buffer", body)
        self.assertNotIn("!pDstBox", body)
        self.assertIn("texture_map", body)
        self.assertIn("buffer_map", body)
        self.assertIn("pDstBox->left", body)
        self.assertIn("DstSubResource % mip_levels", body)
        self.assertIn("util_copy_rect", body)

    def test_ownership_bounds_and_empty_box_rules_remain(self):
        body = overlay_body()
        self.assertIn("resource->owner_cookie != owner", body)
        self.assertIn("layer >= layers", body)
        self.assertIn("pDstBox->right > width", body)
        # An empty box is a D3D no-op, not an error.
        self.assertIn("pDstBox->right <= pDstBox->left", body)
        self.assertIn("AgxD3d10WindowsFlushRetire", body)


class CreateResourceOwnership(unittest.TestCase):
    def test_every_resource_records_device_identity(self):
        """EXP841: textures kept owner_cookie 0, so UpdateSubresourceUP rejected them."""
        source = SCRIPT.read_text()
        end = source.index("   if (bufferResource) {\n      pResource->constant_buffer")
        start = source.rindex("pResource->owner_device = pDevice;", 0, end)
        common = source[start:end]
        self.assertIn("pResource->owner_cookie = resourceOwner;", common)
        self.assertIn("pResource->device_generation = resourceGeneration;", common)
        self.assertIn("AgxD3d10WindowsIdentity", common)
        create = source.index("HRESULT result = AgxD3d10WindowsPresentationCreate(")
        presentation = source[create:create + 1200]
        self.assertIn("pResource->owner_cookie = resourceOwner;", presentation)


if __name__ == "__main__":
    unittest.main()
