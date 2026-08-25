import math
import os
from pathlib import Path
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]


class FakeDevice:
    def __init__(self):
        self.timeout = 3
        self.baudrate = 115200


class FakeInterface:
    def __init__(self):
        self.dev = FakeDevice()
        self.nop_timeouts = []

    def nop(self):
        self.nop_timeouts.append(self.dev.timeout)


class FakeProxy:
    def __init__(self):
        self.whoami_timeouts = []

    def iodev_whoami(self):
        from m1n1.proxy import IODEV

        self.whoami_timeouts.append(self.interface.dev.timeout)
        return IODEV.USB0


class CaptureBootstrapTests(unittest.TestCase):
    def setUp(self):
        import sys

        sys.path.insert(0, str(ROOT / "m1n1_windows" / "proxyclient"))

    def test_explicit_budget_covers_first_request_and_is_restored(self):
        from tools.agx_capture_bootstrap import bootstrap_port

        interface = FakeInterface()
        proxy = FakeProxy()
        proxy.interface = interface

        bootstrap_port(interface, proxy, initial_timeout=3.0)

        self.assertEqual(proxy.whoami_timeouts, [3.0])
        self.assertEqual(interface.nop_timeouts, [3.0])
        self.assertEqual(interface.dev.timeout, 3)

    def test_budget_is_restored_when_final_nop_fails(self):
        from m1n1.proxy import UartTimeout
        from tools.agx_capture_bootstrap import bootstrap_port

        interface = FakeInterface()
        interface.nop = mock.Mock(side_effect=UartTimeout("late"))
        proxy = FakeProxy()
        proxy.interface = interface

        with self.assertRaises(UartTimeout):
            bootstrap_port(interface, proxy, initial_timeout=2.5)

        self.assertEqual(interface.dev.timeout, 3)

    def test_environment_budget_is_bounded_and_finite(self):
        from tools.agx_capture_bootstrap import timeout_from_environment

        with mock.patch.dict(os.environ, {"M1N1_BOOTSTRAP_TIMEOUT": "3.0"}):
            self.assertEqual(timeout_from_environment(), 3.0)
        for value in ("0.149", "10.1", "nan", "inf", "not-a-number"):
            with self.subTest(value=value), mock.patch.dict(
                os.environ, {"M1N1_BOOTSTRAP_TIMEOUT": value}
            ):
                with self.assertRaises(ValueError):
                    timeout_from_environment()

    def test_capture_wrapper_patches_before_importing_historical_shim(self):
        source = (ROOT / "tools/agx_capture_shim.py").read_text(encoding="utf-8")
        install = source.index("install_bootstrap_override()")
        historical = source.index("from m1n1.agx.shim import Shim as HistoricalShim")
        self.assertLess(install, historical)

    def test_capture_wrapper_sets_live_firmware_layout_before_agx_start(self):
        source = (ROOT / "tools/agx_capture_shim.py").read_text(encoding="utf-8")
        set_version = source.index("Ver.set_version(u)")
        historical_start = source.index("super().init_agx()")
        self.assertLess(set_version, historical_start)
        self.assertIn('Ver.check("V == V13_5 && G == G13")', source)

    def test_capture_operator_selects_wrapper_and_explicit_budget(self):
        source = (ROOT / "tools/agx-capture-container/run-capture.sh").read_text(
            encoding="utf-8"
        )
        self.assertIn("AGX_SHIM_MODULE=tools.agx_capture_shim", source)
        self.assertIn("M1N1_BOOTSTRAP_TIMEOUT=3.0", source)

    def test_full_client_probe_reproduces_loader_path_without_agx(self):
        helper = (ROOT / "tools/agx-capture-container/probe-full-client-bootstrap.sh").read_text(
            encoding="utf-8"
        )
        self.assertIn("LD_PRELOAD=\"$SHIM\"", helper)
        self.assertIn("AGX_SHIM_MODULE=tools.agx_capture_bootstrap_probe", helper)
        self.assertIn("M1N1_BOOTSTRAP_TIMEOUT=3.0", helper)
        self.assertIn("verify-agx-capture-env.py", helper)
        self.assertIn("reboot.py", helper)
        self.assertIn("transport-receipt.json", helper)
        self.assertNotIn("m1n1.agx", helper)
        self.assertNotIn("pmgr_adt_clocks_enable", helper)


if __name__ == "__main__":
    unittest.main()
