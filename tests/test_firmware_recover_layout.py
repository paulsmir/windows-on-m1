"""EXP1075: the KMD resumes a halted AGX firmware at m1n1's FW status layout.

EXP1075 kernel dump: after a firmware Timeout event the FW status read
halt_count=1, halted=1, resume=0, and nothing resumed it (TDR, failed reset,
bugcheck 0x116). AdmissionTransportRecover implements Asahi recover(): wait for
halted, write halted=0 and resume=1. The offsets it uses must be the ones of
m1n1's InitData_FWStatus (ChannelInfo, then 32-bit fields each followed by
12 bytes of padding), the layout the firmware was initialised with.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
INITDATA = ROOT / "m1n1_windows/proxyclient/m1n1/fw/agx/initdata.py"
CHANNELS = ROOT / "m1n1_windows/proxyclient/m1n1/fw/agx/channels.py"
KMD = ROOT / "drivers/apple-agx/render-admission/src/backend_platform_windows.c"
SIZES = {"Int32ul": 4, "Int64ul": 8}


def struct_offsets(text, cls, nested):
    body = text.split("class %s(ConstructClass):" % cls, 1)[1].split("def __init__", 1)[0]
    offsets, at = {}, 0
    for line in body.splitlines():
        field = re.match(r'\s*"(\w+)"\s*/\s*(?:Hex\()?(\w+)', line)
        pad = re.match(r"\s*ZPadding\((0x[0-9a-f]+|\d+)\)", line)
        if field:
            offsets[field.group(1)] = at
            kind = field.group(2)
            at += SIZES[kind] if kind in SIZES else nested[kind]
        elif pad:
            at += int(pad.group(1), 0)
    return offsets, at


class FirmwareRecoverLayout(unittest.TestCase):
    def test_kmd_offsets_match_m1n1_fw_status(self):
        channel, channel_size = struct_offsets(CHANNELS.read_text(), "ChannelInfo", {})
        self.assertEqual(channel_size, 0x10)
        status, size = struct_offsets(INITDATA.read_text(), "InitData_FWStatus",
                                      {"ChannelInfo": channel_size})
        self.assertEqual(size, 0x80)
        kmd = KMD.read_text()
        recover = kmd.split("static APPLE_AGX_BACKEND_BOOL AdmissionTransportRecover(", 1)[1]
        recover = recover.split("\n}\n", 1)[0]
        self.assertIn("status[0x%xu / 4u] == 0u" % status["halted"], recover)
        self.assertIn("status[0x%xu / 4u] = 0u;" % status["halted"], recover)
        self.assertIn("status[0x%xu / 4u] = 1u;" % status["resume"], recover)
        # halted is cleared before resume is raised, as in Asahi recover().
        self.assertLess(recover.index("status[0x%xu / 4u] = 0u;" % status["halted"]),
                        recover.index("status[0x%xu / 4u] = 1u;" % status["resume"]))
        self.assertIn("runtime->QueueIo.Recover = AdmissionTransportRecover;", kmd)


if __name__ == "__main__":
    unittest.main()
