import math
import os
from pathlib import Path
import subprocess
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

    def test_capture_wrapper_records_each_ioctl_return_boundary(self):
        source = (ROOT / "tools/agx_capture_shim.py").read_text(encoding="utf-8")
        self.assertIn("capture-ioctl-begin", source)
        self.assertIn("capture-ioctl-end", source)
        self.assertIn("capture-ioctl-error", source)
        self.assertIn("super().ioctl(fd, request, p_arg)", source)

    def test_capture_wrapper_bootstraps_before_the_fake_render_fd_is_opened(self):
        source = (ROOT / "tools/agx_capture_shim.py").read_text(encoding="utf-8")
        constructor = source.index("def __init__(self, memfd):")
        historical = source.index("super().__init__(memfd)", constructor)
        expected = source.index(
            'os.environ.get("AGX_CAPTURE_PROGRAM")', historical
        )
        current = source.index('os.path.realpath("/proc/self/exe")', expected)
        guard = source.index("current_program !=", current)
        isolate = source.index("isolate_capture_subprocess_memory()", guard)
        setup_import = source.index(
            "from m1n1 import setup as capture_setup", isolate
        )
        setup_pin = source.index("self._capture_setup = capture_setup", setup_import)
        ioctl = source.index("def ioctl(self, fd, request, p_arg):", setup_pin)
        self.assertLess(constructor, historical)
        self.assertLess(historical, expected)
        self.assertLess(expected, current)
        self.assertLess(current, guard)
        self.assertLess(guard, isolate)
        self.assertLess(isolate, setup_import)
        self.assertLess(historical, setup_import)
        self.assertLess(setup_import, setup_pin)
        self.assertLess(setup_pin, ioctl)
        self.assertNotIn("self.init()", source[constructor:ioctl])

    def test_capture_subprocesses_cannot_mutate_parent_shim_fd_map(self):
        from tools.agx_capture_shim import isolate_capture_subprocess_memory

        with mock.patch.object(subprocess, "_USE_VFORK", True), mock.patch.object(
            subprocess, "_USE_POSIX_SPAWN", True
        ):
            isolate_capture_subprocess_memory()
            self.assertFalse(subprocess._USE_VFORK)
            self.assertFalse(subprocess._USE_POSIX_SPAWN)

    def test_capture_maps_historical_helper_field_without_changing_m1n1(self):
        from tools.agx_capture_shim import install_start3d_helper_cfg_compatibility

        class HistoricalStart3DStruct1:
            pass

        install_start3d_helper_cfg_compatibility(HistoricalStart3DStruct1)
        command = HistoricalStart3DStruct1()
        command.unk_40 = 0

        self.assertEqual(command.helper_cfg, 0)
        self.assertEqual(command.unk_40, 0)

    def test_capture_operator_selects_wrapper_and_explicit_budget(self):
        source = (ROOT / "tools/agx-capture-container/run-capture.sh").read_text(
            encoding="utf-8"
        )
        self.assertIn("AGX_SHIM_MODULE=tools.agx_capture_shim", source)
        self.assertIn("M1N1_BOOTSTRAP_TIMEOUT=3.0", source)

    def test_capture_producer_has_fixed_wall_clock_deadline(self):
        source = (ROOT / "scripts/capture-agx-clear-frame.sh").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "timeout --foreground --signal=TERM --kill-after=5s 30s", source
        )
        self.assertNotIn("CAPTURE_TIMEOUT_SECONDS", source)

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
