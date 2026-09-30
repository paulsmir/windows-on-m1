"""Real KMD receipts must distinguish root, missing VA and incomplete tail."""
import json
import importlib.util
import os
from pathlib import Path
import subprocess
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class QueryReceiptTests(unittest.TestCase):
    def test_decoder_rejects_truncation_versions_and_unknown_reason(self):
        spec = importlib.util.spec_from_file_location('query_decoder',
            ROOT/'tools/decode_g3_copy_query_failure.py')
        decoder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(decoder)
        old = struct.pack('<4I', 1, 16, 53, 0xc000000d)
        self.assertFalse(decoder.decode(old)['graph_details_available'])
        valid = struct.pack('<12I12Q', 2, 144, 53, 0xc000000d,
                            63, 0, 2, 8, 5, 3, 2, 1, *([0]*12))
        self.assertEqual(decoder.decode(valid)['version'], 2)
        current = struct.pack('<12I15Q', 3, 168, 53, 0xc000000d,
                              223, 0, 2, 8, 3, 3, 2, 1, *([0]*12),
                              0x81234071, 0xabcdef00, 65536)
        decoded = decoder.decode(current)
        self.assertTrue(decoded['resident_group_available'])
        self.assertTrue(decoded['canonical_allocation_available'])
        self.assertEqual(decoded['request_allocation_handle'], '0x81234071')
        self.assertEqual(decoded['canonical_allocation_identity'], '0xabcdef00')
        self.assertEqual(decoded['canonical_allocation_bytes'], 65536)
        for bad in (valid[:-1], valid+b'\0', struct.pack('<I', 3)+valid[4:],
                    current[:-1], current+b'\0',
                    current[:16]+struct.pack('<I', 256)+current[20:],
                    valid[:16]+struct.pack('<I', 128)+valid[20:],
                    valid[:32]+struct.pack('<I', 99)+valid[36:]):
            with self.assertRaises(ValueError):
                decoder.decode(bad)
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)/'devnode.reg'
            value = ','.join(f'{x:02x}' for x in valid)
            p.write_text('Windows Registry Editor Version 5.00\n\n"Wom1G3CopyQueryFailure"=hex:'+
                         value[:48]+'\\\n  '+value[48:]+'\n', encoding='utf-16')
            self.assertEqual(decoder.read_receipt(p), valid)

    def test_historical_graph_rejections_decode_distinctly_in_both_page_profiles(self):
        for profile in ('16', '64'):
            with self.subTest(profile=profile), tempfile.TemporaryDirectory() as tmp:
                run = subprocess.run([sys.executable, str(ROOT/'tests/g3_vidmm_replay.py'),
                    '--function-revision',
                    'AdmissionGpuvaG3CopyEscape=f3d8a2eee833425ff369400b1b5ad2ec02997694'],
                    env=dict(os.environ, G3_REPLAY_R145='1', G3_REPLAY_QUERY_V2='1',
                             G3_REPLAY_PROFILE=profile, G3_QUERY_OUTPUT=tmp),
                    text=True, capture_output=True, cwd=ROOT)
                self.assertEqual(run.returncode, 0, run.stdout+run.stderr)
                decoded = {}
                for name in ('bootstrap-root', 'absent-va', 'absent-shadow',
                             'unpublished-tail', 'leaf-not-published', 'early-request'):
                    path = Path(tmp)/(name+'.bin')
                    result = subprocess.run([sys.executable,
                        str(ROOT/'tools/decode_g3_copy_query_failure.py'), str(path)],
                        text=True, capture_output=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    decoded[name] = json.loads(result.stdout)
                self.assertTrue(decoded['bootstrap-root']['root_is_bootstrap'])
                self.assertEqual(decoded['bootstrap-root']['missing_reason'], 'no-root')
                self.assertEqual(decoded['absent-va']['missing_reason'], 'leaf-absent')
                self.assertEqual(decoded['absent-va']['missing_va'], '0x20000')
                self.assertTrue(decoded['absent-va']['resident_group_available'])
                self.assertFalse(decoded['absent-shadow']['resident_group_available'])
                self.assertEqual(decoded['absent-shadow']['missing_reason'], 'leaf-absent')
                self.assertEqual(decoded['unpublished-tail']['missing_reason'], 'tail-short')
                self.assertEqual(decoded['unpublished-tail']['component_reason'], 'leaf-absent')
                self.assertEqual(decoded['leaf-not-published']['missing_reason'], 'leaf-not-published')
                self.assertTrue(decoded['leaf-not-published']['resident_group_available'])
                self.assertEqual(decoded['unpublished-tail']['query_bytes'], 65552)
                self.assertEqual(decoded['unpublished-tail']['process_set_root_count'], 2)
                self.assertEqual(decoded['unpublished-tail']['context_set_root_count'], 1)
                self.assertNotEqual(decoded['absent-va']['process_last_set_root_ipa'], '0x0')
                early = decoded.pop('early-request')
                self.assertEqual(early['predicate'], 22)
                self.assertEqual(early['request_allocation_handle'], '0x81234072')
                self.assertFalse(early['canonical_allocation_available'])
                self.assertEqual(early['canonical_allocation_identity'], '0x0')
                self.assertEqual(early['canonical_allocation_bytes'], 0)
                for item in decoded.values():
                    self.assertEqual(item['version'], 3)
                    self.assertEqual(item['request_allocation_handle'], '0x81234071')
                    self.assertTrue(item['canonical_allocation_available'])
                    self.assertNotEqual(item['canonical_allocation_identity'], '0x0')
                    self.assertEqual(item['canonical_allocation_bytes'],
                                     65552 if item['missing_reason']=='tail-short' else 65536)
                    self.assertEqual(item['predicate'], 53)
                    self.assertEqual(item['status'], '0xc000000d')
                    self.assertTrue(item['captured_under_lock'])
                    self.assertFalse(item['write'])
