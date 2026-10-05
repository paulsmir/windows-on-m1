import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from tools.agx_contract import contract_sha256, load_contract
from tools.agx_frame_fixture import ValidatedFrame


ROOT = Path(__file__).resolve().parents[1]
CONTRACT_PATH = ROOT / "config" / "j313-agx.json"
FIXTURE_HASH = "a" * 64
POISON_HASH = "b" * 64
OUTPUT_HASH = "c" * 64


def fixture():
    return ValidatedFrame(
        fixture_sha256=FIXTURE_HASH,
        command_buffer={},
        objects=(),
        output_gpu_va=0x1500000000,
        output_size=0x4000,
        poison_sha256=POISON_HASH,
        expected_output_sha256=OUTPUT_HASH,
    )


VALID_RENDER = {
    "context_id": 63,
    "page_size": 0x4000,
    "queue_index": 1,
    "ta_command_count": 2,
    "d3_command_count": 2,
    "ta_producer_before": 0,
    "ta_producer_after": 2,
    "ta_read_before": 0,
    "ta_read_after": 2,
    "ta_done_before": 0,
    "ta_done_after": 2,
    "d3_producer_before": 0,
    "d3_producer_after": 2,
    "d3_read_before": 0,
    "d3_read_after": 2,
    "d3_done_before": 0,
    "d3_done_after": 2,
    "wrap_ambiguous": False,
    "ta_event_id": 7,
    "d3_event_id": 8,
    "event_ta_matches": 1,
    "event_3d_matches": 1,
    "spurious_events": [],
    "ta_stamp_before": 0x7A000000,
    "ta_stamp_after": 0x7A000100,
    "d3_stamp_before": 0x3D000000,
    "d3_stamp_after": 0x3D000100,
    "output_sha256_before": POISON_HASH,
    "output_sha256_after": OUTPUT_HASH,
    "immutable_sha256_before": "d" * 64,
    "immutable_sha256_after": "d" * 64,
    "guards_unmapped": True,
    "declared_mapping_count": 4,
    "mapping_classification": [
        {"class": "firmware-shared", "context_id": 0, "gpu_va": 0x100000000, "size": 0x4000},
        {"class": "frame", "context_id": 63, "gpu_va": 0x1500000000, "size": 0x4000},
        {"class": "renderer", "context_id": 63, "gpu_va": 0x1600010000, "size": 0x4000},
        {"class": "bootstrap", "context_id": 63, "gpu_va": 0x6FFFFF8000, "size": 0x4000},
    ],
    "unexpected_mappings": [],
    "firmware_faults": {},
    "physical_fault_readable": True,
    "physical_fault_value": 0,
    "cleanup_complete": True,
    "elapsed_s": 0.012,
    "deadline_s": 0.5,
}


class RenderCompletionTests(unittest.TestCase):
    def _assert_rejected(self, field, value, boundary):
        from tools.agx_render_gate import RenderGateError, validate_render_completion

        receipt = copy.deepcopy(VALID_RENDER)
        receipt[field] = value
        with self.assertRaisesRegex(RenderGateError, boundary):
            validate_render_completion(receipt, fixture())

    def test_valid_receipt_returns_a_defensive_copy(self):
        from tools.agx_render_gate import validate_render_completion

        source = copy.deepcopy(VALID_RENDER)
        result = validate_render_completion(source, fixture())
        self.assertEqual(result, VALID_RENDER)
        self.assertIsNot(result, source)
        self.assertIsNot(result["spurious_events"], source["spurious_events"])

    def test_every_literal_boundary_is_fail_closed(self):
        mutations = (
            ("context_id", 0, "context_id"),
            ("page_size", 0x1000, "page_size"),
            ("queue_index", 0, "queue_index"),
            ("ta_command_count", 1, "TA command"),
            ("d3_command_count", 1, "3D command"),
            ("ta_producer_after", 1, "TA producer"),
            ("ta_read_after", 1, "TA read"),
            ("ta_done_after", 1, "TA done"),
            ("d3_producer_after", 1, "3D producer"),
            ("d3_read_after", 1, "3D read"),
            ("d3_done_after", 1, "3D done"),
            ("wrap_ambiguous", True, "wrap"),
            ("event_ta_matches", 0, "TA event"),
            ("event_3d_matches", 2, "3D event"),
            ("ta_event_id", 8, "event IDs"),
            ("spurious_events", [9], "spurious"),
            ("ta_stamp_after", 0x7A000000, "TA stamp"),
            ("d3_stamp_after", 0x3D000000, "3D stamp"),
            ("output_sha256_before", OUTPUT_HASH, "poison"),
            ("output_sha256_after", "2" * 64, "output"),
            ("immutable_sha256_after", "e" * 64, "immutable"),
            ("guards_unmapped", False, "guard"),
            ("declared_mapping_count", 3, "mapping count"),
            ("unexpected_mappings", [0x1500008000], "unexpected mapping"),
            ("firmware_faults", {"fault": 1}, "firmware fault"),
            ("physical_fault_value", 1, "physical fault"),
            ("physical_fault_readable", 1, "physical fault readable"),
            ("cleanup_complete", False, "cleanup"),
            ("elapsed_s", 0.500001, "deadline"),
            ("deadline_s", 1.0, "deadline_s"),
        )
        for field, value, boundary in mutations:
            with self.subTest(field=field):
                self._assert_rejected(field, value, boundary)

    def test_pointer_wrap_is_not_inferred_as_progress(self):
        self._assert_rejected("ta_done_before", 255, "TA done")

    def test_unreadable_physical_fault_requires_null_value(self):
        receipt = copy.deepcopy(VALID_RENDER)
        receipt["physical_fault_readable"] = False
        receipt["physical_fault_value"] = None
        from tools.agx_render_gate import validate_render_completion
        self.assertEqual(validate_render_completion(receipt, fixture()), receipt)
        receipt["physical_fault_value"] = 0
        from tools.agx_render_gate import RenderGateError
        with self.assertRaisesRegex(RenderGateError, "physical fault"):
            validate_render_completion(receipt, fixture())

    def test_unknown_field_is_rejected(self):
        receipt = copy.deepcopy(VALID_RENDER)
        receipt["extra"] = 1
        from tools.agx_render_gate import RenderGateError, validate_render_completion
        with self.assertRaisesRegex(RenderGateError, "fields"):
            validate_render_completion(receipt, fixture())

    def test_unknown_or_overlapping_mapping_classification_is_rejected(self):
        from tools.agx_render_gate import RenderGateError, validate_render_completion
        receipt = copy.deepcopy(VALID_RENDER)
        receipt["mapping_classification"][0]["class"] = "guest"
        with self.assertRaisesRegex(RenderGateError, "mapping class"):
            validate_render_completion(receipt, fixture())
        receipt = copy.deepcopy(VALID_RENDER)
        receipt["mapping_classification"][2]["gpu_va"] = 0x1500000000
        with self.assertRaisesRegex(RenderGateError, "overlap"):
            validate_render_completion(receipt, fixture())

    def test_booleans_are_not_integers(self):
        for field in ("context_id", "ta_done_after", "physical_fault_value"):
            with self.subTest(field=field):
                self._assert_rejected(field, True, field.split("_")[0])


class FixtureIdentityBindingTests(unittest.TestCase):
    def test_fixture_uses_capture_pin_not_runtime_m1n1_pin(self):
        from tools.agx_render_gate import _validate_fixture_identity

        contract = load_contract(CONTRACT_PATH)
        identity = {
            "board": contract.platform,
            "chip_generation": contract.firmware.generation,
            "firmware_version": contract.firmware.version,
            "m1n1_commit": contract.source.fixture_m1n1_commit,
            "adt_sha256": contract.source.adt_identity,
        }
        _validate_fixture_identity(contract, identity)


class FakeRenderBackend:
    def __init__(self, *, receipt=None, fail=None, released=True):
        self.receipt = copy.deepcopy(receipt or VALID_RENDER)
        self.fail = fail
        self.is_released = released
        self.calls = []

    def _call(self, name):
        self.calls.append(name)
        if self.fail == name:
            raise RuntimeError(f"{name} exploded")

    def prepare(self, contract, frame): self._call("prepare")
    def start(self): self._call("start")
    def heartbeat(self): self._call("heartbeat"); return {"alive": True}
    def configure_context(self, context_id): self._call(("context", context_id))
    def submit_frame(self, queue_index, timeout_s):
        self._call(("submit", queue_index, timeout_s))
        if self.fail == "submit": raise RuntimeError("submit exploded")
        return copy.deepcopy(self.receipt)
    def snapshot(self, reason):
        self.calls.append(("snapshot", reason))
        if self.fail == "snapshot": raise RuntimeError("snapshot exploded")
        return {
            "firmware": {"m1n1_base": 0x804000000, "proxy_identity": "proxy-a"},
            "reason": reason,
        }
    def stop(self): self._call("stop")
    def reset(self): self._call("reset")
    def released(self): self._call("released"); return self.is_released


class RenderLifecycleTests(unittest.TestCase):
    def setUp(self):
        self.contract = load_contract(CONTRACT_PATH)
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name)

    def tearDown(self): self.tmp.cleanup()

    def _manifest(self):
        return json.loads((self.path / "render-gate-result.json").read_text())

    def _fail(self, backend, boundary, *, clock=None):
        from tools.agx_render_gate import RenderGateError, run_render_gate
        kwargs = {} if clock is None else {"clock": clock}
        with self.assertRaisesRegex(RenderGateError, boundary):
            run_render_gate(
                backend, self.contract, fixture(), cycles=1,
                evidence_dir=self.path, **kwargs,
            )
        result = self._manifest()
        self.assertEqual(result["verdict"], "failed")
        self.assertFalse(result["windows_launch_permitted"])
        self.assertRegex(result["cycles"][0]["error"], boundary)
        return result

    def test_one_shot_sequence_never_permits_windows(self):
        from tools.agx_render_gate import run_render_gate
        backend = FakeRenderBackend()
        result = run_render_gate(
            backend, self.contract, fixture(), cycles=1, evidence_dir=self.path
        )
        self.assertEqual(result.verdict, "incomplete")
        self.assertFalse(result.windows_launch_permitted)
        self.assertEqual(backend.calls, [
            "prepare", "start", "heartbeat", ("context", 63),
            ("submit", 1, 0.5), ("snapshot", "cycle-complete"),
            "stop", "reset", "released",
        ])

    def test_malformed_receipt_preserves_snapshot_and_cleanup(self):
        receipt = copy.deepcopy(VALID_RENDER)
        receipt["event_3d_matches"] = 0
        backend = FakeRenderBackend(receipt=receipt)
        result = self._fail(backend, "3D event")
        self.assertIn("snapshot", result["cycles"][0])
        self.assertIn("stop", backend.calls)
        self.assertIn("reset", backend.calls)

    def test_host_deadline_is_independent(self):
        times = iter((10.0, 10.6))
        self._fail(FakeRenderBackend(), "host submit deadline", clock=lambda: next(times))

    def test_each_backend_failure_is_retained_and_fails_closed(self):
        for boundary in ("prepare", "start", "heartbeat", "submit", "snapshot", "stop", "reset"):
            with self.subTest(boundary=boundary):
                with tempfile.TemporaryDirectory() as directory:
                    self.path = Path(directory)
                    result = self._fail(FakeRenderBackend(fail=boundary), boundary)
                    record = result["cycles"][0]
                    if boundary in ("stop", "reset"):
                        self.assertIn(f"{boundary}_error", record)

    def test_false_release_fails_closed(self):
        result = self._fail(FakeRenderBackend(released=False), "release")
        self.assertFalse(result["cycles"][0]["released"])

    def test_live_gate_requires_one_cycle_and_fresh_evidence(self):
        from tools.agx_render_gate import RenderGateError, run_render_gate
        with self.assertRaisesRegex(RenderGateError, "exactly"):
            run_render_gate(
                FakeRenderBackend(), self.contract, fixture(), cycles=10,
                evidence_dir=self.path,
            )
        self.path.mkdir(exist_ok=True)
        (self.path / "stale").write_text("x")
        with self.assertRaisesRegex(RenderGateError, "fresh"):
            run_render_gate(
                FakeRenderBackend(), self.contract, fixture(), cycles=1,
                evidence_dir=self.path,
            )


def canonical_sha256(value):
    return hashlib.sha256(
        json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()


class RenderAggregateTests(unittest.TestCase):
    def setUp(self):
        self.contract = load_contract(CONTRACT_PATH)
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name)

    def tearDown(self): self.tmp.cleanup()

    def _write_cycle(self, index, *, proxy=None, base=None, cookie=None):
        cookie = cookie or 0x1000000000000000 + index
        proxy = proxy or f"J313:V13_5:{cookie:016x}"
        base = base or 0x804000000 + index * 0x200000
        directory = self.path / f"cycle-{index:02d}"
        directory.mkdir()
        result = {
            "render_gate_version": 2,
            "contract_sha256": contract_sha256(self.contract),
            "fixture_sha256": FIXTURE_HASH,
            "requested_cycles": 1,
            "completed_cycles": 1,
            "cycles": [{
                "cycle": 1,
                "status": "passed",
                "heartbeat": {"alive": True},
                "host_submit_elapsed_s": 0.01,
                "completion": copy.deepcopy(VALID_RENDER),
                "snapshot": {"firmware": {
                    "m1n1_base": base,
                    "boot_cookie": cookie,
                    "proxy_identity": proxy,
                }},
            }],
            "verdict": "incomplete",
            "windows_launch_permitted": False,
        }
        result_path = directory / "render-gate-result.json"
        result_path.write_text(json.dumps(result))
        next_cookie = 0x1000000000000000 + index + 1
        next_proxy = f"J313:V13_5:{next_cookie:016x}"
        next_base = 0x804000000 + (index + 1) * 0x200000
        reset = {
            "render_reset_receipt_version": 2,
            "cycle": index,
            "platform": "J313",
            "firmware": "V13_5",
            "previous_proxy_identity": proxy,
            "proxy_identity": next_proxy,
            "previous_boot_cookie": cookie,
            "boot_cookie": next_cookie,
            "previous_m1n1_base": base,
            "m1n1_base": next_base,
            "cycle_result_sha256": canonical_sha256(result),
            "fresh_proxy": True,
        }
        (self.path / f"reset-{index:02d}.json").write_text(json.dumps(reset))

    def _valid(self):
        for index in range(1, 11): self._write_cycle(index)

    def test_ten_bound_cold_cycles_permit_windows(self):
        from tools.agx_render_gate import aggregate_cold_render_results
        self._valid()
        result = aggregate_cold_render_results(
            self.path, self.contract, fixture(), cycles=10
        )
        self.assertEqual(result["render_aggregate_version"], 2)
        self.assertEqual(result["completed_cycles"], 10)
        self.assertTrue(result["cold_reset_between_cycles"])
        self.assertTrue(result["windows_launch_permitted"])
        self.assertRegex(result["aggregate_sha256"], r"^[0-9a-f]{64}$")

    def test_proxy_receipt_binds_exact_result_and_fresh_identity(self):
        from tools.agx_render_gate import record_render_proxy_receipt
        self._write_cycle(1)
        result_path = self.path / "cycle-01" / "render-gate-result.json"
        output = self.path / "recorded-reset.json"
        receipt = record_render_proxy_receipt(
            output,
            self.contract,
            fixture(),
            cycle=1,
            cycle_result=result_path,
            live_platform="J313",
            live_firmware="V13_5",
            live_proxy_identity="J313:V13_5:1000000000000002",
            live_boot_cookie=0x1000000000000002,
            live_m1n1_base=0x804200000,
        )
        self.assertEqual(receipt["cycle_result_sha256"], canonical_sha256(
            json.loads(result_path.read_text())
        ))
        self.assertEqual(json.loads(output.read_text()), receipt)

    def test_proxy_receipt_rejects_same_boot(self):
        from tools.agx_render_gate import RenderGateError, record_render_proxy_receipt
        self._write_cycle(1)
        result_path = self.path / "cycle-01" / "render-gate-result.json"
        with self.assertRaisesRegex(RenderGateError, "fresh proxy"):
            record_render_proxy_receipt(
                self.path / "bad-reset.json",
                self.contract,
                fixture(),
                cycle=1,
                cycle_result=result_path,
                live_platform="J313",
                live_firmware="V13_5",
                live_proxy_identity="J313:V13_5:1000000000000001",
                live_boot_cookie=0x1000000000000001,
                live_m1n1_base=0x900000000,
            )

    def test_exactly_ten_cycles_are_required(self):
        from tools.agx_render_gate import RenderGateError, aggregate_cold_render_results
        with self.assertRaisesRegex(RenderGateError, "exactly 10"):
            aggregate_cold_render_results(self.path, self.contract, fixture(), cycles=9)

    def test_edited_cycle_after_receipt_is_rejected(self):
        from tools.agx_render_gate import RenderGateError, aggregate_cold_render_results
        self._valid()
        path = self.path / "cycle-04" / "render-gate-result.json"
        value = json.loads(path.read_text())
        value["cycles"][0]["completion"]["elapsed_s"] = 0.02
        path.write_text(json.dumps(value))
        with self.assertRaisesRegex(RenderGateError, "SHA-256"):
            aggregate_cold_render_results(self.path, self.contract, fixture(), cycles=10)

    def test_reordered_reset_is_rejected(self):
        from tools.agx_render_gate import RenderGateError, aggregate_cold_render_results
        self._valid()
        path = self.path / "reset-03.json"
        value = json.loads(path.read_text()); value["cycle"] = 4
        path.write_text(json.dumps(value))
        with self.assertRaisesRegex(RenderGateError, "reset receipt 3"):
            aggregate_cold_render_results(self.path, self.contract, fixture(), cycles=10)

    def test_reused_proxy_identity_is_rejected(self):
        from tools.agx_render_gate import RenderGateError, aggregate_cold_render_results
        for index in range(1, 11):
            self._write_cycle(index, proxy="reused" if index in (5, 6) else None)
        with self.assertRaisesRegex(RenderGateError, "distinct proxy"):
            aggregate_cold_render_results(self.path, self.contract, fixture(), cycles=10)

    def test_reused_m1n1_base_is_accepted_when_cookies_are_unique(self):
        from tools.agx_render_gate import aggregate_cold_render_results
        reused = 0x900000000
        for index in range(1, 11):
            self._write_cycle(index, base=reused if index in (5, 6) else None)
        result = aggregate_cold_render_results(
            self.path, self.contract, fixture(), cycles=10
        )
        self.assertTrue(result["windows_launch_permitted"])

    def test_reused_boot_cookie_is_rejected_even_when_base_changes(self):
        from tools.agx_render_gate import RenderGateError, aggregate_cold_render_results
        reused = 0x123456789abcdef0
        for index in range(1, 11):
            self._write_cycle(index, cookie=reused if index in (5, 6) else None)
        with self.assertRaisesRegex(RenderGateError, "distinct boot cookies"):
            aggregate_cold_render_results(self.path, self.contract, fixture(), cycles=10)

    def test_incomplete_render_is_rejected_even_when_rehashed(self):
        from tools.agx_render_gate import RenderGateError, aggregate_cold_render_results
        self._valid()
        result_path = self.path / "cycle-07" / "render-gate-result.json"
        result = json.loads(result_path.read_text())
        result["cycles"][0]["completion"]["output_sha256_after"] = "e" * 64
        result_path.write_text(json.dumps(result))
        reset_path = self.path / "reset-07.json"
        reset = json.loads(reset_path.read_text())
        reset["cycle_result_sha256"] = canonical_sha256(result)
        reset_path.write_text(json.dumps(reset))
        with self.assertRaisesRegex(RenderGateError, "output"):
            aggregate_cold_render_results(self.path, self.contract, fixture(), cycles=10)

    def test_verify_cli_rejects_a_byte_edited_aggregate(self):
        from tools.agx_render_gate import aggregate_cold_render_results
        self._valid()
        aggregate_cold_render_results(self.path, self.contract, fixture(), cycles=10)
        path = self.path / "render-gate-result.json"
        good = subprocess.run(
            [sys.executable, "-m", "tools.agx_render_gate", "verify-result", str(path)],
            cwd=ROOT, text=True, capture_output=True, check=False,
        )
        self.assertEqual(good.returncode, 0, good.stderr)
        data = json.loads(path.read_text())
        data["cycles"][0]["completion"]["elapsed_s"] = 0.02
        path.write_text(json.dumps(data))
        bad = subprocess.run(
            [sys.executable, "-m", "tools.agx_render_gate", "verify-result", str(path)],
            cwd=ROOT, text=True, capture_output=True, check=False,
        )
        self.assertNotEqual(bad.returncode, 0)
        self.assertIn("aggregate_sha256", bad.stderr)

    def test_rehashed_unknown_aggregate_field_is_rejected(self):
        from tools.agx_render_gate import (
            RenderGateError, aggregate_cold_render_results, verify_render_gate_result,
        )
        self._valid()
        aggregate_cold_render_results(self.path, self.contract, fixture(), cycles=10)
        path = self.path / "render-gate-result.json"
        data = json.loads(path.read_text())
        data["unknown"] = True
        data.pop("aggregate_sha256")
        data["aggregate_sha256"] = canonical_sha256(data)
        path.write_text(json.dumps(data))
        with self.assertRaisesRegex(RenderGateError, "fields"):
            verify_render_gate_result(path)


if __name__ == "__main__":
    unittest.main()
