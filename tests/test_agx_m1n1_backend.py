from contextlib import contextmanager
from dataclasses import replace
from pathlib import Path
import unittest

from tools.agx_contract import load_contract


ROOT = Path(__file__).resolve().parents[1]
CONTRACT_PATH = ROOT / "config" / "j313-agx.json"


class FakeProxy:
    def __init__(self, calls):
        self.calls = calls

    def pmgr_adt_clocks_enable(self, path):
        self.calls.append(("clock", path))


class FakeU:
    def __init__(self, calls):
        self.proxy = FakeProxy(calls)


class FakeMgmt:
    def __init__(self, calls, *, respond=True):
        self.calls = calls
        self.respond = respond
        self.pending_ping = False
        self.iop_power_state = 0x20
        self.ap_power_state = 0x20
        self.msghandler = {4: self._pong}

    def _pong(self, _message):
        return True

    def ping(self):
        self.calls.append("ping")
        self.pending_ping = True


class FakeAsc:
    def __init__(self, calls, *, respond=True):
        self.calls = calls
        self.mgmt = FakeMgmt(calls, respond=respond)
        self.running = False

    def work(self):
        self.calls.append("work")
        if self.mgmt.pending_ping and self.mgmt.respond:
            self.mgmt.pending_ping = False
            self.mgmt.msghandler[4](object())

    def is_running(self):
        return self.running


class FakeFaultRegister:
    value = 0x1234
    FAULTED = 0


class FakeFaultInfo:
    @property
    def reg(self):
        return FakeFaultRegister()


class FakeSgx:
    FAULT_INFO = FakeFaultInfo()


class FakeUat:
    def __init__(self, calls):
        self.calls = calls
        self.initialized = True
        self.pt_cache = {0x1000: [1], 0x2000: [2]}
        self.dirty = set()
        self.dirty_ranges = {0: [(0x4000, 0x4000)]}
        self.handoff = self

    @contextmanager
    def lock(self):
        self.calls.append("uat-lock")
        yield

    def set_l0(self, context, offset, base):
        self.calls.append(("set-l0", context, offset, base))

    def flush_dirty(self):
        self.calls.append("flush-dirty")

    def invalidate_cache(self):
        self.calls.append("invalidate-cache")
        self.pt_cache = {}


class FakeEventManager:
    event_count = 7


class FakeAgx:
    def __init__(self, calls, *, respond=True):
        self.calls = calls
        self.asc = FakeAsc(calls, respond=respond)
        self.uat = FakeUat(calls)
        self.event_mgr = FakeEventManager()
        self.sgx = FakeSgx()

    def start(self):
        self.calls.append("agx-start")
        self.asc.running = True

    def stop(self):
        self.calls.append("agx-stop")
        self.asc.running = False
        self.asc.mgmt.ap_power_state = 0x10
        self.asc.mgmt.iop_power_state = 0x10

    def get_irqs(self):
        self.calls.append("get-irqs")
        return [0, 1, 0, 0]


class M1n1AgxBackendTests(unittest.TestCase):
    def setUp(self):
        self.contract = load_contract(CONTRACT_PATH)
        self.calls = []
        self.u = FakeU(self.calls)

    def backend(self, *, live_contract=None, respond=True):
        from tools.agx_m1n1_backend import M1n1AgxBackend

        live_contract = live_contract or self.contract

        def read_live(_contract):
            self.calls.append("inventory")
            return live_contract

        def create_agx(_u):
            self.calls.append("agx-constructor")
            return FakeAgx(self.calls, respond=respond)

        return M1n1AgxBackend(
            self.u,
            live_contract_reader=read_live,
            agx_factory=create_agx,
            heartbeat_attempts=3,
        )

    def test_prepare_validates_every_resource_before_clocks(self):
        backend = self.backend()
        backend.prepare(self.contract)

        self.assertEqual(
            self.calls[:4],
            [
                "inventory",
                ("clock", "/arm-io/gfx-asc"),
                ("clock", "/arm-io/sgx"),
                "agx-constructor",
            ],
        )
        self.assertFalse(backend.released())

    def test_prepare_accepts_live_node_enumeration_order(self):
        live_contract = replace(
            self.contract,
            nodes=tuple(reversed(self.contract.nodes)),
        )
        backend = self.backend(live_contract=live_contract)

        backend.prepare(self.contract)

        self.assertEqual(self.calls[0], "inventory")
        self.assertIn(("clock", "/arm-io/gfx-asc"), self.calls)
        self.assertFalse(backend.released())

    def test_mismatch_refuses_before_clocks_or_constructor(self):
        from tools.agx_m1n1_backend import BackendError

        mismatched = replace(self.contract, interrupts=(999,))
        backend = self.backend(live_contract=mismatched)
        with self.assertRaisesRegex(BackendError, "live AGX resources"):
            backend.prepare(self.contract)
        self.assertEqual(self.calls, ["inventory"])
        self.assertTrue(backend.released())

    def test_clock_enable_failure_cannot_be_reported_released(self):
        from tools.agx_m1n1_backend import BackendError

        backend = self.backend()

        def fail_clock(path):
            self.calls.append(("clock", path))
            raise RuntimeError("clock transport failed")

        self.u.proxy.pmgr_adt_clocks_enable = fail_clock
        with self.assertRaisesRegex(RuntimeError, "clock transport failed"):
            backend.prepare(self.contract)
        self.assertFalse(backend.released())
        with self.assertRaisesRegex(BackendError, "clocks enabled"):
            backend.reset()
        self.assertFalse(backend.released())

    def test_heartbeat_requires_management_progress(self):
        from tools.agx_m1n1_backend import BackendError

        backend = self.backend(respond=False)
        backend.prepare(self.contract)
        backend.start()
        with self.assertRaisesRegex(BackendError, "management progress"):
            backend.heartbeat()

    def test_heartbeat_reports_management_and_event_progress(self):
        backend = self.backend()
        backend.prepare(self.contract)
        backend.start()
        heartbeat = backend.heartbeat()

        self.assertEqual(heartbeat["management_pongs"], 1)
        self.assertEqual(heartbeat["event_count"], 7)
        self.assertTrue(heartbeat["progress"])

    def test_snapshot_contains_required_diagnostic_classes(self):
        backend = self.backend()
        backend.prepare(self.contract)
        backend.start()
        snapshot = backend.snapshot("test")

        self.assertEqual(snapshot["reason"], "test")
        self.assertIn("firmware", snapshot)
        self.assertIn("sgx_irqs", snapshot)
        self.assertIn("fault", snapshot)
        self.assertIn("uat_mappings", snapshot)
        self.assertEqual(snapshot["fault"]["raw"], 0x1234)

    def test_reset_invalidates_private_roots_before_release(self):
        backend = self.backend()
        backend.prepare(self.contract)
        backend.start()
        backend.stop()
        backend.reset()

        first_root = self.calls.index(("set-l0", 0, 0, 0))
        second_root = self.calls.index(("set-l0", 0, 1, 0))
        flush = self.calls.index("flush-dirty")
        invalidate = self.calls.index("invalidate-cache")
        self.assertLess(first_root, second_root)
        self.assertLess(second_root, flush)
        self.assertLess(flush, invalidate)
        self.assertTrue(backend.released())

    def test_adapter_does_not_import_or_submit_render_work(self):
        source = (ROOT / "tools" / "agx_m1n1_backend.py").read_text()
        for forbidden in (
            "m1n1.agx.render",
            "GPUContext",
            "GPUWorkQueue",
            "submit(",
        ):
            self.assertNotIn(forbidden, source)


if __name__ == "__main__":
    unittest.main()
