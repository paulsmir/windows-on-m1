import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from tools.agx_contract import load_contract


ROOT = Path(__file__).resolve().parents[1]
CONTRACT = ROOT / "config" / "j313-agx.json"


VALID_COMPLETION = {
    "context_id": 63,
    "page_size": 0x4000,
    "queue_index": 1,
    "queue_type": "3D",
    "submitted_commands": 1,
    "producer_before": 0,
    "producer_after": 1,
    "consumer_before": 0,
    "consumer_after": 1,
    "event_id": 0,
    "event_count_before": 7,
    "event_count_after": 8,
    "matching_event_count": 1,
    "stamp_before": 0x51000000,
    "stamp_after": 0x51000000,
    "elapsed_s": 0.004,
    "deadline_s": 0.5,
    "canary_sha256_before": "1" * 64,
    "canary_sha256_after": "1" * 64,
    "guards_unmapped": True,
    "declared_mapping_count": 1,
    "unexpected_mappings": [],
}


class QueueCompletionTests(unittest.TestCase):
    def _assert_rejected(self, field, value, boundary):
        from tools.agx_queue_gate import QueueGateError, validate_completion

        receipt = copy.deepcopy(VALID_COMPLETION)
        receipt[field] = value
        with self.assertRaisesRegex(QueueGateError, boundary):
            validate_completion(receipt)

    def test_valid_completion_returns_a_defensive_copy(self):
        from tools.agx_queue_gate import validate_completion

        source = copy.deepcopy(VALID_COMPLETION)
        validated = validate_completion(source)
        self.assertEqual(validated, VALID_COMPLETION)
        self.assertIsNot(validated, source)
        self.assertIsNot(validated["unexpected_mappings"], source["unexpected_mappings"])

    def test_wrong_context_is_rejected(self):
        self._assert_rejected("context_id", 62, "context_id")

    def test_wrong_page_size_is_rejected(self):
        self._assert_rejected("page_size", 0x1000, "page_size")

    def test_wrong_queue_index_is_rejected(self):
        self._assert_rejected("queue_index", 0, "queue_index")

    def test_wrong_queue_type_is_rejected(self):
        self._assert_rejected("queue_type", "TA", "queue_type")

    def test_wrong_command_count_is_rejected(self):
        self._assert_rejected("submitted_commands", 2, "submitted_commands")

    def test_wrong_producer_delta_is_rejected(self):
        self._assert_rejected("producer_after", 2, "producer")

    def test_consumer_mismatch_is_rejected(self):
        self._assert_rejected("consumer_after", 0, "consumer")

    def test_wrong_event_delta_is_rejected(self):
        self._assert_rejected("event_count_after", 9, "event_count")

    def test_duplicate_matching_event_is_rejected(self):
        self._assert_rejected("matching_event_count", 2, "matching_event_count")

    def test_changed_stamp_is_rejected(self):
        self._assert_rejected("stamp_after", 0x51000001, "stamp")

    def test_elapsed_deadline_is_rejected(self):
        self._assert_rejected("elapsed_s", 0.500001, "elapsed_s")

    def test_wrong_declared_deadline_is_rejected(self):
        self._assert_rejected("deadline_s", 1.0, "deadline_s")

    def test_changed_canary_is_rejected(self):
        self._assert_rejected("canary_sha256_after", "2" * 64, "canary")

    def test_malformed_canary_hash_is_rejected(self):
        self._assert_rejected("canary_sha256_before", "A" * 64, "canary")

    def test_mapped_guard_is_rejected(self):
        self._assert_rejected("guards_unmapped", False, "guards_unmapped")

    def test_wrong_mapping_count_is_rejected(self):
        self._assert_rejected("declared_mapping_count", 2, "declared_mapping_count")

    def test_unexpected_mapping_is_rejected(self):
        self._assert_rejected("unexpected_mappings", [0x1600008000], "unexpected_mappings")

    def test_unknown_field_is_rejected(self):
        from tools.agx_queue_gate import QueueGateError, validate_completion

        receipt = copy.deepcopy(VALID_COMPLETION)
        receipt["extra"] = True
        with self.assertRaisesRegex(QueueGateError, "fields"):
            validate_completion(receipt)

    def test_boolean_is_not_accepted_as_an_integer(self):
        self._assert_rejected("context_id", True, "context_id")


class FakeQueueBackend:
    def __init__(
        self,
        *,
        completion=None,
        submit_error=None,
        snapshot_error=None,
        stop_error=None,
        reset_error=None,
        fail_release=False,
    ):
        self.calls = []
        self.completion = copy.deepcopy(completion or VALID_COMPLETION)
        self.submit_error = submit_error
        self.snapshot_error = snapshot_error
        self.stop_error = stop_error
        self.reset_error = reset_error
        self.fail_release = fail_release

    def prepare(self, contract):
        self.calls.append("prepare")
        self.contract = contract

    def start(self):
        self.calls.append("start")

    def heartbeat(self):
        self.calls.append("heartbeat")
        return {"alive": True}

    def configure_context(self, context_id):
        self.calls.append(("context", context_id))

    def submit_barrier(self, queue_index, timeout_s):
        self.calls.append(("submit", queue_index, timeout_s))
        if self.submit_error:
            raise RuntimeError(self.submit_error)
        return copy.deepcopy(self.completion)

    def snapshot(self, reason):
        self.calls.append(("snapshot", reason))
        if self.snapshot_error:
            raise RuntimeError(self.snapshot_error)
        return {"reason": reason, "fault": None}

    def stop(self):
        self.calls.append("stop")
        if self.stop_error:
            raise RuntimeError(self.stop_error)

    def reset(self):
        self.calls.append("reset")
        if self.reset_error:
            raise RuntimeError(self.reset_error)

    def released(self):
        self.calls.append("released")
        return not self.fail_release


class QueueLifecycleTests(unittest.TestCase):
    def setUp(self):
        self.contract = load_contract(CONTRACT)
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def _manifest(self):
        return json.loads((self.path / "queue-gate-result.json").read_text())

    def _assert_failed(self, backend, boundary):
        from tools.agx_queue_gate import QueueGateError, run_queue_gate

        with self.assertRaisesRegex(QueueGateError, boundary):
            run_queue_gate(
                backend,
                self.contract,
                cycles=1,
                evidence_dir=self.path,
            )
        manifest = self._manifest()
        self.assertEqual(manifest["verdict"], "failed")
        self.assertFalse(manifest["windows_launch_permitted"])
        self.assertRegex(manifest["cycles"][0]["error"], boundary)
        return manifest

    def test_one_shot_runs_exact_sequence_and_never_permits_windows(self):
        from tools.agx_queue_gate import run_queue_gate

        backend = FakeQueueBackend()
        result = run_queue_gate(
            backend,
            self.contract,
            cycles=1,
            evidence_dir=self.path,
        )

        self.assertEqual(result.verdict, "incomplete")
        self.assertFalse(result.windows_launch_permitted)
        self.assertEqual(
            backend.calls,
            [
                "prepare",
                "start",
                "heartbeat",
                ("context", 63),
                ("submit", 1, 0.5),
                ("snapshot", "cycle-complete"),
                "stop",
                "reset",
                "released",
            ],
        )
        manifest = self._manifest()
        self.assertEqual(manifest["completed_cycles"], 1)
        self.assertEqual(manifest["cycles"][0]["completion"], VALID_COMPLETION)

    def test_malformed_completion_fails_closed_with_snapshot(self):
        completion = copy.deepcopy(VALID_COMPLETION)
        completion["matching_event_count"] = 2
        backend = FakeQueueBackend(completion=completion)
        manifest = self._assert_failed(backend, "matching_event_count")
        self.assertEqual(manifest["cycles"][0]["snapshot"]["fault"], None)
        self.assertIn("stop", backend.calls)
        self.assertIn("reset", backend.calls)

    def test_submit_exception_fails_closed_with_snapshot(self):
        backend = FakeQueueBackend(submit_error="submit exploded")
        manifest = self._assert_failed(backend, "submit exploded")
        self.assertIn("snapshot", str(manifest["cycles"][0]))

    def test_reported_deadline_failure_fails_closed(self):
        completion = copy.deepcopy(VALID_COMPLETION)
        completion["elapsed_s"] = 0.75
        backend = FakeQueueBackend(completion=completion)
        self._assert_failed(backend, "elapsed_s")

    def test_snapshot_exception_is_preserved_with_cleanup(self):
        backend = FakeQueueBackend(snapshot_error="snapshot exploded")
        manifest = self._assert_failed(backend, "snapshot exploded")
        self.assertRegex(manifest["cycles"][0]["snapshot_error"], "snapshot exploded")
        self.assertIn("stop", backend.calls)
        self.assertIn("reset", backend.calls)

    def test_stop_exception_is_preserved_and_reset_runs(self):
        backend = FakeQueueBackend(stop_error="stop exploded")
        manifest = self._assert_failed(backend, "stop exploded")
        self.assertRegex(manifest["cycles"][0]["stop_error"], "stop exploded")
        self.assertIn("reset", backend.calls)

    def test_reset_exception_is_preserved(self):
        backend = FakeQueueBackend(reset_error="reset exploded")
        manifest = self._assert_failed(backend, "reset exploded")
        self.assertRegex(manifest["cycles"][0]["reset_error"], "reset exploded")

    def test_false_release_fails_closed(self):
        backend = FakeQueueBackend(fail_release=True)
        manifest = self._assert_failed(backend, "release")
        self.assertFalse(manifest["cycles"][0]["released"])


def _canonical_sha256(data):
    encoded = json.dumps(data, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(encoded).hexdigest()


class QueueAggregateTests(unittest.TestCase):
    def setUp(self):
        self.contract = load_contract(CONTRACT)
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def _write_cycle(self, index, *, same_boot=False):
        from tools.agx_contract import contract_sha256

        cycle_dir = self.path / f"cycle-{index:02d}"
        cycle_dir.mkdir()
        previous_base = 0x804000000 + index * 0x200000
        result = {
            "queue_gate_version": 1,
            "contract_sha256": contract_sha256(self.contract),
            "requested_cycles": 1,
            "completed_cycles": 1,
            "cycles": [
                {
                    "cycle": 1,
                    "status": "passed",
                    "heartbeat": {"alive": True},
                    "host_submit_elapsed_s": 0.004,
                    "completion": copy.deepcopy(VALID_COMPLETION),
                    "snapshot": {
                        "firmware": {"m1n1_base": previous_base},
                        "fault": {"source": "firmware-shared-memory", "faulted": False},
                    },
                }
            ],
            "verdict": "incomplete",
            "windows_launch_permitted": False,
        }
        result_path = cycle_dir / "queue-gate-result.json"
        result_path.write_text(json.dumps(result))
        receipt = {
            "reset_receipt_version": 1,
            "cycle": index,
            "platform": "J313",
            "firmware": "V13_5",
            "previous_m1n1_base": previous_base,
            "m1n1_base": previous_base if same_boot else previous_base + 0x4000,
            "cycle_result_sha256": _canonical_sha256(result),
            "fresh_proxy": not same_boot,
        }
        (self.path / f"reset-{index:02d}.json").write_text(json.dumps(receipt))

    def _write_valid_set(self):
        for index in range(1, 11):
            self._write_cycle(index)

    def test_ten_bound_cold_cycles_permit_windows(self):
        from tools.agx_queue_gate import aggregate_cold_queue_results

        self._write_valid_set()
        result = aggregate_cold_queue_results(self.path, self.contract, cycles=10)

        self.assertEqual(result["queue_gate_version"], 2)
        self.assertEqual(result["completed_cycles"], 10)
        self.assertTrue(result["cold_reset_between_cycles"])
        self.assertTrue(result["windows_launch_permitted"])
        self.assertRegex(result["aggregate_sha256"], r"^[0-9a-f]{64}$")
        self.assertEqual(len(result["cycles"]), 10)

    def test_fewer_than_ten_cycles_are_rejected(self):
        from tools.agx_queue_gate import QueueGateError, aggregate_cold_queue_results

        with self.assertRaisesRegex(QueueGateError, "exactly 10"):
            aggregate_cold_queue_results(self.path, self.contract, cycles=9)

    def test_edited_cycle_after_receipt_is_rejected(self):
        from tools.agx_queue_gate import QueueGateError, aggregate_cold_queue_results

        self._write_valid_set()
        path = self.path / "cycle-04" / "queue-gate-result.json"
        data = json.loads(path.read_text())
        data["cycles"][0]["completion"]["elapsed_s"] = 0.005
        path.write_text(json.dumps(data))
        with self.assertRaisesRegex(QueueGateError, "SHA-256"):
            aggregate_cold_queue_results(self.path, self.contract, cycles=10)

    def test_reordered_reset_receipt_is_rejected(self):
        from tools.agx_queue_gate import QueueGateError, aggregate_cold_queue_results

        self._write_valid_set()
        receipt = json.loads((self.path / "reset-03.json").read_text())
        receipt["cycle"] = 4
        (self.path / "reset-03.json").write_text(json.dumps(receipt))
        with self.assertRaisesRegex(QueueGateError, "reset receipt 3"):
            aggregate_cold_queue_results(self.path, self.contract, cycles=10)

    def test_reused_proxy_identity_is_rejected(self):
        from tools.agx_queue_gate import QueueGateError, aggregate_cold_queue_results

        for index in range(1, 11):
            self._write_cycle(index, same_boot=index == 6)
        with self.assertRaisesRegex(QueueGateError, "fresh proxy boot"):
            aggregate_cold_queue_results(self.path, self.contract, cycles=10)

    def test_false_fresh_proxy_claim_is_rejected(self):
        from tools.agx_queue_gate import QueueGateError, aggregate_cold_queue_results

        self._write_valid_set()
        path = self.path / "reset-05.json"
        receipt = json.loads(path.read_text())
        receipt["fresh_proxy"] = False
        path.write_text(json.dumps(receipt))
        with self.assertRaisesRegex(QueueGateError, "fresh proxy boot"):
            aggregate_cold_queue_results(self.path, self.contract, cycles=10)

    def test_incomplete_queue_completion_is_rejected_even_if_rehashed(self):
        from tools.agx_queue_gate import QueueGateError, aggregate_cold_queue_results

        self._write_valid_set()
        result_path = self.path / "cycle-07" / "queue-gate-result.json"
        result = json.loads(result_path.read_text())
        result["cycles"][0]["completion"]["consumer_after"] = 0
        result_path.write_text(json.dumps(result))
        receipt_path = self.path / "reset-07.json"
        receipt = json.loads(receipt_path.read_text())
        receipt["cycle_result_sha256"] = _canonical_sha256(result)
        receipt_path.write_text(json.dumps(receipt))
        with self.assertRaisesRegex(QueueGateError, "consumer"):
            aggregate_cold_queue_results(self.path, self.contract, cycles=10)

    def test_g1_result_cannot_be_used_as_g1q_evidence(self):
        from tools.agx_queue_gate import QueueGateError, aggregate_cold_queue_results

        self._write_valid_set()
        result_path = self.path / "cycle-02" / "queue-gate-result.json"
        result = json.loads(result_path.read_text())
        result["gate_version"] = result.pop("queue_gate_version")
        result_path.write_text(json.dumps(result))
        receipt_path = self.path / "reset-02.json"
        receipt = json.loads(receipt_path.read_text())
        receipt["cycle_result_sha256"] = _canonical_sha256(result)
        receipt_path.write_text(json.dumps(receipt))
        with self.assertRaisesRegex(QueueGateError, "G1Q"):
            aggregate_cold_queue_results(self.path, self.contract, cycles=10)

    def test_verify_rejects_an_edited_aggregate(self):
        from tools.agx_queue_gate import (
            QueueGateError,
            aggregate_cold_queue_results,
            verify_queue_gate_result,
        )

        self._write_valid_set()
        aggregate_cold_queue_results(self.path, self.contract, cycles=10)
        path = self.path / "queue-gate-result.json"
        result = json.loads(path.read_text())
        result["cycles"][0]["completion"]["elapsed_s"] = 0.006
        path.write_text(json.dumps(result))
        with self.assertRaisesRegex(QueueGateError, "aggregate_sha256"):
            verify_queue_gate_result(path)

    def test_proxy_receipt_binds_exact_cycle_result_and_live_identity(self):
        from tools.agx_queue_gate import record_queue_proxy_receipt

        self._write_cycle(1)
        result_path = self.path / "cycle-01" / "queue-gate-result.json"
        output = self.path / "recorded-reset.json"
        receipt = record_queue_proxy_receipt(
            output,
            self.contract,
            cycle=1,
            cycle_result=result_path,
            live_platform="J313",
            live_firmware="V13_5",
            live_m1n1_base=0x900000000,
        )
        result = json.loads(result_path.read_text())
        previous = result["cycles"][0]["snapshot"]["firmware"]["m1n1_base"]
        self.assertEqual(receipt, json.loads(output.read_text()))
        self.assertEqual(receipt["previous_m1n1_base"], previous)
        self.assertEqual(receipt["cycle_result_sha256"], _canonical_sha256(result))
        self.assertTrue(receipt["fresh_proxy"])

    def test_proxy_receipt_rejects_same_boot_identity(self):
        from tools.agx_queue_gate import QueueGateError, record_queue_proxy_receipt

        self._write_cycle(1)
        result_path = self.path / "cycle-01" / "queue-gate-result.json"
        result = json.loads(result_path.read_text())
        previous = result["cycles"][0]["snapshot"]["firmware"]["m1n1_base"]
        with self.assertRaisesRegex(QueueGateError, "fresh proxy boot"):
            record_queue_proxy_receipt(
                self.path / "reset.json",
                self.contract,
                cycle=1,
                cycle_result=result_path,
                live_platform="J313",
                live_firmware="V13_5",
                live_m1n1_base=previous,
            )


class QueueCliTests(unittest.TestCase):
    def test_cli_exposes_required_commands_without_importing_hardware(self):
        result = subprocess.run(
            [sys.executable, "-m", "tools.agx_queue_gate", "--help"],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        for command in ("run-one", "proxy-receipt", "aggregate-cold", "verify-result"):
            self.assertIn(command, result.stdout)


if __name__ == "__main__":
    unittest.main()
