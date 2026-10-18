#!/usr/bin/env python3
"""Install a GPU-visible recovery wrapper into one newly prepared experiment.

This only writes host artifacts. It never launches Windows, executes a recovery
script, contacts SSH, or changes the immutable legacy recovery bytes.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile


class RecoveryInstallError(ValueError):
    pass


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def quote(value: str) -> str:
    """Always quote shell operands, including paths without whitespace."""
    return "'" + value.replace("'", "'\"'\"'") + "'"


def read_regular(path: Path) -> bytes:
    if path.is_symlink() or not path.is_file():
        raise RecoveryInstallError(f"expected regular, nonsymlink file: {path}")
    return path.read_bytes()


def assert_legacy(data: bytes, experiment: Path, number: str) -> None:
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise RecoveryInstallError("legacy recovery is not UTF-8") from exc
    expected = (
        "#!/bin/bash\n",
        f"R=$E/{experiment.name}/evidence/rec{number}h",
        "waitcycle; nohup $R/launch-hidden.sh > $R/launch.log",
        f"EXP{number}\\cleanup{number}\\cleanup-hidden.ps1",
        "HIDDEN_CLEANUP_UNVERIFIED_STOP_BEFORE_NORMAL",
        f"EXP{number}\\cleanup{number}\\durable-transition.ps1 -Mode Preflight",
        "RECOVERY_DONE",
    )
    if not text.startswith(expected[0]) or any(marker not in text for marker in expected[1:]):
        raise RecoveryInstallError("not the expected generated launch-hidden recovery script")
    if "agx-recover-visible.sh" in text:
        raise RecoveryInstallError("legacy backup is already a visible wrapper")
    # Reject accidental use of a different experiment even if expected markers
    # were appended to an unrelated script. Known reference launch IDs (909/950)
    # are allowed; recovery/cleanup-specific IDs must match this experiment.
    recovery_ids = re.findall(r"/evidence/rec([0-9]+)h\b|\\cleanup([0-9]+)\\", text)
    if any((left or right) != number for left, right in recovery_ids):
        raise RecoveryInstallError("legacy recovery names another experiment")


def atomic_replace(path: Path, data: bytes, mode: int) -> None:
    name = None
    try:
        with tempfile.NamedTemporaryFile(prefix=f".{path.name}.", dir=path.parent, delete=False) as stream:
            name = stream.name
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(name, mode)
        os.replace(name, path)
        name = None
    finally:
        if name is not None:
            Path(name).unlink(missing_ok=True)


def create_exclusive(path: Path, data: bytes, mode: int) -> None:
    # Never replace a backup or manifest, including in an interrupted rerun.
    with path.open("xb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    os.chmod(path, mode)


def install(experiment_dir: Path, experiment_id: str, worktree: Path | None = None) -> dict:
    if not re.fullmatch(r"[1-9][0-9]*", experiment_id):
        raise RecoveryInstallError("experiment ID must be a positive decimal number")
    experiment = experiment_dir.resolve(strict=True)
    if not experiment.is_dir() or not re.fullmatch(rf"EXP{experiment_id}-.+", experiment.name):
        raise RecoveryInstallError("experiment directory/ID mismatch")
    root = (worktree or Path(__file__).resolve().parents[1]).resolve(strict=True)
    host = root / "scripts/agx-recover-visible.sh"
    helper = root / "scripts/agx-recovery-disable-service.ps1"
    host_bytes, helper_bytes = read_regular(host), read_regular(helper)
    recovery = experiment / "evidence" / f"rec{experiment_id}h"
    if not recovery.is_dir() or recovery.is_symlink():
        raise RecoveryInstallError("expected recovery directory absent or symlinked")
    script = recovery / "recover.sh"
    backup = recovery / "recover.sh.legacy-hidden"
    manifest = recovery / "visible-recovery-source.json"
    original = read_regular(script)
    wrapper = ("#!/bin/bash\n# Installed by agx-install-visible-recovery.py; ordinary GPU-visible recovery.\n"
               f"exec /bin/bash -- {quote(str(host))} {quote(str(experiment))} {quote(experiment_id)}\n").encode()
    backup_bytes = read_regular(backup) if backup.exists() or backup.is_symlink() else None
    manifest_bytes = read_regular(manifest) if manifest.exists() or manifest.is_symlink() else None

    if original == wrapper:
        if backup_bytes is None:
            raise RecoveryInstallError("visible wrapper has no preserved legacy original")
        legacy = backup_bytes
        assert_legacy(legacy, experiment, experiment_id)
    else:
        assert_legacy(original, experiment, experiment_id)
        if backup_bytes is not None and backup_bytes != original:
            raise RecoveryInstallError("existing legacy backup differs; refusing replacement")
        if manifest_bytes is not None:
            raise RecoveryInstallError("existing manifest disagrees with current recovery wrapper")
        legacy = original

    result = {
        "Version": 1,
        "ExperimentId": experiment_id,
        "ExperimentDirectory": str(experiment),
        "Worktree": str(root),
        "RecoveryMode": "GpuVisibleServiceDisabledOrderedReboot",
        "Files": {
            "Wrapper": {"Path": str(script), "Sha256": digest(wrapper)},
            "LegacyHidden": {"Path": str(backup), "Sha256": digest(legacy)},
            "HostShell": {"Path": str(host), "Sha256": digest(host_bytes)},
            "DisableServiceHelper": {"Path": str(helper), "Sha256": digest(helper_bytes)},
        },
    }
    expected_manifest = (json.dumps(result, indent=2, sort_keys=True) + "\n").encode()
    # Validate every possible conflict before changing any experiment artifact.
    if manifest_bytes is not None and manifest_bytes != expected_manifest:
        raise RecoveryInstallError("immutable source manifest differs; refusing overwrite")
    mode = script.stat().st_mode & 0o777
    if backup_bytes is None:
        create_exclusive(backup, legacy, mode)
    if original != wrapper:
        atomic_replace(script, wrapper, mode | 0o100)
    if manifest_bytes is None:
        create_exclusive(manifest, expected_manifest, 0o644)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("experiment_dir", type=Path)
    parser.add_argument("experiment_id", help="numeric experiment ID, e.g. 1381")
    parser.add_argument("--worktree", type=Path)
    args = parser.parse_args()
    try:
        result = install(args.experiment_dir, args.experiment_id, args.worktree)
    except (RecoveryInstallError, OSError) as exc:
        parser.exit(2, f"VISIBLE_RECOVERY_INSTALL_REFUSED: {exc}\n")
    print(json.dumps({"Verdict": "PASS", **result}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
