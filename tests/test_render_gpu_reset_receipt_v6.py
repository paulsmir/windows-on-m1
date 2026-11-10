"""Wom1GpuReset v6: the TDR-H0 halt observation (Wom1TdrHaltObserve).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
- The H0 verdict (acknowledged / not acknowledged / send failed) is read from
  these bytes only. A decoder field order that differs from the C struct, or
  a v6 tail read at the wrong offset, would report a wrong ack or a wrong N.
- v5 records from earlier experiments must keep decoding without an invented
  observation; a truncated or mislabelled record is rejected, never guessed.
- "Not sent" and "no change" are not values (0xffffffff sentinels).
"""
import struct
import unittest

from test_render_gpu_reset_receipt_v5 import RENDER, run_c
from test_wom1_gpu_fault_receipt import DECODER

PROGRAM = r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "render_gpu_fault.h"

int main(void) {
  ADMISSION_GPU_RESET_RECEIPT reset;
  ADMISSION_GPU_TDR_HALT_OBSERVE *o = &reset.HaltObserve;
  memset(&reset, 0, sizeof(reset));
  reset.Version = ADMISSION_GPU_RESET_RECEIPT_VERSION; reset.Bytes = sizeof(reset);
  reset.Entry = ADMISSION_GPU_RESET_ADAPTER;
  o->Observe = ADMISSION_TDR_HALT_OBSERVE_RAN;
  o->SkipReason = ADMISSION_TDR_HALT_SKIP_NONE;
  o->Endpoint = 0x21u; o->Sent = 2u; o->SendStatus = 0u;
  o->Acked = 1u; o->AckMs = 17u; o->Polls = 3u; o->FirstChangeMs = 16u;
  o->WorkerWaitMs = 4u;
  o->Before[0] = 2u; o->Before[1] = 0u; o->Before[2] = 0u; o->Before[3] = 0u;
  o->After[0] = 3u; o->After[1] = 1u; o->After[2] = 1u; o->After[3] = 0u;
  assert(fwrite(&reset, 1, sizeof(reset), stdout) == sizeof(reset));
  return 0;
}
'''


def blank(version, size):
    data = bytearray(size)
    struct.pack_into('<2I', data, 0, version, size)
    return data


class ReceiptV6(unittest.TestCase):
    def test_c_layout_decodes_the_observation(self):
        data = run_c(PROGRAM, includes=(RENDER / "include",))
        self.assertEqual(len(data), DECODER.RESET_V6)
        reset = DECODER.decode_reset(data)
        self.assertEqual(reset['version'], 6)
        self.assertEqual(reset['tdr_halt_observe'], {
            'observe': 'ran', 'skip_reason': 'none', 'worker_wait_ms': 4,
            'endpoint': '0x21', 'sent_n': 2, 'message': '0x0085000000000002',
            'send_status': 'ok', 'acked': True, 'ack_ms': 17, 'polls': 3,
            'first_change_ms': 16,
            'before': {'halt_count': 2, 'halted': 0, 'resume': 0},
            'after': {'halt_count': 3 | (1 << 32), 'halted': 1, 'resume': 0}})

    def test_v5_still_decodes_and_v6_sentinels_are_not_values(self):
        v5 = DECODER.decode_reset(blank(5, DECODER.RESET_V5))
        self.assertNotIn('tdr_halt_observe', v5)
        self.assertEqual(v5['failure_class'], 'none')
        data = blank(6, DECODER.RESET_V6)
        # Sent, but the send failed: no ack, no poll, no change time.
        DECODER.HALT_OBSERVE.pack_into(data, DECODER.RESET_V5, 1, 0, 0x21, 0, 2, 0, 0, 0,
                                       0xffffffff, 0, *([0] * 10))
        observe = DECODER.decode_reset(data)['tdr_halt_observe']
        self.assertEqual((observe['send_status'], observe['acked'], observe['first_change_ms']),
                         ('timeout', False, None))
        DECODER.HALT_OBSERVE.pack_into(data, DECODER.RESET_V5, 2, 4, 0x21, 0, 0xffffffff, 0,
                                       0, 0, 0xffffffff, 1000, *([0] * 10))
        self.assertEqual(DECODER.decode_reset(data)['tdr_halt_observe'],
                         {'observe': 'skipped', 'skip_reason': 'worker',
                          'worker_wait_ms': 1000})
        for length in (DECODER.RESET_V5, DECODER.RESET_V6 - 1):
            with self.assertRaises(ValueError):
                DECODER.decode_reset(data[:length])
        struct.pack_into('<I', data, 0, 5)
        with self.assertRaises(ValueError):
            DECODER.decode_reset(data)
        bad = blank(6, DECODER.RESET_V6)
        struct.pack_into('<I', bad, DECODER.RESET_V5, 3)  # undefined state
        with self.assertRaises(ValueError):
            DECODER.decode_reset(bad)


if __name__ == "__main__":
    unittest.main()
