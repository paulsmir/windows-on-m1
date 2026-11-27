# SPDX-License-Identifier: MIT
"""B20 stage 1 host side (proxyclient m1n1/rtc_anchor_host.py in the stage-1 kit's proxyclient copy):
- env unset: chainload's placeholder hook and HV's refresh are no-ops, so the built ADT is byte-identical;
- env set: three 8-byte zero placeholders appear in /chosen, and refresh copies the values m1n1 filled.
Inputs (skipped when absent): RTC_HOST_PROXYCLIENT (default: the kit copy) and RTC_HOST_ADT (a real J313 ADT)."""
import os
import sys
import unittest

PC = os.environ.get("RTC_HOST_PROXYCLIENT",
                    "/Volumes/pwdev/public_windows/.local/experiments/LCV2-pc-81c450e2-rtc1/proxyclient")
ADT = os.environ.get("RTC_HOST_ADT",
                     "/Volumes/pwdev/public_windows/.local/experiments/EXP3005-pathb5/evidence/pathb5/out/adt-BOOT.bin")


@unittest.skipUnless(os.path.isdir(PC) and os.path.isfile(ADT), "kit proxyclient or ADT fixture not present")
class RtcAnchorHostTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        sys.path.insert(0, PC)
        from m1n1.adt import load_adt
        from m1n1 import rtc_anchor_host
        cls.load_adt, cls.h = staticmethod(load_adt), rtc_anchor_host
        cls.blob = open(ADT, "rb").read()

    def test_env_unset_is_byte_identical(self):
        a = self.load_adt(self.blob)
        b = self.load_adt(self.blob)
        self.assertFalse(self.h.add_placeholders(b["chosen"], env={}))
        self.assertIsNone(self.h.refresh(b, a, env={}))
        self.assertEqual(a.build(), b.build())

    def test_env_set_adds_zero_placeholders_and_refreshes(self):
        env = {"M1N1_RTC_PLACEHOLDERS": "1"}
        live = self.load_adt(self.blob)
        self.assertTrue(self.h.add_placeholders(live["chosen"], env=env))
        rebuilt = self.load_adt(live.build())                     # survives a build/parse round trip, 8 bytes each
        for n in self.h.RTC_PROPS:
            self.assertEqual(int(rebuilt["chosen"]._properties[n]), 0)
        guest = self.load_adt(self.blob)
        filled = {"m1n1-rtc-utc-ms": 1795788732000, "m1n1-rtc-utc-cntpct": 0x1234567890, "m1n1-rtc-utc-cntfrq": 24000000}
        for n, v in filled.items():
            rebuilt["chosen"]._properties[n] = v
        self.assertEqual(self.h.refresh(guest, rebuilt, env=env), filled)
        out = self.load_adt(guest.build())
        for n, v in filled.items():
            self.assertEqual(int(out["chosen"]._properties[n]), v)

    def test_refresh_without_placeholders_leaves_guest(self):
        env = {"M1N1_RTC_PLACEHOLDERS": "1"}
        a, b = self.load_adt(self.blob), self.load_adt(self.blob)
        self.assertIsNone(self.h.refresh(b, a, env=env))
        self.assertEqual(a.build(), b.build())


if __name__ == "__main__":
    unittest.main()
