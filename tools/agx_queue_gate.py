"""Fail-closed evidence validation for the J313 AGX queue gate."""

import argparse
import copy
from dataclasses import dataclass
import json
import math
import os
from pathlib import Path
import re
import sys
import time
from typing import Protocol

from tools.agx_contract import AgxContract, contract_sha256


ROOT = Path(__file__).resolve().parents[1]
PROXYCLIENT = ROOT / "m1n1_windows" / "proxyclient"


GATE_VERSION = 1
AGGREGATE_VERSION = 2
CONTEXT_ID = 63
PAGE_SIZE = 0x4000
QUEUE_INDEX = 1
QUEUE_TYPE = "3D"
COMPLETION_DEADLINE_S = 0.5
QUALIFICATION_CYCLES = 10

_SHA256_RE = re.compile(r"[0-9a-f]{64}\Z")
_COMPLETION_FIELDS = frozenset({
    "context_id",
    "page_size",
    "queue_index",
    "queue_type",
    "submitted_commands",
    "producer_before",
    "producer_after",
    "consumer_before",
    "consumer_after",
    "event_id",
    "event_count_before",
    "event_count_after",
    "matching_event_count",
    "stamp_before",
    "stamp_after",
    "elapsed_s",
    "deadline_s",
    "canary_sha256_before",
    "canary_sha256_after",
    "guards_unmapped",
    "declared_mapping_count",
    "unexpected_mappings",
})


class QueueGateError(RuntimeError):
    """Queue evidence failed a G1Q safety boundary."""


class QueueGateBackend(Protocol):
    def prepare(self, contract: AgxContract) -> None: ...

    def start(self) -> None: ...

    def heartbeat(self) -> dict: ...

    def configure_context(self, context_id: int) -> None: ...

    def submit_barrier(self, queue_index: int, timeout_s: float) -> dict: ...

    def snapshot(self, reason: str) -> dict: ...

    def stop(self) -> None: ...

    def reset(self) -> None: ...

    def released(self) -> bool: ...


@dataclass(frozen=True)
class QueueGateResult:
    completed_cycles: int
    windows_launch_permitted: bool
    verdict: str
    evidence_path: Path


def _integer(receipt: dict, field: str, *, minimum: int = 0) -> int:
    value = receipt[field]
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise QueueGateError(f"{field} must be an integer >= {minimum}")
    return value


def _literal(receipt: dict, field: str, expected) -> None:
    value = receipt[field]
    if isinstance(expected, int):
        _integer(receipt, field)
    if value != expected:
        raise QueueGateError(f"{field} must equal {expected!r}")


def validate_completion(receipt: dict) -> dict:
    """Validate one exact, bounded queue completion and return a deep copy."""

    if not isinstance(receipt, dict):
        raise QueueGateError("completion receipt must be an object")
    fields = frozenset(receipt)
    if fields != _COMPLETION_FIELDS:
        missing = sorted(_COMPLETION_FIELDS - fields)
        unknown = sorted(fields - _COMPLETION_FIELDS)
        raise QueueGateError(
            f"completion fields mismatch: missing={missing}, unknown={unknown}"
        )

    _literal(receipt, "context_id", CONTEXT_ID)
    _literal(receipt, "page_size", PAGE_SIZE)
    _literal(receipt, "queue_index", QUEUE_INDEX)
    _literal(receipt, "queue_type", QUEUE_TYPE)
    _literal(receipt, "submitted_commands", 1)
    _literal(receipt, "event_id", 0)
    _literal(receipt, "matching_event_count", 1)
    _literal(receipt, "stamp_before", 0x51000000)
    _literal(receipt, "declared_mapping_count", 1)

    producer_before = _integer(receipt, "producer_before")
    producer_after = _integer(receipt, "producer_after")
    if producer_after <= producer_before or producer_after - producer_before != 1:
        raise QueueGateError("producer pointer must advance exactly once")

    consumer_before = _integer(receipt, "consumer_before")
    consumer_after = _integer(receipt, "consumer_after")
    if consumer_after <= consumer_before or consumer_after - consumer_before != 1:
        raise QueueGateError("consumer pointer must advance exactly once")
    if consumer_before != producer_before or consumer_after != producer_after:
        raise QueueGateError("consumer pointer must match producer pointer")

    event_before = _integer(receipt, "event_count_before")
    event_after = _integer(receipt, "event_count_after")
    if event_after <= event_before or event_after - event_before != 1:
        raise QueueGateError("event_count must advance exactly once")

    stamp_after = _integer(receipt, "stamp_after")
    if stamp_after != receipt["stamp_before"]:
        raise QueueGateError("stamp must remain at the already-satisfied value")

    elapsed = receipt["elapsed_s"]
    if (isinstance(elapsed, bool) or not isinstance(elapsed, (int, float))
            or not math.isfinite(elapsed) or elapsed < 0):
        raise QueueGateError("elapsed_s must be a finite non-negative number")
    deadline = receipt["deadline_s"]
    if (isinstance(deadline, bool) or not isinstance(deadline, (int, float))
            or not math.isfinite(deadline)
            or float(deadline) != COMPLETION_DEADLINE_S):
        raise QueueGateError(
            f"deadline_s must equal {COMPLETION_DEADLINE_S}"
        )
    if float(elapsed) > float(deadline):
        raise QueueGateError("elapsed_s exceeds the completion deadline")

    before_hash = receipt["canary_sha256_before"]
    after_hash = receipt["canary_sha256_after"]
    if not isinstance(before_hash, str) or not _SHA256_RE.fullmatch(before_hash):
        raise QueueGateError("canary_sha256_before must be lowercase SHA-256")
    if not isinstance(after_hash, str) or not _SHA256_RE.fullmatch(after_hash):
        raise QueueGateError("canary_sha256_after must be lowercase SHA-256")
    if before_hash != after_hash:
        raise QueueGateError("canary hash changed during queue execution")

    if receipt["guards_unmapped"] is not True:
        raise QueueGateError("guards_unmapped must be true")
    mappings = receipt["unexpected_mappings"]
    if not isinstance(mappings, list) or mappings:
        raise QueueGateError("unexpected_mappings must be an empty list")

    return copy.deepcopy(receipt)


def _atomic_json(path: Path, data: dict) -> None:
    encoded = json.dumps(data, indent=2, sort_keys=True) + "\n"
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(encoded)
    temporary.replace(path)


def _one_shot_manifest(contract: AgxContract) -> dict:
    return {
        "queue_gate_version": GATE_VERSION,
        "contract_sha256": contract_sha256(contract),
        "requested_cycles": 1,
        "completed_cycles": 0,
        "cycles": [],
        "verdict": "running",
        "windows_launch_permitted": False,
    }


def run_queue_gate(
    backend: QueueGateBackend,
    contract: AgxContract,
    *,
    cycles: int,
    evidence_dir: Path,
    clock=time.monotonic,
) -> QueueGateResult:
    """Run exactly one assisted queue cycle and always leave Windows blocked."""

    if isinstance(cycles, bool) or not isinstance(cycles, int) or cycles != 1:
        raise QueueGateError("live queue gate requires exactly cycles=1")
    evidence_dir = Path(evidence_dir)
    if evidence_dir.exists() and any(evidence_dir.iterdir()):
        raise QueueGateError("evidence_dir must be fresh and empty")
    evidence_dir.mkdir(parents=True, exist_ok=True)
    evidence_path = evidence_dir / "queue-gate-result.json"
    manifest = _one_shot_manifest(contract)
    record = {"cycle": 1, "status": "running"}
    manifest["cycles"].append(record)
    _atomic_json(evidence_path, manifest)

    snapshot_taken = False
    stop_attempted = False
    reset_attempted = False
    try:
        backend.prepare(contract)
        backend.start()
        record["heartbeat"] = backend.heartbeat()
        backend.configure_context(CONTEXT_ID)

        submit_started = clock()
        completion = backend.submit_barrier(
            QUEUE_INDEX,
            COMPLETION_DEADLINE_S,
        )
        host_elapsed = clock() - submit_started
        record["host_submit_elapsed_s"] = host_elapsed
        if host_elapsed > COMPLETION_DEADLINE_S:
            raise QueueGateError(
                "host submit deadline exceeded: "
                f"{host_elapsed:.6f}s > {COMPLETION_DEADLINE_S:.6f}s"
            )
        record["completion"] = validate_completion(completion)
        record["snapshot"] = backend.snapshot("cycle-complete")
        snapshot_taken = True

        stop_attempted = True
        try:
            backend.stop()
        except Exception as exc:
            record["stop_error"] = str(exc)
            raise QueueGateError(f"stop failed: {exc}") from exc

        reset_attempted = True
        try:
            backend.reset()
        except Exception as exc:
            record["reset_error"] = str(exc)
            raise QueueGateError(f"reset failed: {exc}") from exc

        if not backend.released():
            raise QueueGateError("backend did not release queue ownership")

        record["status"] = "passed"
        manifest["completed_cycles"] = 1
        manifest["verdict"] = "incomplete"
        _atomic_json(evidence_path, manifest)
        return QueueGateResult(1, False, "incomplete", evidence_path)
    except Exception as exc:
        failure = exc if isinstance(exc, QueueGateError) else QueueGateError(
            f"queue backend failure: {exc}"
        )
        if not snapshot_taken:
            try:
                record["snapshot"] = backend.snapshot(str(failure))
            except Exception as snapshot_exc:
                record["snapshot_error"] = str(snapshot_exc)

        if not stop_attempted:
            stop_attempted = True
            try:
                backend.stop()
            except Exception as stop_exc:
                record["stop_error"] = str(stop_exc)

        if not reset_attempted:
            reset_attempted = True
            try:
                backend.reset()
            except Exception as reset_exc:
                record["reset_error"] = str(reset_exc)

        try:
            record["released"] = backend.released()
        except Exception as release_exc:
            record["released"] = False
            record["release_error"] = str(release_exc)

        record["status"] = "failed"
        record["error"] = str(failure)
        manifest["completed_cycles"] = 0
        manifest["verdict"] = "failed"
        manifest["windows_launch_permitted"] = False
        _atomic_json(evidence_path, manifest)
        raise failure


def _canonical_sha256(data: dict) -> str:
    encoded = json.dumps(data, sort_keys=True, separators=(",", ":")).encode()
    import hashlib

    return hashlib.sha256(encoded).hexdigest()


def _read_json(path: Path, description: str) -> dict:
    try:
        data = json.loads(Path(path).read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise QueueGateError(f"cannot read {description}: {path}") from exc
    if not isinstance(data, dict):
        raise QueueGateError(f"invalid {description}: {path}")
    return data


def _validate_one_shot(data: dict, contract_digest: str, index: int) -> dict:
    records = data.get("cycles")
    complete = (
        data.get("queue_gate_version") == GATE_VERSION
        and "gate_version" not in data
        and data.get("contract_sha256") == contract_digest
        and data.get("requested_cycles") == 1
        and data.get("completed_cycles") == 1
        and isinstance(records, list)
        and len(records) == 1
        and isinstance(records[0], dict)
        and records[0].get("cycle") == 1
        and records[0].get("status") == "passed"
        and data.get("verdict") == "incomplete"
        and data.get("windows_launch_permitted") is False
    )
    if not complete:
        raise QueueGateError(
            f"cycle {index} is not a complete G1Q one-shot result"
        )
    validate_completion(records[0].get("completion"))
    try:
        previous_base = records[0]["snapshot"]["firmware"]["m1n1_base"]
    except (KeyError, TypeError) as exc:
        raise QueueGateError(f"cycle {index} has no proxy boot identity") from exc
    if (isinstance(previous_base, bool) or not isinstance(previous_base, int)
            or previous_base <= 0):
        raise QueueGateError(f"cycle {index} has invalid proxy boot identity")
    return records[0]


def _validate_reset_receipt(
    receipt: dict,
    *,
    index: int,
    contract: AgxContract,
    previous_base: int,
    result_digest: str,
) -> dict:
    expected_fields = {
        "reset_receipt_version",
        "cycle",
        "platform",
        "firmware",
        "previous_m1n1_base",
        "m1n1_base",
        "cycle_result_sha256",
        "fresh_proxy",
    }
    fields_match = set(receipt) == expected_fields
    live_base = receipt.get("m1n1_base")
    valid = (
        fields_match
        and receipt.get("reset_receipt_version") == 1
        and receipt.get("cycle") == index
        and receipt.get("platform") == contract.platform
        and receipt.get("firmware") == contract.firmware.version
        and receipt.get("previous_m1n1_base") == previous_base
        and isinstance(live_base, int)
        and not isinstance(live_base, bool)
        and live_base > 0
        and live_base != previous_base
        and receipt.get("cycle_result_sha256") == result_digest
        and receipt.get("fresh_proxy") is True
    )
    if not valid:
        raise QueueGateError(
            f"reset receipt {index} does not prove a fresh proxy boot"
        )
    return copy.deepcopy(receipt)


def aggregate_cold_queue_results(
    evidence_dir: Path,
    contract: AgxContract,
    *,
    cycles: int,
) -> dict:
    """Bind ten one-shot G1Q results to ten fresh proxy reset receipts."""

    if (isinstance(cycles, bool) or not isinstance(cycles, int)
            or cycles != QUALIFICATION_CYCLES):
        raise QueueGateError("cold queue qualification requires exactly 10 cycles")
    evidence_dir = Path(evidence_dir)
    contract_digest = contract_sha256(contract)
    aggregate_cycles = []

    for index in range(1, QUALIFICATION_CYCLES + 1):
        result_path = (
            evidence_dir / f"cycle-{index:02d}" / "queue-gate-result.json"
        )
        data = _read_json(result_path, "G1Q single-cycle result")
        result_digest = _canonical_sha256(data)
        receipt_path = evidence_dir / f"reset-{index:02d}.json"
        receipt = _read_json(receipt_path, "G1Q reset receipt")
        if receipt.get("cycle_result_sha256") != result_digest:
            raise QueueGateError(f"cycle {index} SHA-256 binding mismatch")

        record = _validate_one_shot(data, contract_digest, index)
        previous_base = record["snapshot"]["firmware"]["m1n1_base"]
        bound_receipt = _validate_reset_receipt(
            receipt,
            index=index,
            contract=contract,
            previous_base=previous_base,
            result_digest=result_digest,
        )
        aggregate_record = copy.deepcopy(record)
        aggregate_record["cycle"] = index
        aggregate_record["cycle_result_sha256"] = result_digest
        aggregate_record["reset_receipt"] = bound_receipt
        aggregate_cycles.append(aggregate_record)

    result = {
        "queue_gate_version": AGGREGATE_VERSION,
        "contract_sha256": contract_digest,
        "requested_cycles": QUALIFICATION_CYCLES,
        "completed_cycles": QUALIFICATION_CYCLES,
        "cycles": aggregate_cycles,
        "cold_reset_between_cycles": True,
        "verdict": "passed",
        "windows_launch_permitted": True,
    }
    result["aggregate_sha256"] = _canonical_sha256(result)
    _atomic_json(evidence_dir / "queue-gate-result.json", result)
    return result


def record_queue_proxy_receipt(
    path: Path,
    contract: AgxContract,
    *,
    cycle: int,
    cycle_result: Path,
    live_platform: str,
    live_firmware: str,
    live_m1n1_base: int,
) -> dict:
    """Bind a complete one-shot result to a fresh post-reset proxy identity."""

    if isinstance(cycle, bool) or not isinstance(cycle, int) or cycle <= 0:
        raise QueueGateError("cycle must be a positive integer")
    data = _read_json(cycle_result, "G1Q single-cycle result")
    record = _validate_one_shot(data, contract_sha256(contract), cycle)
    previous_base = record["snapshot"]["firmware"]["m1n1_base"]
    if live_platform != contract.platform:
        raise QueueGateError("reset receipt platform does not match contract")
    if live_firmware != contract.firmware.version:
        raise QueueGateError("reset receipt firmware does not match contract")
    if (isinstance(live_m1n1_base, bool)
            or not isinstance(live_m1n1_base, int)
            or live_m1n1_base <= 0):
        raise QueueGateError("live_m1n1_base must be a positive integer")
    if live_m1n1_base == previous_base:
        raise QueueGateError("reset receipt does not prove a fresh proxy boot")

    receipt = {
        "reset_receipt_version": 1,
        "cycle": cycle,
        "platform": live_platform,
        "firmware": live_firmware,
        "previous_m1n1_base": previous_base,
        "m1n1_base": live_m1n1_base,
        "cycle_result_sha256": _canonical_sha256(data),
        "fresh_proxy": True,
    }
    _atomic_json(Path(path), receipt)
    return receipt


def verify_queue_gate_result(path: Path) -> dict:
    """Return only an intact, complete ten-cold-cycle G1Q aggregate."""

    data = _read_json(path, "G1Q aggregate result")
    expected_digest = data.get("aggregate_sha256")
    unsigned = dict(data)
    unsigned.pop("aggregate_sha256", None)
    if (not isinstance(expected_digest, str)
            or not _SHA256_RE.fullmatch(expected_digest)
            or _canonical_sha256(unsigned) != expected_digest):
        raise QueueGateError("aggregate_sha256 mismatch")

    records = data.get("cycles")
    complete = (
        data.get("queue_gate_version") == AGGREGATE_VERSION
        and data.get("requested_cycles") == QUALIFICATION_CYCLES
        and data.get("completed_cycles") == QUALIFICATION_CYCLES
        and isinstance(records, list)
        and len(records) == QUALIFICATION_CYCLES
        and data.get("cold_reset_between_cycles") is True
        and data.get("verdict") == "passed"
        and data.get("windows_launch_permitted") is True
    )
    if not complete:
        raise QueueGateError("G1Q aggregate does not permit Windows launch")
    for index, record in enumerate(records, 1):
        if (not isinstance(record, dict)
                or record.get("cycle") != index
                or record.get("status") != "passed"
                or record.get("reset_receipt", {}).get("fresh_proxy") is not True):
            raise QueueGateError(f"G1Q aggregate cycle {index} is invalid")
        validate_completion(record.get("completion"))
    return copy.deepcopy(data)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)

    run_one = commands.add_parser("run-one")
    run_one.add_argument("--contract", type=Path, required=True)
    run_one.add_argument("--evidence-dir", type=Path, required=True)

    receipt = commands.add_parser("proxy-receipt")
    receipt.add_argument("--contract", type=Path, required=True)
    receipt.add_argument("--cycle", type=int, required=True)
    receipt.add_argument("--cycle-result", type=Path, required=True)
    receipt.add_argument("--output", type=Path, required=True)

    aggregate = commands.add_parser("aggregate-cold")
    aggregate.add_argument("--contract", type=Path, required=True)
    aggregate.add_argument("--evidence-dir", type=Path, required=True)
    aggregate.add_argument("--cycles", type=int, required=True)

    verify = commands.add_parser("verify-result")
    verify.add_argument("path", type=Path)
    return parser


def _live_proxy():
    device = os.environ.get("M1N1DEVICE", "")
    if not device:
        raise QueueGateError("M1N1DEVICE is required")
    if str(PROXYCLIENT) not in sys.path:
        sys.path.insert(0, str(PROXYCLIENT))
    from m1n1.setup import u

    return u


def main() -> int:
    from tools.agx_contract import load_contract

    args = _parser().parse_args()
    try:
        if args.command == "run-one":
            from tools.agx_m1n1_queue_backend import M1n1AgxQueueBackend

            result = run_queue_gate(
                M1n1AgxQueueBackend(_live_proxy()),
                load_contract(args.contract),
                cycles=1,
                evidence_dir=args.evidence_dir,
            )
            print(result.evidence_path)
        elif args.command == "proxy-receipt":
            contract = load_contract(args.contract)
            u = _live_proxy()
            receipt = record_queue_proxy_receipt(
                args.output,
                contract,
                cycle=args.cycle,
                cycle_result=args.cycle_result,
                live_platform=u.adt.target_type,
                live_firmware=u.version,
                live_m1n1_base=int(u.base),
            )
            print(json.dumps(receipt, indent=2, sort_keys=True))
        elif args.command == "aggregate-cold":
            result = aggregate_cold_queue_results(
                args.evidence_dir,
                load_contract(args.contract),
                cycles=args.cycles,
            )
            print(
                f"aggregated {result['completed_cycles']} cold G1Q cycles; "
                "Windows launch is permitted"
            )
        else:
            result = verify_queue_gate_result(args.path)
            print(
                f"validated {result['completed_cycles']} cold G1Q cycles; "
                "Windows launch is permitted"
            )
    except QueueGateError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
