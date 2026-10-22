"""D3D11.1 output-merger logic ops (agx_wddm2_ddi.cpp logic_op).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? logic_ops[] maps
D3D11_1_DDI_LOGIC_OP (WDK order, formula in its comment) to enum
pipe_logicop, whose value is the truth table with bit (s << 1 | d). A
swapped entry draws wrong pixels silently. The cap is reported only with the
implementation.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/mesa/d3d11/agx_wddm2_ddi.cpp'
CAPS = ROOT / 'drivers/apple-agx/mesa/scripts/build-native-asahi-state.py'
HEADER = Path('/Volumes/pwdev/public_windows/.local/reference/wdk26100/d3d10umddi.h')
BLEND = Path('/Volumes/pwdev/public_windows/.local/reference/mesa/src/util/blend.h')


class Wddm2LogicOp(unittest.TestCase):
    def test_table_matches_the_wdk_truth_tables(self):
        if not HEADER.exists() or not BLEND.exists():
            self.skipTest('WDK or Mesa reference not available')
        wdk = HEADER.read_text(encoding='utf-8-sig', errors='replace')
        body = wdk[wdk.index('D3D11_1_DDI_LOGIC_OP_CLEAR = 0'):wdk.index('} D3D11_1_DDI_LOGIC_OP;')]
        formulas = re.findall(r'D3D11_1_DDI_LOGIC_OP_(\w+)(?: = 0)?,\s*// (.+)', body)
        self.assertEqual(len(formulas), 16)
        text = BLEND.read_text()
        pipe = re.findall(r'PIPE_LOGICOP_(\w+),', text[text.index('enum pipe_logicop {'):])[:16]
        source = SRC.read_text()
        table = source[source.index('logic_ops[16] = {'):]
        table = re.findall(r'PIPE_LOGICOP_(\w+)', table[:table.index('};')])
        self.assertEqual(len(table), 16)
        for (name, formula), mapped in zip(formulas, table):
            truth = 0
            for s_bit in (0, 1):
                for d_bit in (0, 1):
                    expr = formula.strip().replace('s', str(s_bit)).replace('d', str(d_bit))
                    truth |= (eval(expr) & 1) << (s_bit << 1 | d_bit)
            self.assertEqual(pipe.index(mapped), truth, f'{name} ({formula}) -> {mapped}')

    def test_cap_follows_the_implementation(self):
        source = SRC.read_text()
        self.assertIn('state.logicop_enable = 1;', source)
        self.assertNotIn('E_NOTIMPL, i);\n         return false;', source)
        self.assertIn('->OutputMergerLogicOp = TRUE;', CAPS.read_text())


if __name__ == '__main__':
    unittest.main()
