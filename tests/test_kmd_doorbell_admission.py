"""EXP1212: the KMD transport rings every doorbell the platform binds.

What it catches: EXP1212 bugchecked (0x116) because AdmissionTransportDoorbell
admitted only the TA and 3D pipe doorbells. The compute RunCompute was
written and its ring pointer published, the compute doorbell (6) was
refused, the visible failure stopped RTKit, and the TDR reset failed. Every
APPLE_AGX_PLATFORM_*_DOORBELL the provider binds to a channel must be
admitted by the KMD transport, and the values must follow Asahi's
pipe type | priority << 2 for the group-1 channels.
"""
from pathlib import Path
import os
import re
import unittest

ROOT = Path(os.environ.get("APPLE_AGX_WIRING_ROOT", Path(__file__).resolve().parents[1]))
HEADER = ROOT / "drivers/apple-agx/shared/include/apple_agx_platform_provider.h"
PROVIDER = ROOT / "drivers/apple-agx/shared/src/apple_agx_platform_provider.c"
KMD = ROOT / "drivers/apple-agx/render-admission/src/backend_platform_windows.c"


def function_body(text, name):
    start = text.index(name + "(")
    start = text.index("{", start)
    depth, pos = 1, start + 1
    while depth:
        depth += {"{": 1, "}": -1}.get(text[pos], 0)
        pos += 1
    return text[start:pos]


class KmdDoorbellAdmission(unittest.TestCase):
    def test_every_bound_doorbell_is_admitted(self):
        doorbells = dict(re.findall(r"#define (APPLE_AGX_PLATFORM_\w+_DOORBELL) (\d+)u", HEADER.read_text()))
        bound = set(re.findall(r"Doorbell\s*=\s*(APPLE_AGX_PLATFORM_\w+_DOORBELL)", PROVIDER.read_text()))
        self.assertEqual(set(doorbells), bound)
        body = function_body(KMD.read_text(), "static APPLE_AGX_BACKEND_BOOL AdmissionTransportDoorbell")
        missing = [name for name in sorted(bound) if name not in body]
        self.assertEqual([], missing)

    def test_pipe_doorbells_follow_asahi_encoding(self):
        values = dict(re.findall(r"#define APPLE_AGX_PLATFORM_(\w+)_DOORBELL (\d+)u", HEADER.read_text()))
        # Asahi: MSG_TX_DOORBELL | pipe_type | (priority << 2); vertex 0,
        # fragment 1, compute 2; this driver's channels are priority 1.
        self.assertEqual({"TA": 4, "D3": 5, "COMPUTE": 6}, {k: int(v) for k, v in values.items()})


if __name__ == "__main__":
    unittest.main()
