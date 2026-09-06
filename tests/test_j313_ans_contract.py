"""Catch colliding resources and accidental native launch before qualification."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class AnsContractTests(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('ansgen', ROOT/'tools/generate_j313_ans_contract.py')
        self.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.module)
        self.cfg = json.loads((ROOT/'config/j313-ans.json').read_text())

    def test_default_is_recovery_and_native_is_gated(self):
        self.module.validate(self.cfg, ROOT)
        with self.assertRaisesRegex(ValueError, 'qualification'):
            self.module.validate(self.cfg, ROOT, for_launch=True)

    def test_reject_gpu_irq_collision(self):
        self.cfg['interrupt']['guest_gsiv'] = 880
        with self.assertRaisesRegex(ValueError, 'collision'):
            self.module.validate(self.cfg, ROOT)

    def test_reject_gpu_broker_overlap(self):
        self.cfg['broker']['guest_base'] = '0x300000000'
        with self.assertRaisesRegex(ValueError, 'collision'):
            self.module.validate(self.cfg, ROOT)

    def test_reject_recovery_irq_collision(self):
        self.cfg['interrupt']['guest_gsiv'] = 64
        with self.assertRaisesRegex(ValueError, 'collision'):
            self.module.validate(self.cfg, ROOT)

    def test_reject_input_mmio_collision(self):
        self.cfg['broker']['guest_base'] = '0x23510c000'
        with self.assertRaisesRegex(ValueError, 'collision'):
            self.module.validate(self.cfg, ROOT)

    def test_reject_gpu_physical_mmio_collision(self):
        self.cfg['broker']['guest_base'] = hex(0x204000000)
        with self.assertRaisesRegex(ValueError, 'collision'):
            self.module.validate(self.cfg, ROOT)

    def test_reject_ans_physical_mmio_collision(self):
        self.cfg['broker']['guest_base'] = '0x27bcc0000'
        with self.assertRaisesRegex(ValueError, 'collision'):
            self.module.validate(self.cfg, ROOT)

    def test_reject_nonhex_acpi_product_id(self):
        self.cfg['acpi_hid'] = 'APPLANS0'
        with self.assertRaisesRegex(ValueError, 'ACPI'):
            self.module.validate(self.cfg, ROOT)

    def test_reject_payload_bounce(self):
        self.cfg['dma']['payload_bounce'] = True
        with self.assertRaises(ValueError):
            self.module.validate(self.cfg, ROOT)

    def test_outputs_match_canonical_config(self):
        self.assertEqual([], self.module.generate(ROOT, check=True))

if __name__ == '__main__': unittest.main()
