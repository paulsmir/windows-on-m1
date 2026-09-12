"""Finite model and unchanged production UAT implementation, host execution only."""
from pathlib import Path
import os
import hashlib
import json
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
class GpuvaSemanticModelTests(unittest.TestCase):
    def test_finite_model(self):
        with tempfile.TemporaryDirectory(prefix='ad04-semantic-') as tmp:
            binary = Path(tmp) / 'model'
            shared = ROOT / 'drivers/apple-agx/shared'
            command = [os.environ.get('CC','clang'), '-std=c11', '-Wall', '-Wextra',
                '-Werror', '-fsanitize=address,undefined', '-I', str(shared/'include'),
                str(ROOT/'tests/models/gpuva_semantic_model_test.c'),
                str(ROOT/'tests/models/gpuva_semantic_model.c'),
                str(shared/'src/apple_agx_uat.c'), str(shared/'src/apple_agx_uat_table.c'),
                '-o', str(binary)]
            subprocess.run(command, check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            observed = json.loads(result.stdout)
            self.assertEqual(observed['harness'], 'PASS')
            self.assertEqual(observed['arbitrary_4k_groups'], 'COUNTEREXAMPLE')
            self.assertFalse(observed['wddm_input_domain_proven'])
            print(result.stdout.strip())
            if os.environ.get('AD04_MODEL_EVIDENCE'):
                target = Path(os.environ['AD04_MODEL_EVIDENCE'])
                target.mkdir(parents=True, exist_ok=False)
                (target/'stdout.json').write_text(result.stdout)
                (target/'stderr.log').write_text(result.stderr)
                inputs = [Path(arg) for arg in command if arg.endswith('.c')]
                inputs.append(ROOT/'tests/models/gpuva_semantic_model.h')
                report = {'command': command, 'exit': result.returncode,
                          'compiler': subprocess.check_output([command[0],'--version'], text=True).splitlines()[0],
                          'source_sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},
                          'assumptions': ['synthetic owned backing', 'acknowledged invalidation event',
                              'abstract external residency grant', 'paging process plus two users one binding each one slot',
                              'not a WDDM caller trace or hardware proof']}
                (target/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
