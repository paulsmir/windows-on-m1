"""Catch the EXP1506 stale INF/CAT receipt and refuse changed compiled binaries."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('repack', Path(__file__).resolve().parents[1] / 'scripts/agx-finalize-repack-receipt.py')
repack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(repack)


class RepackReceiptTests(unittest.TestCase):
    def test_final_hashes_preserve_raw_provenance_and_refuse_binary_change(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in repack.ARTIFACTS:
                (root / name).write_bytes(name.encode())
            (root / 'arm64-build.log').write_bytes(b'build')
            sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
            built = {name: sha(root / name) for name in repack.ARTIFACTS[:4]}
            raw = root / 'raw.json'
            raw.write_text(json.dumps({'ArtifactSha256': built, 'BuildLogSha256': sha(root / 'arm64-build.log'), 'SourceCommit': 'source'}))
            original = raw.read_bytes()
            (root / repack.ARTIFACTS[0]).write_bytes(b'signed INF')
            (root / repack.ARTIFACTS[3]).write_bytes(b'signed CAT')
            self.assertNotEqual(built[repack.ARTIFACTS[0]], sha(root / repack.ARTIFACTS[0]))
            output = root / 'final.json'
            repack.finalize(raw, root, output, 'reviewed repack')
            receipt = json.loads(output.read_text())
            self.assertEqual(receipt['Repackage']['BuiltArtifactSha256'], built)
            self.assertEqual(receipt['ArtifactSha256'], {name: sha(root / name) for name in repack.ARTIFACTS})
            self.assertEqual(raw.read_bytes(), original)
            self.assertEqual(receipt['SourceCommit'], 'source')
            with self.assertRaisesRegex(ValueError, 'already exists'):
                repack.finalize(raw, root, output, 'reviewed repack')
            (root / repack.UNCHANGED[0]).write_bytes(b'foreign binary')
            refused = root / 'refused.json'
            with self.assertRaisesRegex(ValueError, 'built binary changed'):
                repack.finalize(raw, root, refused, 'reviewed repack')
            self.assertFalse(refused.exists())
