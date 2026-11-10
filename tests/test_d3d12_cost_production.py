"""The cost fixture's robustness filter is the D3D12 owner's.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
tests/dxil_agx_cost_fixture.c left SSBO atomics unchecked although the D3D12
owner's context lowers them: agx_win32_should_lower_robustness_d3d excludes
only load_ubo, load_ssbo and store_ssbo (agx_win32_lower_d3d_buffer_access
owns those) and otherwise answers as Gallium's should_lower_robustness for a
robust context. Offline costs of every kernel with a UAV atomic were too low
(GravityMark a8260e4297efffb3 with its root-view masks and soft-fault loads:
793 instructions offline, 801 in the guest's measure-d3d12-shader-stats row;
97ae4b027aa43c6c: 1102 against 1118). The invariant: the fixture's filter
lowers exactly the intrinsics the owner's filter lowers.
"""
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_dxbc_to_nir import local_tree  # noqa: E402

BATCH = ROOT / 'drivers/apple-agx/mesa/scripts/native-asahi-batch-lifecycle.py'
FIXTURE = ROOT / 'tests/dxil_agx_cost_fixture.c'


def cases(text, start, end):
    """{intrinsic: the first return that follows its case label} of text[start:end]"""
    body = text[text.index(start):]
    body = body[:body.index(end)]
    result, pending = {}, []
    for label, value in re.findall(r'case nir_intrinsic_(\w+):|return ([^;]+);', body):
        if label:
            pending.append(label)
        else:
            result.update(dict.fromkeys(pending, value.strip()))
            pending = []
    return result


class RobustnessFilter(unittest.TestCase):
    def test_fixture_filter_is_the_d3d_owner_filter(self):
        mesa = local_tree('reference', 'mesa')
        if not mesa:
            self.skipTest('pinned Mesa sources not available')
        gallium = cases((mesa / 'src/gallium/drivers/asahi/agx_state.c').read_text(),
                        '\nshould_lower_robustness(', '\n}\n')
        d3d = cases(BATCH.read_text(), '\nagx_win32_should_lower_robustness_d3d(', '\n}')
        fixture = cases(FIXTURE.read_text(), '\ngl_part_of_d3d_robustness(', '\n}\n')
        self.assertEqual(set(d3d.values()), {'false'})
        self.assertEqual(set(d3d), {'load_ubo', 'load_ssbo', 'store_ssbo'})
        expected = {k: v for k, v in gallium.items() if k not in d3d}
        # the context's robustness: *gl_robust there, the fixture's d3d_robust flag here
        self.assertEqual({k: v.replace('*gl_robust', '*(const bool *)data') for k, v in expected.items()}, fixture)
        self.assertIn('ssbo_atomic', fixture)
        self.assertIn('NIR_PASS(_, nir, nir_lower_robust_access, gl_part_of_d3d_robustness, &d3d_robust);',
                      FIXTURE.read_text())


if __name__ == '__main__':
    unittest.main()
