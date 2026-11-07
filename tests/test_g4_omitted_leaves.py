"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Successful logical mappings with missing native leaves must have a bounded,
identity-bearing exact-range receipt; ordinary invalidations and CPU paging
must not be reported as GPU omissions. Decoder must not invent hole coverage.
"""
import importlib.util
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('fault_decoder', ROOT/'tools/decode_wom1_gpu_fault.py')
decoder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(decoder)

class OmittedLeaves(unittest.TestCase):
    def test_decode_exact_groups_ring_and_invalid_input(self):
        # v1 header: version, bytes, next, count, total events, total groups.
        header = struct.pack('<4I2Q', 1, 32 + 8*368, 1, 8, 9, 27)
        events = []
        for i in range(8):
            seq = 9 if i == 0 else i+1
            # 12 qwords + four dwords + 256-byte bitmap (2048 native groups).
            event = struct.pack('<12Q4I', seq, 88, 0xabc, 123, 0xdef,
                0x10000, 0x20000, 0x14000, 0xc000, 0x14000, 0x14000, 0x1c000,
                0, 2, 3, 1)
            events.append(event + bytes([5]) + bytes(255))
        result = decoder.decode_omitted_leaves(header+b''.join(events))
        self.assertEqual(result['total_events'], 9)
        self.assertEqual([e['sequence'] for e in result['events']], list(range(2,10)))
        self.assertEqual(result['events'][-1]['omitted_ranges'],
                         [{'start':'0x14000','end':'0x18000'}, {'start':'0x1c000','end':'0x20000'}])
        for bad in (b'', header, bytes(32+8*368), (header+b''.join(events))[:-1]):
            with self.assertRaises(ValueError): decoder.decode_omitted_leaves(bad)

    def test_deferred_flushed_wiring(self):
        text=(ROOT/'drivers/apple-agx/render-admission/src/receipts.c').read_text()
        self.assertIn('AdmissionPagingReceiptOmittedLeaves', text)
        self.assertRegex(text, r'L"Wom1G4OmittedLeaves",\s*&omitted,\s*sizeof\(omitted\), TRUE')
        paging=(ROOT/'drivers/apple-agx/render-admission/src/gpuva_g3_paging_windows.c').read_text()
        self.assertNotIn('ZwFlushKey',paging)

    def test_recorder_is_only_compiled_with_paging_worker(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A recorder outside the G3 build guard references an absent paging worker in non-G3 driver profiles."""
        text=(ROOT/'drivers/apple-agx/render-admission/src/receipts.c').read_text()
        stack=[]
        for line in text.splitlines():
            if line.startswith('#if'): stack.append(line)
            elif line.startswith('#endif'): stack.pop()
            if line.startswith('void AdmissionRecordG4OmittedLeaves('):
                self.assertIn('#if defined(APPLE_AGX_GPUVA_G3_QUALIFICATION)',stack)
                break
        else: self.fail('receipt recorder missing')
