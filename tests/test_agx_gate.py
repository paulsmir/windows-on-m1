import json
from pathlib import Path
import sys
import tempfile
import unittest

from tools.agx_contract import load_contract


ROOT = Path(__file__).resolve().parents[1]
CONTRACT = ROOT / "config" / "j313-agx.json"


class FakeBackend:
    def __init__(self, *, stall_heartbeat=False, fail_release=False):
        self.calls = []
        self.now = 0.0
        self.stall_heartbeat = stall_heartbeat
        self.fail_release = fail_release
        self.owned = False

    def clock(self):
        return self.now

    def prepare(self, contract):
        self.calls.append("prepare")
        self.contract = contract

    def start(self):
        self.calls.append("start")
        self.owned = True

    def heartbeat(self):
        self.calls.append("heartbeat")
        self.now += 2.0 if self.stall_heartbeat else 0.1
        return {"alive": True, "sequence": self.calls.count("heartbeat")}

    def snapshot(self, reason):
        self.calls.append("snapshot")
        return {"reason": reason, "owned": self.owned}

    def stop(self):
        self.calls.append("stop")

    def reset(self):
        self.calls.append("reset")
        self.owned = self.fail_release

    def released(self):
        self.calls.append("released")
        return not self.owned


class AgxGateTests(unittest.TestCase):
    def test_cli_exposes_the_bundled_m1n1_proxyclient(self):
        import tools.agx_gate  # noqa: F401

        proxyclient = str(ROOT / "m1n1_windows" / "proxyclient")
        self.assertIn(proxyclient, sys.path)

    def setUp(self):
        self.contract = load_contract(CONTRACT)
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_ten_cycles_release_ownership(self):
        from tools.agx_gate import run_gate

        backend = FakeBackend()
        result = run_gate(
            backend,
            self.contract,
            cycles=10,
            timeout_s=1,
            evidence_dir=self.path,
            clock=backend.clock,
        )

        self.assertEqual(result.completed_cycles, 10)
        self.assertTrue(result.windows_launch_permitted)
        self.assertTrue(backend.released())
        self.assertEqual(backend.calls.count("start"), 10)
        self.assertEqual(backend.calls.count("reset"), 10)
        manifest = json.loads((self.path / "gate-result.json").read_text())
        self.assertEqual(manifest["verdict"], "passed")
        self.assertEqual(len(manifest["cycles"]), 10)
        self.assertTrue(manifest["windows_launch_permitted"])

    def test_heartbeat_timeout_saves_snapshot_and_fails_closed(self):
        from tools.agx_gate import GateError, run_gate

        backend = FakeBackend(stall_heartbeat=True)
        with self.assertRaisesRegex(GateError, "heartbeat deadline"):
            run_gate(
                backend,
                self.contract,
                cycles=10,
                timeout_s=1,
                evidence_dir=self.path,
                clock=backend.clock,
            )

        manifest = json.loads((self.path / "gate-result.json").read_text())
        self.assertEqual(manifest["verdict"], "failed")
        self.assertIn("snapshot", manifest["cycles"][0])
        self.assertFalse(manifest["windows_launch_permitted"])
        self.assertEqual(backend.calls.count("stop"), 1)
        self.assertEqual(backend.calls.count("reset"), 1)
        self.assertTrue(backend.released())

    def test_unreleased_backend_fails_closed(self):
        from tools.agx_gate import GateError, run_gate

        backend = FakeBackend(fail_release=True)
        with self.assertRaisesRegex(GateError, "did not release ownership"):
            run_gate(
                backend,
                self.contract,
                cycles=10,
                timeout_s=1,
                evidence_dir=self.path,
                clock=backend.clock,
            )

        manifest = json.loads((self.path / "gate-result.json").read_text())
        self.assertEqual(manifest["verdict"], "failed")
        self.assertFalse(manifest["windows_launch_permitted"])

    def test_less_than_ten_cycles_never_permits_windows(self):
        from tools.agx_gate import run_gate

        backend = FakeBackend()
        result = run_gate(
            backend,
            self.contract,
            cycles=3,
            timeout_s=1,
            evidence_dir=self.path,
            clock=backend.clock,
        )

        self.assertEqual(result.completed_cycles, 3)
        self.assertFalse(result.windows_launch_permitted)
        manifest = json.loads((self.path / "gate-result.json").read_text())
        self.assertEqual(manifest["verdict"], "incomplete")
        self.assertFalse(manifest["windows_launch_permitted"])

    def test_invalid_limits_are_rejected_before_backend_access(self):
        from tools.agx_gate import GateError, run_gate

        backend = FakeBackend()
        with self.assertRaisesRegex(GateError, "cycles"):
            run_gate(
                backend,
                self.contract,
                cycles=0,
                timeout_s=1,
                evidence_dir=self.path,
                clock=backend.clock,
            )
        with self.assertRaisesRegex(GateError, "timeout"):
            run_gate(
                backend,
                self.contract,
                cycles=10,
                timeout_s=0,
                evidence_dir=self.path,
                clock=backend.clock,
            )
        self.assertEqual(backend.calls, [])

    def _write_cold_cycle(self, index, *, same_boot=False):
        from tools.agx_contract import contract_sha256

        cycle_dir = self.path / f"cycle-{index:02d}"
        cycle_dir.mkdir()
        before = 0x100000 + index * 0x10000
        result = {
            "gate_version": 1,
            "contract_sha256": contract_sha256(self.contract),
            "requested_cycles": 1,
            "completed_cycles": 1,
            "timeout_s": 1.0,
            "cycles": [
                {
                    "cycle": 1,
                    "status": "passed",
                    "heartbeat": {"progress": True},
                    "snapshot": {
                        "firmware": {"m1n1_base": before},
                        "fault": {"source": "firmware-shared-memory"},
                    },
                }
            ],
            "verdict": "incomplete",
            "windows_launch_permitted": False,
        }
        (cycle_dir / "gate-result.json").write_text(json.dumps(result))
        receipt = {
            "reset_receipt_version": 1,
            "cycle": index,
            "platform": "J313",
            "firmware": "V13_5",
            "previous_m1n1_base": before,
            "m1n1_base": before if same_boot else before + 0x4000,
        }
        (self.path / f"reset-{index:02d}.json").write_text(json.dumps(receipt))

    def test_ten_cold_cycles_and_distinct_reset_receipts_permit_windows(self):
        from tools.agx_gate import aggregate_cold_results

        for index in range(1, 11):
            self._write_cold_cycle(index)

        result = aggregate_cold_results(self.path, self.contract, cycles=10)

        self.assertTrue(result["windows_launch_permitted"])
        self.assertEqual(result["completed_cycles"], 10)
        self.assertTrue(result["cold_reset_between_cycles"])
        self.assertEqual(len(result["cycles"]), 10)

    def test_same_proxy_boot_cannot_satisfy_cold_reset_receipt(self):
        from tools.agx_gate import GateError, aggregate_cold_results

        for index in range(1, 11):
            self._write_cold_cycle(index, same_boot=index == 4)

        with self.assertRaisesRegex(GateError, "fresh proxy boot"):
            aggregate_cold_results(self.path, self.contract, cycles=10)

    def test_missing_cold_reset_receipt_blocks_windows(self):
        from tools.agx_gate import GateError, aggregate_cold_results

        for index in range(1, 11):
            self._write_cold_cycle(index)
        (self.path / "reset-07.json").unlink()

        with self.assertRaisesRegex(GateError, "reset receipt"):
            aggregate_cold_results(self.path, self.contract, cycles=10)

    def test_proxy_receipt_requires_a_changed_boot_identity(self):
        from tools.agx_gate import GateError, record_proxy_receipt

        with self.assertRaisesRegex(GateError, "fresh proxy boot"):
            record_proxy_receipt(
                self.path / "reset-01.json",
                self.contract,
                cycle=1,
                previous_m1n1_base=0x804000000,
                live_platform="J313",
                live_firmware="V13_5",
                live_m1n1_base=0x804000000,
            )

    def test_proxy_receipt_records_exact_live_identity(self):
        from tools.agx_gate import record_proxy_receipt

        path = self.path / "reset-01.json"
        receipt = record_proxy_receipt(
            path,
            self.contract,
            cycle=1,
            previous_m1n1_base=0x804000000,
            live_platform="J313",
            live_firmware="V13_5",
            live_m1n1_base=0x804100000,
        )

        self.assertEqual(receipt, json.loads(path.read_text()))
        self.assertEqual(receipt["m1n1_base"], 0x804100000)


if __name__ == "__main__":
    unittest.main()
