"""Fail-closed qualification for one private AGX TA-to-3D render."""

import argparse
import copy
from dataclasses import dataclass
import hashlib
import json
import math
import os
from pathlib import Path
import re
import sys
import time
from typing import Protocol

from tools.agx_contract import AgxContract, contract_sha256
from tools.agx_frame_fixture import ValidatedFrame
from tools.agx_proxy_identity import ProxyIdentityError, read_proxy_boot_identity


ROOT = Path(__file__).resolve().parents[1]
PROXYCLIENT = ROOT / "m1n1_windows" / "proxyclient"
RENDER_GATE_VERSION = 2
RENDER_AGGREGATE_VERSION = 2
CONTEXT_ID = 63
PAGE_SIZE = 0x4000
QUEUE_INDEX = 1
COMPLETION_DEADLINE_S = 0.5
QUALIFICATION_CYCLES = 10

_SHA256_RE = re.compile(r"[0-9a-f]{64}\Z")
_COMPLETION_FIELDS = frozenset({
    "context_id", "page_size", "queue_index",
    "ta_command_count", "d3_command_count",
    "ta_producer_before", "ta_producer_after",
    "ta_read_before", "ta_read_after", "ta_done_before", "ta_done_after",
    "d3_producer_before", "d3_producer_after",
    "d3_read_before", "d3_read_after", "d3_done_before", "d3_done_after",
    "wrap_ambiguous", "ta_event_id", "d3_event_id",
    "event_ta_matches", "event_3d_matches", "spurious_events",
    "ta_stamp_before", "ta_stamp_after", "d3_stamp_before", "d3_stamp_after",
    "output_sha256_before", "output_sha256_after",
    "immutable_sha256_before", "immutable_sha256_after",
    "guards_unmapped", "declared_mapping_count", "mapping_classification",
    "unexpected_mappings",
    "firmware_faults", "physical_fault_readable", "physical_fault_value",
    "cleanup_complete", "elapsed_s", "deadline_s",
})
_ONE_SHOT_FIELDS = frozenset({
    "render_gate_version", "contract_sha256", "fixture_sha256",
    "requested_cycles", "completed_cycles", "cycles", "verdict",
    "windows_launch_permitted",
})
_ONE_SHOT_CYCLE_FIELDS = frozenset({
    "cycle", "status", "heartbeat", "host_submit_elapsed_s", "completion",
    "snapshot",
})
_AGGREGATE_FIELDS = frozenset({
    "render_aggregate_version", "contract_sha256", "fixture_sha256",
    "poison_sha256", "expected_output_sha256", "requested_cycles",
    "completed_cycles", "cycles", "cold_reset_between_cycles", "verdict",
    "windows_launch_permitted", "aggregate_sha256",
})
_AGGREGATE_CYCLE_FIELDS = frozenset(
    _ONE_SHOT_CYCLE_FIELDS | {"cycle_result_sha256", "reset_receipt"}
)


class RenderGateError(RuntimeError):
    """Private-render evidence violated the G1R contract."""


class RenderGateBackend(Protocol):
    def prepare(self, contract: AgxContract, fixture: ValidatedFrame) -> None: ...
    def start(self) -> None: ...
    def heartbeat(self) -> dict: ...
    def configure_context(self, context_id: int) -> None: ...
    def submit_frame(self, queue_index: int, timeout_s: float) -> dict: ...
    def snapshot(self, reason: str) -> dict: ...
    def stop(self) -> None: ...
    def reset(self) -> None: ...
    def released(self) -> bool: ...


@dataclass(frozen=True)
class RenderGateResult:
    completed_cycles: int
    windows_launch_permitted: bool
    verdict: str
    evidence_path: Path


def _integer(receipt: dict, field: str, *, minimum: int = 0) -> int:
    value = receipt[field]
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise RenderGateError(f"{field} must be an integer >= {minimum}")
    return value


def _literal(receipt: dict, field: str, expected, boundary: str | None = None) -> None:
    if isinstance(expected, int) and not isinstance(expected, bool):
        _integer(receipt, field)
    if receipt[field] != expected:
        raise RenderGateError(f"{boundary or field} must equal {expected!r}")


def _hash(receipt: dict, field: str, boundary: str) -> str:
    value = receipt[field]
    if not isinstance(value, str) or not _SHA256_RE.fullmatch(value):
        raise RenderGateError(f"{boundary} must be lowercase SHA-256")
    return value


def _progress(receipt: dict, prefix: str, label: str) -> None:
    producer_before = _integer(receipt, f"{prefix}_producer_before")
    producer_after = _integer(receipt, f"{prefix}_producer_after")
    read_before = _integer(receipt, f"{prefix}_read_before")
    read_after = _integer(receipt, f"{prefix}_read_after")
    done_before = _integer(receipt, f"{prefix}_done_before")
    done_after = _integer(receipt, f"{prefix}_done_after")
    if producer_before != 0 or producer_after != 2:
        raise RenderGateError(f"{label} producer must advance 0 -> 2")
    if read_before != 0 or read_after != 2:
        raise RenderGateError(f"{label} read must advance 0 -> 2")
    if done_before != 0 or done_after != 2:
        raise RenderGateError(f"{label} done must advance 0 -> 2")


def validate_render_completion(
    receipt: dict,
    fixture: ValidatedFrame,
) -> dict:
    """Validate exact TA, 3D, event, output, fault and cleanup evidence."""

    if not isinstance(receipt, dict):
        raise RenderGateError("completion receipt must be an object")
    fields = frozenset(receipt)
    if fields != _COMPLETION_FIELDS:
        raise RenderGateError(
            "completion fields mismatch: "
            f"missing={sorted(_COMPLETION_FIELDS - fields)}, "
            f"unknown={sorted(fields - _COMPLETION_FIELDS)}"
        )
    _literal(receipt, "context_id", CONTEXT_ID)
    _literal(receipt, "page_size", PAGE_SIZE)
    _literal(receipt, "queue_index", QUEUE_INDEX)
    _literal(receipt, "ta_command_count", 2, "TA command count")
    _literal(receipt, "d3_command_count", 2, "3D command count")
    _progress(receipt, "ta", "TA")
    _progress(receipt, "d3", "3D")
    if receipt["wrap_ambiguous"] is not False:
        raise RenderGateError("wrap ambiguity must be false")

    ta_event = _integer(receipt, "ta_event_id")
    d3_event = _integer(receipt, "d3_event_id")
    if ta_event == d3_event:
        raise RenderGateError("TA and 3D event IDs must differ")
    _literal(receipt, "event_ta_matches", 1, "TA event match count")
    _literal(receipt, "event_3d_matches", 1, "3D event match count")
    if not isinstance(receipt["spurious_events"], list) or receipt["spurious_events"]:
        raise RenderGateError("spurious events must be an empty list")

    _literal(receipt, "ta_stamp_before", 0x7A000000, "TA stamp before")
    _literal(receipt, "ta_stamp_after", 0x7A000100, "TA stamp after")
    _literal(receipt, "d3_stamp_before", 0x3D000000, "3D stamp before")
    _literal(receipt, "d3_stamp_after", 0x3D000100, "3D stamp after")

    before = _hash(receipt, "output_sha256_before", "poison output hash")
    after = _hash(receipt, "output_sha256_after", "output hash")
    if before != fixture.poison_sha256:
        raise RenderGateError("poison output does not match fixture")
    if after != fixture.expected_output_sha256:
        raise RenderGateError("output does not match fixture oracle")
    immutable_before = _hash(receipt, "immutable_sha256_before", "immutable hash")
    immutable_after = _hash(receipt, "immutable_sha256_after", "immutable hash")
    if immutable_before != immutable_after:
        raise RenderGateError("immutable objects changed during render")

    if receipt["guards_unmapped"] is not True:
        raise RenderGateError("guard mappings must remain unmapped")
    mappings = receipt["mapping_classification"]
    if not isinstance(mappings, list) or not mappings:
        raise RenderGateError("mapping classification must be a nonempty list")
    if _integer(receipt, "declared_mapping_count") != len(mappings):
        raise RenderGateError("declared mapping count must match classification")
    allowed_classes = {"bootstrap", "frame", "renderer", "firmware-shared"}
    required_classes = {"bootstrap", "frame", "renderer"}
    seen_classes = set()
    intervals = []
    for index, mapping in enumerate(mappings):
        if not isinstance(mapping, dict) or set(mapping) != {
            "class", "context_id", "gpu_va", "size"
        }:
            raise RenderGateError(f"mapping classification {index} fields mismatch")
        kind = mapping["class"]
        if kind not in allowed_classes:
            raise RenderGateError(f"mapping class is not allowed: {kind!r}")
        context = mapping["context_id"]
        address = mapping["gpu_va"]
        size = mapping["size"]
        for field, value in (("context_id", context), ("gpu_va", address), ("size", size)):
            if isinstance(value, bool) or not isinstance(value, int) or value < 0:
                raise RenderGateError(f"mapping {field} must be a non-negative integer")
        if context not in (0, CONTEXT_ID):
            raise RenderGateError("mapping context must be 0 or 63")
        if kind == "firmware-shared" and context != 0:
            raise RenderGateError("firmware-shared mapping must use context 0")
        if kind != "firmware-shared" and context != CONTEXT_ID:
            raise RenderGateError(f"{kind} mapping must use context 63")
        if size <= 0 or address % PAGE_SIZE or size % PAGE_SIZE:
            raise RenderGateError("mapping address and size must be 0x4000 aligned")
        end = address + size
        if end <= address or end > 1 << 64:
            raise RenderGateError("mapping range overflows")
        intervals.append((context, address, end))
        seen_classes.add(kind)
    if not required_classes.issubset(seen_classes):
        raise RenderGateError("mapping classification lacks a required class")
    intervals.sort()
    for previous, current in zip(intervals, intervals[1:]):
        if current[0] == previous[0] and current[1] < previous[2]:
            raise RenderGateError("mapping classification contains an overlap")
    if not isinstance(receipt["unexpected_mappings"], list) or receipt["unexpected_mappings"]:
        raise RenderGateError("unexpected mappings must be an empty list")
    if not isinstance(receipt["firmware_faults"], dict) or receipt["firmware_faults"]:
        raise RenderGateError("firmware fault evidence must be empty")
    if not isinstance(receipt["physical_fault_readable"], bool):
        raise RenderGateError("physical fault readable must be boolean")
    if receipt["physical_fault_readable"]:
        if _integer(receipt, "physical_fault_value") != 0:
            raise RenderGateError("physical fault value must be zero")
    elif receipt["physical_fault_value"] is not None:
        raise RenderGateError("physical fault value must be null when unreadable")
    if receipt["cleanup_complete"] is not True:
        raise RenderGateError("frame cleanup must be complete")

    elapsed = receipt["elapsed_s"]
    deadline = receipt["deadline_s"]
    if (
        isinstance(elapsed, bool)
        or not isinstance(elapsed, (int, float))
        or not math.isfinite(elapsed)
        or elapsed < 0
    ):
        raise RenderGateError("elapsed_s must be finite and non-negative")
    if (
        isinstance(deadline, bool)
        or not isinstance(deadline, (int, float))
        or not math.isfinite(deadline)
        or float(deadline) != COMPLETION_DEADLINE_S
    ):
        raise RenderGateError(f"deadline_s must equal {COMPLETION_DEADLINE_S}")
    if float(elapsed) > float(deadline):
        raise RenderGateError("render deadline exceeded")
    return copy.deepcopy(receipt)


def _atomic_json(path: Path, value: dict) -> None:
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")
    temporary.replace(path)


def _canonical_sha256(value: dict) -> str:
    data = json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(data).hexdigest()


def _read_json(path: Path, boundary: str) -> dict:
    try:
        value = json.loads(Path(path).read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise RenderGateError(f"cannot read {boundary}: {path}") from exc
    if not isinstance(value, dict):
        raise RenderGateError(f"invalid {boundary}: {path}")
    return value


def _one_shot_manifest(contract: AgxContract, fixture: ValidatedFrame) -> dict:
    return {
        "render_gate_version": RENDER_GATE_VERSION,
        "contract_sha256": contract_sha256(contract),
        "fixture_sha256": fixture.fixture_sha256,
        "requested_cycles": 1,
        "completed_cycles": 0,
        "cycles": [],
        "verdict": "running",
        "windows_launch_permitted": False,
    }


def run_render_gate(
    backend: RenderGateBackend,
    contract: AgxContract,
    fixture: ValidatedFrame,
    *,
    cycles: int,
    evidence_dir: Path,
    clock=time.monotonic,
) -> RenderGateResult:
    """Run exactly one render cycle and always leave Windows blocked."""

    if isinstance(cycles, bool) or not isinstance(cycles, int) or cycles != 1:
        raise RenderGateError("live render gate requires exactly cycles=1")
    evidence_dir = Path(evidence_dir)
    if evidence_dir.exists() and any(evidence_dir.iterdir()):
        raise RenderGateError("evidence_dir must be fresh and empty")
    evidence_dir.mkdir(parents=True, exist_ok=True)
    path = evidence_dir / "render-gate-result.json"
    manifest = _one_shot_manifest(contract, fixture)
    record = {"cycle": 1, "status": "running"}
    manifest["cycles"].append(record)
    _atomic_json(path, manifest)

    snapshot_taken = False
    stop_attempted = False
    reset_attempted = False
    try:
        backend.prepare(contract, fixture)
        backend.start()
        record["heartbeat"] = backend.heartbeat()
        backend.configure_context(CONTEXT_ID)
        started = clock()
        completion = backend.submit_frame(QUEUE_INDEX, COMPLETION_DEADLINE_S)
        host_elapsed = clock() - started
        record["host_submit_elapsed_s"] = host_elapsed
        if host_elapsed > COMPLETION_DEADLINE_S:
            raise RenderGateError(
                f"host submit deadline exceeded: {host_elapsed:.6f}s"
            )
        record["completion"] = validate_render_completion(completion, fixture)
        record["snapshot"] = backend.snapshot("cycle-complete")
        snapshot_taken = True

        stop_attempted = True
        try:
            backend.stop()
        except Exception as exc:
            record["stop_error"] = str(exc)
            raise RenderGateError(f"stop failed: {exc}") from exc
        reset_attempted = True
        try:
            backend.reset()
        except Exception as exc:
            record["reset_error"] = str(exc)
            raise RenderGateError(f"reset failed: {exc}") from exc
        if not backend.released():
            raise RenderGateError("backend did not release render ownership")
        record["status"] = "passed"
        manifest["completed_cycles"] = 1
        manifest["verdict"] = "incomplete"
        _atomic_json(path, manifest)
        return RenderGateResult(1, False, "incomplete", path)
    except Exception as exc:
        failure = exc if isinstance(exc, RenderGateError) else RenderGateError(
            f"render backend failure: {exc}"
        )
        if not snapshot_taken:
            try:
                record["snapshot"] = backend.snapshot(str(failure))
            except Exception as snapshot_exc:
                record["snapshot_error"] = str(snapshot_exc)
        if not stop_attempted:
            try:
                backend.stop()
            except Exception as stop_exc:
                record["stop_error"] = str(stop_exc)
        if not reset_attempted:
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
        _atomic_json(path, manifest)
        raise failure


def _validate_one_shot(
    data: dict,
    contract: AgxContract,
    fixture: ValidatedFrame,
    index: int,
) -> dict:
    records = data.get("cycles")
    valid = (
        frozenset(data) == _ONE_SHOT_FIELDS
        and data.get("render_gate_version") == RENDER_GATE_VERSION
        and data.get("contract_sha256") == contract_sha256(contract)
        and data.get("fixture_sha256") == fixture.fixture_sha256
        and data.get("requested_cycles") == 1
        and data.get("completed_cycles") == 1
        and isinstance(records, list) and len(records) == 1
        and isinstance(records[0], dict)
        and frozenset(records[0]) == _ONE_SHOT_CYCLE_FIELDS
        and records[0].get("cycle") == 1
        and records[0].get("status") == "passed"
        and data.get("verdict") == "incomplete"
        and data.get("windows_launch_permitted") is False
    )
    if not valid:
        raise RenderGateError(f"cycle {index} is not a complete G1R one-shot result")
    validate_render_completion(records[0].get("completion"), fixture)
    try:
        firmware = records[0]["snapshot"]["firmware"]
        base = firmware["m1n1_base"]
        cookie = firmware["boot_cookie"]
        proxy = firmware["proxy_identity"]
    except (KeyError, TypeError) as exc:
        raise RenderGateError(f"cycle {index} has no proxy boot identity") from exc
    if isinstance(base, bool) or not isinstance(base, int) or base <= 0:
        raise RenderGateError(f"cycle {index} has invalid m1n1 base")
    if isinstance(cookie, bool) or not isinstance(cookie, int) or cookie <= 0:
        raise RenderGateError(f"cycle {index} has invalid boot cookie")
    if not isinstance(proxy, str) or not proxy:
        raise RenderGateError(f"cycle {index} has invalid proxy identity")
    return copy.deepcopy(records[0])


def _validate_reset(
    receipt: dict,
    *,
    index: int,
    contract: AgxContract,
    previous_proxy: str,
    previous_cookie: int,
    previous_base: int,
    result_sha256: str,
) -> dict:
    fields = {
        "render_reset_receipt_version", "cycle", "platform", "firmware",
        "previous_proxy_identity", "proxy_identity",
        "previous_boot_cookie", "boot_cookie",
        "previous_m1n1_base", "m1n1_base", "cycle_result_sha256", "fresh_proxy",
    }
    proxy = receipt.get("proxy_identity")
    cookie = receipt.get("boot_cookie")
    base = receipt.get("m1n1_base")
    valid = (
        set(receipt) == fields
        and receipt.get("render_reset_receipt_version") == 2
        and receipt.get("cycle") == index
        and receipt.get("platform") == contract.platform
        and receipt.get("firmware") == contract.firmware.version
        and receipt.get("previous_proxy_identity") == previous_proxy
        and isinstance(proxy, str) and proxy and proxy != previous_proxy
        and receipt.get("previous_boot_cookie") == previous_cookie
        and isinstance(cookie, int) and not isinstance(cookie, bool)
        and cookie > 0 and cookie != previous_cookie
        and receipt.get("previous_m1n1_base") == previous_base
        and isinstance(base, int) and not isinstance(base, bool)
        and base > 0 and base != previous_base
        and receipt.get("cycle_result_sha256") == result_sha256
        and receipt.get("fresh_proxy") is True
    )
    if not valid:
        raise RenderGateError(f"reset receipt {index} does not prove a fresh proxy boot")
    return copy.deepcopy(receipt)


def record_render_proxy_receipt(
    path: Path,
    contract: AgxContract,
    fixture: ValidatedFrame,
    *,
    cycle: int,
    cycle_result: Path,
    live_platform: str,
    live_firmware: str,
    live_proxy_identity: str,
    live_boot_cookie: int,
    live_m1n1_base: int,
) -> dict:
    """Bind one complete render result to a fresh post-reset proxy boot."""

    if isinstance(cycle, bool) or not isinstance(cycle, int) or cycle <= 0:
        raise RenderGateError("cycle must be a positive integer")
    data = _read_json(cycle_result, "G1R one-shot result")
    record = _validate_one_shot(data, contract, fixture, cycle)
    previous = record["snapshot"]["firmware"]
    if live_platform != contract.platform:
        raise RenderGateError("reset platform does not match contract")
    if live_firmware != contract.firmware.version:
        raise RenderGateError("reset firmware does not match contract")
    if not isinstance(live_proxy_identity, str) or not live_proxy_identity:
        raise RenderGateError("live proxy identity must be nonempty")
    if (
        isinstance(live_boot_cookie, bool)
        or not isinstance(live_boot_cookie, int)
        or live_boot_cookie <= 0
    ):
        raise RenderGateError("live boot cookie must be a positive integer")
    if (
        isinstance(live_m1n1_base, bool)
        or not isinstance(live_m1n1_base, int)
        or live_m1n1_base <= 0
    ):
        raise RenderGateError("live m1n1 base must be a positive integer")
    if (
        live_proxy_identity == previous["proxy_identity"]
        or live_boot_cookie == previous["boot_cookie"]
    ):
        raise RenderGateError("reset receipt does not prove a fresh proxy boot")
    receipt = {
        "render_reset_receipt_version": 2,
        "cycle": cycle,
        "platform": live_platform,
        "firmware": live_firmware,
        "previous_proxy_identity": previous["proxy_identity"],
        "proxy_identity": live_proxy_identity,
        "previous_boot_cookie": previous["boot_cookie"],
        "boot_cookie": live_boot_cookie,
        "previous_m1n1_base": previous["m1n1_base"],
        "m1n1_base": live_m1n1_base,
        "cycle_result_sha256": _canonical_sha256(data),
        "fresh_proxy": True,
    }
    _atomic_json(Path(path), receipt)
    return receipt


def aggregate_cold_render_results(
    evidence_dir: Path,
    contract: AgxContract,
    fixture: ValidatedFrame,
    *,
    cycles: int,
) -> dict:
    """Bind ten complete one-shot renders to ten physical reboot receipts."""

    if isinstance(cycles, bool) or not isinstance(cycles, int) or cycles != 10:
        raise RenderGateError("cold render qualification requires exactly 10 cycles")
    evidence_dir = Path(evidence_dir)
    loaded = []
    proxies = []
    cookies = []
    bases = []
    for index in range(1, QUALIFICATION_CYCLES + 1):
        path = evidence_dir / f"cycle-{index:02d}" / "render-gate-result.json"
        data = _read_json(path, "G1R one-shot result")
        record = _validate_one_shot(data, contract, fixture, index)
        firmware = record["snapshot"]["firmware"]
        proxies.append(firmware["proxy_identity"])
        cookies.append(firmware["boot_cookie"])
        bases.append(firmware["m1n1_base"])
        loaded.append((data, record, _canonical_sha256(data)))
    if len(set(cookies)) != QUALIFICATION_CYCLES:
        raise RenderGateError("ten distinct boot cookies are required")
    if len(set(proxies)) != QUALIFICATION_CYCLES:
        raise RenderGateError("ten distinct proxy identities are required")

    aggregate_cycles = []
    previous_reset = None
    for index, (data, record, digest) in enumerate(loaded, 1):
        if previous_reset is not None:
            firmware = record["snapshot"]["firmware"]
            if (
                firmware["proxy_identity"] != previous_reset["proxy_identity"]
                or firmware["boot_cookie"] != previous_reset["boot_cookie"]
            ):
                raise RenderGateError(f"cycle {index} is not bound to reset {index - 1}")
        receipt = _read_json(
            evidence_dir / f"reset-{index:02d}.json", "G1R reset receipt"
        )
        if receipt.get("cycle_result_sha256") != digest:
            raise RenderGateError(f"cycle {index} SHA-256 binding mismatch")
        firmware = record["snapshot"]["firmware"]
        bound = _validate_reset(
            receipt,
            index=index,
            contract=contract,
            previous_proxy=firmware["proxy_identity"],
            previous_cookie=firmware["boot_cookie"],
            previous_base=firmware["m1n1_base"],
            result_sha256=digest,
        )
        item = copy.deepcopy(record)
        item["cycle"] = index
        item["cycle_result_sha256"] = digest
        item["reset_receipt"] = bound
        aggregate_cycles.append(item)
        previous_reset = bound

    result = {
        "render_aggregate_version": RENDER_AGGREGATE_VERSION,
        "contract_sha256": contract_sha256(contract),
        "fixture_sha256": fixture.fixture_sha256,
        "poison_sha256": fixture.poison_sha256,
        "expected_output_sha256": fixture.expected_output_sha256,
        "requested_cycles": QUALIFICATION_CYCLES,
        "completed_cycles": QUALIFICATION_CYCLES,
        "cycles": aggregate_cycles,
        "cold_reset_between_cycles": True,
        "verdict": "passed",
        "windows_launch_permitted": True,
    }
    result["aggregate_sha256"] = _canonical_sha256(result)
    _atomic_json(evidence_dir / "render-gate-result.json", result)
    return result


def verify_render_gate_result(path: Path) -> dict:
    """Return only an intact ten-cold-cycle private-render aggregate."""

    data = _read_json(path, "G1R aggregate")
    if frozenset(data) != _AGGREGATE_FIELDS:
        raise RenderGateError("aggregate fields mismatch")
    digest = data.get("aggregate_sha256")
    unsigned = dict(data)
    unsigned.pop("aggregate_sha256", None)
    if (
        not isinstance(digest, str)
        or not _SHA256_RE.fullmatch(digest)
        or _canonical_sha256(unsigned) != digest
    ):
        raise RenderGateError("aggregate_sha256 mismatch")
    records = data.get("cycles")
    valid = (
        data.get("render_aggregate_version") == RENDER_AGGREGATE_VERSION
        and data.get("requested_cycles") == QUALIFICATION_CYCLES
        and data.get("completed_cycles") == QUALIFICATION_CYCLES
        and isinstance(records, list) and len(records) == QUALIFICATION_CYCLES
        and data.get("cold_reset_between_cycles") is True
        and data.get("verdict") == "passed"
        and data.get("windows_launch_permitted") is True
    )
    if not valid:
        raise RenderGateError("G1R aggregate does not permit Windows launch")
    poison = data.get("poison_sha256")
    expected = data.get("expected_output_sha256")
    if not isinstance(poison, str) or not _SHA256_RE.fullmatch(poison):
        raise RenderGateError("aggregate poison hash is invalid")
    if not isinstance(expected, str) or not _SHA256_RE.fullmatch(expected):
        raise RenderGateError("aggregate output hash is invalid")
    proxy_ids = []
    boot_cookies = []
    stub = ValidatedFrame(
        fixture_sha256=data.get("fixture_sha256"), command_buffer={}, objects=(),
        output_gpu_va=0, output_size=PAGE_SIZE,
        poison_sha256=poison, expected_output_sha256=expected,
    )
    for index, record in enumerate(records, 1):
        if (
            not isinstance(record, dict)
            or frozenset(record) != _AGGREGATE_CYCLE_FIELDS
            or record.get("cycle") != index
        ):
            raise RenderGateError(f"G1R aggregate cycle {index} is invalid")
        validate_render_completion(record.get("completion"), stub)
        try:
            firmware = record["snapshot"]["firmware"]
            proxy_ids.append(firmware["proxy_identity"])
            boot_cookies.append(firmware["boot_cookie"])
        except (KeyError, TypeError) as exc:
            raise RenderGateError(f"G1R cycle {index} has no boot identity") from exc
        if record.get("reset_receipt", {}).get("fresh_proxy") is not True:
            raise RenderGateError(f"G1R cycle {index} has no cold reset receipt")
    if len(set(boot_cookies)) != 10 or len(set(proxy_ids)) != 10:
        raise RenderGateError("G1R aggregate reuses a boot cookie or proxy identity")
    return copy.deepcopy(data)


def _validate_fixture_identity(contract, identity: dict) -> None:
    expected = {
        "board": contract.platform,
        "chip_generation": contract.firmware.generation,
        "firmware_version": contract.firmware.version,
        "m1n1_commit": contract.source.fixture_m1n1_commit,
        "adt_sha256": contract.source.adt_identity,
    }
    if not isinstance(identity, dict) or any(
        identity.get(field) != value for field, value in expected.items()
    ):
        raise RenderGateError("fixture identity does not match AGX contract")


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    preflight = commands.add_parser("preflight-fixture")
    preflight.add_argument("--contract", type=Path, required=True)
    preflight.add_argument("--frame", type=Path, required=True)
    preflight.add_argument("--manifest", type=Path, required=True)
    preflight.add_argument("--identity", type=Path, required=True)
    run_one = commands.add_parser("run-one")
    run_one.add_argument("--contract", type=Path, required=True)
    run_one.add_argument("--frame", type=Path, required=True)
    run_one.add_argument("--manifest", type=Path, required=True)
    run_one.add_argument("--identity", type=Path, required=True)
    run_one.add_argument("--evidence-dir", type=Path, required=True)
    receipt = commands.add_parser("proxy-receipt")
    receipt.add_argument("--contract", type=Path, required=True)
    receipt.add_argument("--frame", type=Path, required=True)
    receipt.add_argument("--manifest", type=Path, required=True)
    receipt.add_argument("--identity", type=Path, required=True)
    receipt.add_argument("--cycle-result", type=Path, required=True)
    receipt.add_argument("--cycle", type=int, required=True)
    receipt.add_argument("--output", type=Path, required=True)
    aggregate = commands.add_parser("aggregate-cold")
    aggregate.add_argument("--contract", type=Path, required=True)
    aggregate.add_argument("--frame", type=Path, required=True)
    aggregate.add_argument("--manifest", type=Path, required=True)
    aggregate.add_argument("--identity", type=Path, required=True)
    aggregate.add_argument("--evidence-dir", type=Path, required=True)
    aggregate.add_argument("--cycles", type=int, required=True)
    verify = commands.add_parser("verify-result")
    verify.add_argument("path", type=Path)
    return parser


def main(argv=None) -> int:
    args = _parser().parse_args(argv)
    try:
        if args.command == "verify-result":
            result = verify_render_gate_result(args.path)
            print(
                f"validated {result['completed_cycles']} cold G1R cycles; "
                "Windows launch is permitted"
            )
            return 0
        from tools.agx_contract import load_contract
        from tools.agx_frame_fixture import (
            FixtureError,
            require_canonical_fixture,
            validate_fixture,
        )

        contract = load_contract(args.contract)
        try:
            identity = _read_json(args.identity, "fixture identity")
            _validate_fixture_identity(contract, identity)
            fixture = validate_fixture(args.frame, args.manifest, identity)
            if args.command == "preflight-fixture":
                require_canonical_fixture(args.frame)
        except FixtureError as exc:
            raise RenderGateError(f"fixture validation failed: {exc}") from exc
        if args.command == "preflight-fixture":
            print(json.dumps({
                "contract": str(args.contract),
                "fixture_sha256": fixture.fixture_sha256,
                "expected_output_sha256": fixture.expected_output_sha256,
                "context": 63,
                "queue": 1,
                "deadline_s": COMPLETION_DEADLINE_S,
            }, indent=2, sort_keys=True))
            return 0
        if args.command == "aggregate-cold":
            result = aggregate_cold_render_results(
                args.evidence_dir, contract, fixture, cycles=args.cycles
            )
            print(
                f"aggregated {result['completed_cycles']} cold G1R cycles; "
                "Windows launch is permitted"
            )
            return 0
        if args.command == "run-one":
            from tools.agx_m1n1_render_backend import M1n1AgxRenderBackend

            result = run_render_gate(
                M1n1AgxRenderBackend(_live_proxy()),
                contract,
                fixture,
                cycles=1,
                evidence_dir=args.evidence_dir,
            )
            print(result.evidence_path)
            return 0
        u = _live_proxy()
        try:
            live_identity = read_proxy_boot_identity(u)
        except ProxyIdentityError as exc:
            raise RenderGateError(str(exc)) from exc
        receipt = record_render_proxy_receipt(
            args.output,
            contract,
            fixture,
            cycle=args.cycle,
            cycle_result=args.cycle_result,
            live_platform=live_identity.platform,
            live_firmware=live_identity.firmware,
            live_proxy_identity=live_identity.proxy_identity,
            live_boot_cookie=live_identity.boot_cookie,
            live_m1n1_base=live_identity.m1n1_base,
        )
        print(json.dumps(receipt, indent=2, sort_keys=True))
        return 0
    except RenderGateError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


def _live_proxy():
    if not os.environ.get("M1N1DEVICE"):
        raise RenderGateError("M1N1DEVICE is required")
    if str(PROXYCLIENT) not in sys.path:
        sys.path.insert(0, str(PROXYCLIENT))
    from m1n1.setup import u

    return u


if __name__ == "__main__":
    raise SystemExit(main())
