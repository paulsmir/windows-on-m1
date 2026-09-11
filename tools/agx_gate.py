"""Fail-closed lifecycle gate for bounded J313 AGX firmware experiments."""

import argparse
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
from typing import Protocol


ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))
PROXYCLIENT = ROOT / "m1n1_windows" / "proxyclient"
if str(PROXYCLIENT) not in sys.path:
    sys.path.insert(0, str(PROXYCLIENT))

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


SHA256_RE = re.compile(r"[0-9a-f]{64}\Z")


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


def verify_gate_result(path: Path) -> dict:
    """Return a complete cold-reset ten-cycle result or block Windows."""

    try:
        data = json.loads(Path(path).read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise GateError(f"cannot read gate result: {path}") from exc
    cycles = data.get("cycles")
    complete = (
        data.get("gate_version") == 2
        and data.get("requested_cycles") == 10
        and data.get("completed_cycles") == 10
        and isinstance(cycles, list)
        and len(cycles) == 10
        and all(
            item.get("cycle") == index and item.get("status") == "passed"
            for index, item in enumerate(cycles, 1)
        )
        and data.get("verdict") == "passed"
        and data.get("windows_launch_permitted") is True
        and data.get("cold_reset_between_cycles") is True
        and all(item.get("reset_receipt", {}).get("fresh_proxy") is True
                for item in cycles)
    )
    if not complete:
        raise GateError("G1 result does not permit Windows launch")
    return data


def _read_json(path: Path, description: str) -> dict:
    try:
        data = json.loads(Path(path).read_text())
    except (OSError, json.JSONDecodeError) as exc:
        raise GateError(f"cannot read {description}: {path}") from exc
    if not isinstance(data, dict):
        raise GateError(f"invalid {description}: {path}")
    return data


def record_proxy_receipt(
    path: Path,
    contract: AgxContract,
    *,
    cycle: int,
    previous_m1n1_base: int,
    live_platform: str,
    live_firmware: str,
    live_m1n1_base: int,
) -> dict:
    """Record a fresh post-cycle proxy identity or reject the reset boundary."""

    cycle = _valid_positive_integer(cycle, "cycle")
    for value, name in (
        (previous_m1n1_base, "previous_m1n1_base"),
        (live_m1n1_base, "live_m1n1_base"),
    ):
        if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
            raise GateError(f"{name} must be a positive integer")
    if live_platform != contract.platform:
        raise GateError("reset receipt platform does not match contract")
    if live_firmware != contract.firmware.version:
        raise GateError("reset receipt firmware does not match contract")
    if live_m1n1_base == previous_m1n1_base:
        raise GateError("reset receipt does not prove a fresh proxy boot")

    receipt = {
        "reset_receipt_version": 1,
        "cycle": cycle,
        "platform": live_platform,
        "firmware": live_firmware,
        "previous_m1n1_base": previous_m1n1_base,
        "m1n1_base": live_m1n1_base,
        "fresh_proxy": True,
    }
    _atomic_json(Path(path), receipt)
    return receipt


def aggregate_cold_results(
    evidence_dir: Path,
    contract: AgxContract,
    *,
    cycles: int,
) -> dict:
    """Permit Windows only after ten one-shot cycles and ten fresh proxies."""

    if cycles != 10:
        raise GateError("cold qualification requires exactly 10 cycles")
    evidence_dir = Path(evidence_dir)
    contract_digest = contract_sha256(contract)
    aggregate_cycles = []

    for index in range(1, cycles + 1):
        cycle_path = evidence_dir / f"cycle-{index:02d}" / "gate-result.json"
        data = _read_json(cycle_path, "single-cycle result")
        records = data.get("cycles")
        valid_cycle = (
            data.get("gate_version") == 1
            and data.get("contract_sha256") == contract_digest
            and data.get("requested_cycles") == 1
            and data.get("completed_cycles") == 1
            and isinstance(records, list)
            and len(records) == 1
            and records[0].get("cycle") == 1
            and records[0].get("status") == "passed"
            and data.get("verdict") == "incomplete"
            and data.get("windows_launch_permitted") is False
        )
        if not valid_cycle:
            raise GateError(f"cycle {index} is not a complete one-shot result")
        try:
            previous_base = int(
                records[0]["snapshot"]["firmware"]["m1n1_base"]
            )
        except (KeyError, TypeError, ValueError) as exc:
            raise GateError(f"cycle {index} has no proxy boot identity") from exc

        receipt_path = evidence_dir / f"reset-{index:02d}.json"
        receipt = _read_json(receipt_path, "reset receipt")
        valid_receipt = (
            receipt.get("reset_receipt_version") == 1
            and receipt.get("cycle") == index
            and receipt.get("platform") == contract.platform
            and receipt.get("firmware") == contract.firmware.version
            and receipt.get("previous_m1n1_base") == previous_base
            and isinstance(receipt.get("m1n1_base"), int)
            and receipt.get("m1n1_base") != previous_base
        )
        if not valid_receipt:
            raise GateError(f"reset receipt {index} does not prove a fresh proxy boot")

        record = dict(records[0])
        record["cycle"] = index
        record["reset_receipt"] = dict(receipt, fresh_proxy=True)
        aggregate_cycles.append(record)

    result = {
        "gate_version": 2,
        "contract_sha256": contract_digest,
        "requested_cycles": cycles,
        "completed_cycles": cycles,
        "cycles": aggregate_cycles,
        "cold_reset_between_cycles": True,
        "verdict": "passed",
        "windows_launch_permitted": True,
    }
    _atomic_json(evidence_dir / "gate-result.json", result)
    return result


def _git(root: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(root), *args],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode:
        raise GateError(result.stderr.strip() or f"git failed in {root}")
    return result.stdout.strip()


def _parse_checksums(path: Path) -> dict[str, str]:
    try:
        lines = path.read_text().splitlines()
    except OSError as exc:
        raise GateError(f"Stable checksum list not found: {path}") from exc
    result = {}
    for line in lines:
        fields = line.split("  ", 1)
        if len(fields) != 2 or not SHA256_RE.fullmatch(fields[0]):
            raise GateError(f"invalid stable checksum line: {line}")
        name = fields[1]
        if not name or Path(name).name != name or name in result:
            raise GateError(f"invalid stable checksum artifact: {name}")
        result[name] = fields[0]
    if not result:
        raise GateError("stable checksum list is empty")
    return result


def preflight_operator(root: Path, contract_path: Path, artifact_dir: Path) -> dict:
    """Validate immutable G0, source, and recovery identities before proxy use."""

    from tools.agx_contract import canonical_bytes, load_contract
    from tools.agx_live_inventory import ensure_guest_inactive
    from tools.artifact_manifest import ARTIFACT_ROLES, verify_manifest

    root = Path(root).resolve()
    contract_path = Path(contract_path).resolve()
    artifact_dir = Path(artifact_dir).resolve()
    ensure_guest_inactive(root)

    if not contract_path.is_file():
        raise GateError(f"AGX contract not found: {contract_path}")
    contract = load_contract(contract_path)
    if contract_path.read_bytes() != canonical_bytes(contract):
        raise GateError("AGX contract is not canonical")

    manifest_path = artifact_dir / "MANIFEST.json"
    if not manifest_path.is_file():
        raise GateError(f"Artifact manifest not found: {manifest_path}")
    try:
        manifest = verify_manifest(
            manifest_path,
            expected_profile="debug",
            expected_roles=ARTIFACT_ROLES,
            expected_display="both",
            expected_debug="monitor",
        )
    except Exception as exc:
        raise GateError(str(exc)) from exc

    artifacts = manifest.get("artifacts", {})
    if set(artifacts) != set(ARTIFACT_ROLES):
        raise GateError("stable recovery manifest has an unexpected artifact set")
    allowed_entries = set(artifacts) | {"MANIFEST.json", "SHA256SUMS"}
    actual_entries = {path.name for path in artifact_dir.iterdir()}
    unexpected = sorted(actual_entries - allowed_entries)
    missing = sorted(allowed_entries - actual_entries)
    if unexpected:
        raise GateError(f"unexpected recovery entry: {unexpected[0]}")
    if missing:
        raise GateError(f"missing recovery entry: {missing[0]}")

    checksums = _parse_checksums(artifact_dir / "SHA256SUMS")
    if set(checksums) != set(artifacts):
        raise GateError("stable checksum artifact set does not match manifest")
    for name, digest in checksums.items():
        if artifacts[name].get("sha256") != digest:
            raise GateError(f"stable checksum mismatch: {name}")

    m1n1_commit = _git(root / "m1n1_windows", "rev-parse", "HEAD")
    mu_commit = _git(root / "mu", "rev-parse", "HEAD")
    if m1n1_commit != contract.source.m1n1_commit:
        raise GateError("m1n1 source commit does not match AGX contract")
    if mu_commit != contract.source.mu_commit:
        raise GateError("Mu source commit does not match AGX contract")
    _git(root, "cat-file", "-e", f"{contract.source.root_commit}^{{commit}}")
    if manifest.get("m1n1_windows_commit") != contract.source.m1n1_commit:
        raise GateError("stable m1n1 artifact source does not match AGX contract")

    return {
        "contract": str(contract_path),
        "contract_sha256": contract_sha256(contract),
        "artifact_dir": str(artifact_dir),
        "m1n1_sha256": artifacts["m1n1.macho"]["sha256"],
        "firmware_sha256": artifacts["J313_EFI.fd"]["sha256"],
        "m1n1_commit": m1n1_commit,
        "mu_commit": mu_commit,
        "recovery_checksums": checksums,
    }


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    preflight = subparsers.add_parser("preflight")
    preflight.add_argument("--root", type=Path, required=True)
    preflight.add_argument("--contract", type=Path, required=True)
    preflight.add_argument("--artifact-dir", type=Path, required=True)
    run = subparsers.add_parser("run")
    run.add_argument("--contract", type=Path, required=True)
    run.add_argument("--evidence-dir", type=Path, required=True)
    run.add_argument("--cycles", type=int, required=True)
    run.add_argument("--timeout", type=float, default=1.0)
    run_one = subparsers.add_parser("run-one")
    run_one.add_argument("--contract", type=Path, required=True)
    run_one.add_argument("--evidence-dir", type=Path, required=True)
    run_one.add_argument("--timeout", type=float, default=1.0)
    receipt = subparsers.add_parser("proxy-receipt")
    receipt.add_argument("--contract", type=Path, required=True)
    receipt.add_argument("--cycle", type=int, required=True)
    receipt.add_argument("--cycle-result", type=Path, required=True)
    receipt.add_argument("--output", type=Path, required=True)
    aggregate = subparsers.add_parser("aggregate-cold")
    aggregate.add_argument("--contract", type=Path, required=True)
    aggregate.add_argument("--evidence-dir", type=Path, required=True)
    aggregate.add_argument("--cycles", type=int, required=True)
    verify = subparsers.add_parser("verify-result")
    verify.add_argument("path", type=Path)
    return parser


def main() -> int:
    parser = _parser()
    args = parser.parse_args()
    try:
        if args.command == "preflight":
            data = preflight_operator(args.root, args.contract, args.artifact_dir)
            print(json.dumps(data, indent=2, sort_keys=True))
        elif args.command == "verify-result":
            data = verify_gate_result(args.path)
            print(
                f"validated {data['completed_cycles']} AGX cycles; "
                "Windows launch is permitted"
            )
        elif args.command in ("run", "run-one"):
            cycles = args.cycles if args.command == "run" else 1
            if args.command == "run" and args.cycles != 10:
                raise GateError("cycles must be exactly 10")
            os.environ.setdefault("M1N1DEVICE", "")
            if not os.environ["M1N1DEVICE"]:
                raise GateError("M1N1DEVICE is required")
            from m1n1.setup import u
            from tools.agx_contract import load_contract
            from tools.agx_m1n1_backend import M1n1AgxBackend

            result = run_gate(
                M1n1AgxBackend(u),
                load_contract(args.contract),
                cycles=cycles,
                timeout_s=args.timeout,
                evidence_dir=args.evidence_dir,
            )
            print(result.evidence_path)
        elif args.command == "proxy-receipt":
            os.environ.setdefault("M1N1DEVICE", "")
            if not os.environ["M1N1DEVICE"]:
                raise GateError("M1N1DEVICE is required")
            cycle_data = _read_json(args.cycle_result, "single-cycle result")
            try:
                previous_base = int(
                    cycle_data["cycles"][0]["snapshot"]["firmware"]["m1n1_base"]
                )
            except (KeyError, IndexError, TypeError, ValueError) as exc:
                raise GateError("single-cycle result has no proxy boot identity") from exc
            from m1n1.setup import u
            from tools.agx_contract import load_contract

            receipt = record_proxy_receipt(
                args.output,
                load_contract(args.contract),
                cycle=args.cycle,
                previous_m1n1_base=previous_base,
                live_platform=u.adt.target_type,
                live_firmware=u.version,
                live_m1n1_base=int(u.base),
            )
            print(json.dumps(receipt, indent=2, sort_keys=True))
        else:
            from tools.agx_contract import load_contract

            data = aggregate_cold_results(
                args.evidence_dir,
                load_contract(args.contract),
                cycles=args.cycles,
            )
            print(
                f"aggregated {data['completed_cycles']} cold AGX cycles; "
                "Windows launch is permitted"
            )
    except GateError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
