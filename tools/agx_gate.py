"""Fail-closed lifecycle gate for bounded J313 AGX firmware experiments."""

from dataclasses import dataclass
import json
from pathlib import Path
import time
from typing import Protocol

from tools.agx_contract import AgxContract, contract_sha256


class GateError(RuntimeError):
    """The bounded AGX experiment did not return to a safe released state."""


class GateBackend(Protocol):
    def prepare(self, contract: AgxContract) -> None: ...

    def start(self) -> None: ...

    def heartbeat(self) -> dict: ...

    def snapshot(self, reason: str) -> dict: ...

    def stop(self) -> None: ...

    def reset(self) -> None: ...

    def released(self) -> bool: ...


@dataclass(frozen=True)
class GateResult:
    completed_cycles: int
    windows_launch_permitted: bool
    verdict: str
    evidence_path: Path


def _atomic_json(path: Path, data: dict) -> None:
    encoded = json.dumps(data, indent=2, sort_keys=True) + "\n"
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(encoded)
    temporary.replace(path)


def _manifest(contract: AgxContract, cycles: int, timeout_s: float) -> dict:
    return {
        "gate_version": 1,
        "contract_sha256": contract_sha256(contract),
        "requested_cycles": cycles,
        "timeout_s": timeout_s,
        "completed_cycles": 0,
        "cycles": [],
        "verdict": "running",
        "windows_launch_permitted": False,
    }


def _valid_positive_integer(value, name: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
        raise GateError(f"{name} must be a positive integer")
    return value


def _valid_timeout(value) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or value <= 0:
        raise GateError("timeout_s must be positive")
    return float(value)


def run_gate(
    backend: GateBackend,
    contract: AgxContract,
    cycles: int,
    timeout_s: float,
    evidence_dir: Path,
    clock=time.monotonic,
) -> GateResult:
    """Run bounded ownership cycles and permit Windows only after ten passes."""

    cycles = _valid_positive_integer(cycles, "cycles")
    timeout_s = _valid_timeout(timeout_s)
    evidence_dir = Path(evidence_dir)
    evidence_dir.mkdir(parents=True, exist_ok=True)
    evidence_path = evidence_dir / "gate-result.json"
    manifest = _manifest(contract, cycles, timeout_s)
    _atomic_json(evidence_path, manifest)

    completed = 0
    for index in range(1, cycles + 1):
        record = {"cycle": index, "status": "running"}
        manifest["cycles"].append(record)
        started = False
        stopped = False
        reset_done = False
        snapshot_taken = False
        try:
            backend.prepare(contract)
            backend.start()
            started = True

            heartbeat_started = clock()
            record["heartbeat"] = backend.heartbeat()
            heartbeat_elapsed = clock() - heartbeat_started
            record["heartbeat_elapsed_s"] = heartbeat_elapsed
            if heartbeat_elapsed > timeout_s:
                raise GateError(
                    f"heartbeat deadline exceeded in cycle {index}: "
                    f"{heartbeat_elapsed:.6f}s > {timeout_s:.6f}s"
                )

            record["snapshot"] = backend.snapshot("cycle-complete")
            snapshot_taken = True
            backend.stop()
            stopped = True
            backend.reset()
            reset_done = True
            if not backend.released():
                raise GateError(f"backend did not release ownership in cycle {index}")

            completed += 1
            record["status"] = "passed"
            manifest["completed_cycles"] = completed
            _atomic_json(evidence_path, manifest)
        except Exception as exc:
            failure = exc if isinstance(exc, GateError) else GateError(
                f"backend failure in cycle {index}: {exc}"
            )
            if not snapshot_taken:
                try:
                    record["snapshot"] = backend.snapshot(str(failure))
                except Exception as snapshot_exc:
                    record["snapshot_error"] = str(snapshot_exc)
            if started and not stopped:
                try:
                    backend.stop()
                    stopped = True
                except Exception as stop_exc:
                    record["stop_error"] = str(stop_exc)
            if not reset_done:
                try:
                    backend.reset()
                    reset_done = True
                except Exception as reset_exc:
                    record["reset_error"] = str(reset_exc)
            try:
                record["released"] = backend.released()
            except Exception as release_exc:
                record["released"] = False
                record["release_error"] = str(release_exc)

            record["status"] = "failed"
            record["error"] = str(failure)
            manifest["completed_cycles"] = completed
            manifest["verdict"] = "failed"
            manifest["windows_launch_permitted"] = False
            _atomic_json(evidence_path, manifest)
            raise failure

    final_released = backend.released()
    if not final_released:
        manifest["verdict"] = "failed"
        manifest["windows_launch_permitted"] = False
        _atomic_json(evidence_path, manifest)
        raise GateError("backend did not release ownership after final cycle")

    permitted = cycles == 10 and completed == 10
    verdict = "passed" if permitted else "incomplete"
    manifest["verdict"] = verdict
    manifest["windows_launch_permitted"] = permitted
    _atomic_json(evidence_path, manifest)
    return GateResult(completed, permitted, verdict, evidence_path)
