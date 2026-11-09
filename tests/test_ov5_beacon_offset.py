"""EXP2290: the OV5 waiver beacon was placed at broker offset 0xE00, the pool
window ABI; every pool register was swallowed and GravityMark ran out of memory.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The beacon range must not overlap another register block of the broker page.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KMD = (ROOT / 'drivers/apple-agx/render-admission/src/backend_platform_windows.c').read_text()
POOL = (ROOT / 'drivers/apple-agx/shared/include/apple_agx_pool_window_abi.h').read_text()


class BeaconOffset(unittest.TestCase):
    def test_beacon_is_outside_the_pool_window(self):
        beacon = int(re.search(r'BrokerBase \+ (0x[0-9A-Fa-f]+)u \+ i \* sizeof\(ULONG\)', KMD).group(1), 16)
        pool = int(re.search(r'AGX_POOL_WINDOW_OFFSET (0x[0-9a-f]+)u', POOL).group(1), 16)
        pool_bytes = int(re.search(r'AGX_POOL_WINDOW_BYTES (0x[0-9a-f]+)u', POOL).group(1), 16)
        self.assertTrue(beacon + 48 <= pool or beacon >= pool + pool_bytes)
        self.assertLessEqual(beacon + 48, 0x1000)


if __name__ == '__main__':
    unittest.main()
