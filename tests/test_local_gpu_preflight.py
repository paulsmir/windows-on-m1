import importlib.util
import io
import json
import unittest
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('proposal_client', Path(__file__).resolve().parents[1] / 'scripts/agent/local_proposal_client.py')
client = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client)


class GpuPreflightTests(unittest.TestCase):
    def record(self, **overrides):
        model = {'name': client.MODEL, 'size': 1000, 'size_vram':1000, 'context_length':4096}
        model.update(overrides)
        with patch.object(client.urllib.request, 'urlopen', return_value=io.BytesIO(json.dumps({'models':[model]}).encode())):
            return client.gpu_record()

    def test_full_gpu_accepts_exact_model_and_context(self):
        self.assertEqual(self.record()['gpu_residency_ratio'], 1)

    def test_cpu_and_material_fallback_rejected(self):
        for vram in (0, 500, 949):
            with self.subTest(vram=vram), self.assertRaises(ValueError):
                self.record(size_vram=vram)

    def test_wrong_model_context_or_missing_size_rejected(self):
        for overrides in ({'name':'other'}, {'context_length':16384}, {'size':0}):
            with self.subTest(overrides=overrides), self.assertRaises(ValueError):
                self.record(**overrides)

if __name__ == '__main__':
    unittest.main()
