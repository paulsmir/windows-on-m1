"""EXP1046: UpdateSubresourceUP of a presentation surface uploads through the GPU.

EXP1045 ApplicationFrameHost dump: DCompUtil::UpdateSinglePixelSurfaceColor ->
UpdateSubresource -> ResourceUpdateSubResourceUP -> util_copy_rect wrote to
0x104 (NULL map + offset). Presentation surfaces are imported VidMm
allocations without a CPU view (Direct, CpuVisible 0): native_map fails, marks
the backend failed and Asahi's transfer returns NULL plus the box offset. Every
UWP frame (Settings) crashed in ApplicationFrameHost at its background colour.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/mesa/scripts/build-native-asahi-state.py'


def body(name):
    text = SRC.read_text()
    start = text.index("'src/gallium/frontends/d3d10umd/Resource.cpp','" + name + "','''")
    return text[start:text.index("''')", start)]


class PresentationUpdate(unittest.TestCase):
    def test_presentation_update_uses_staging_and_blit(self):
        update = body('ResourceUpdateSubResourceUP')
        branch = update[update.index('if (resource->presentation'):]
        mapped = branch.index('texture_map(pDevice->pipe, staging')
        self.assertIn('PIPE_USAGE_STAGING', branch[:mapped])
        self.assertLess(mapped, branch.index('pDevice->pipe->blit(pDevice->pipe, &info);'))
        # The presentation branch returns before the direct CPU map of dst.
        self.assertLess(update.index('if (resource->presentation'),
                        update.index('pDevice->pipe->texture_map(pDevice->pipe, dst, level,'))


if __name__ == '__main__':
    unittest.main()
