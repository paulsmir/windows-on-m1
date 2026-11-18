#!/usr/bin/env python3
"""Launcher guard for an AppleAgx package on the standing (Wi-Fi/Bluetooth) profile (L4(i), 2026-11-18).

The bench launchers boot driver-line profiles that must not run an unrecorded GPU
driver.  This module decides, from what the guest has staged and from the record
of the accepted (or experiment-candidate) package, whether a launch may proceed.

Inputs (all plain JSON, produced by the guest script w2-agxpkg.ps1 and the bench):
  staged     list of {"oem": "oemNN.inf", "files": {name: sha256}} - one entry per
             staged AppleAgx driver package (hashes of the DriverStore copies)
  appl0002   {"problem": int, "version": str|None} - the ACPI\\APPL0002\\0 devnode
  record     {"version", "files": {name: sha256}, "valid_on_pairs": [..],
              "experiment_id": str (candidate records only)} or None

Modes:
  recovery    never refuses (a recovery that can be refused is worse than what the
              guard protects); it only reports what it saw.
  standing    the accepted package from STANDING.json, or no package.
  experiment  a candidate record bound to one experiment id; refused if the id
              differs, is missing, or that experiment already has an ACTUAL.

Phases:
  pre    before the ordered restart (guest up: a live read) or at a proxy start
         (no guest: the host-side last-known record stands in for the live read)
  post   after boot, once SSH is up (always a live read)

Every field in the record is compared; there are no half-checked fields.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ACCEPT, REFUSE = "ACCEPT", "REFUSE"
MODES = ("recovery", "standing", "experiment")


def _norm(h: str) -> str:
    return h.strip().lower()


def decide(*, mode: str, phase: str, pair: str, staged: list | None, appl0002: dict | None,
           record: dict | None, experiment_id: str | None = None,
           experiment_has_actual: bool = False) -> tuple[str, str]:
    """Pure decision.  staged None = no live read and no last-known record."""
    if mode not in MODES:
        raise ValueError(f"mode {mode!r}")
    if phase not in ("pre", "post"):
        raise ValueError(f"phase {phase!r}")
    if mode == "recovery":
        n = "unknown" if staged is None else str(len(staged))
        return ACCEPT, f"recovery-never-refused staged={n}"
    if staged is None:
        return REFUSE, ("no-live-read-and-no-last-known-record: boot the recovery mode, or start with "
                        "the guest up (live read), then relaunch")
    if len(staged) == 0:
        if phase == "post" and appl0002 is not None and int(appl0002.get("problem", -1)) != 28:
            return REFUSE, f"no-package-staged-but-APPL0002-problem-{appl0002.get('problem')}"
        return ACCEPT, "no-package-staged"
    if len(staged) > 1:
        return REFUSE, f"{len(staged)}-packages-staged"
    if record is None:
        return REFUSE, "package-staged-but-no-record"
    if mode == "experiment":
        rid = record.get("experiment_id")
        if not experiment_id or not rid:
            return REFUSE, "experiment-id-missing"
        if rid != experiment_id:
            return REFUSE, f"experiment-id-mismatch record={rid} launch={experiment_id}"
        if experiment_has_actual:
            return REFUSE, f"experiment-{rid}-already-has-ACTUAL"
    elif record.get("experiment_id"):
        return REFUSE, "standing-launch-with-a-candidate-record"
    if pair not in record.get("valid_on_pairs", []):
        return REFUSE, f"pair-{pair}-not-in-record"
    want = {k.lower(): _norm(v) for k, v in record.get("files", {}).items()}
    if not want:
        return REFUSE, "record-has-no-file-hashes"
    have = {k.lower(): _norm(v) for k, v in staged[0].get("files", {}).items()}
    for name, h in sorted(want.items()):
        if name not in have:
            return REFUSE, f"{name}-missing-in-staged-package"
        if have[name] != h:
            return REFUSE, f"{name}-hash-differs"
    if phase == "post":
        if appl0002 is None:
            return REFUSE, "post-boot-without-APPL0002-read"
        if int(appl0002.get("problem", -1)) != 0:   # EXP2717: Code 43 with the right version bound
            return REFUSE, f"package-bound-but-APPL0002-problem-{appl0002.get('problem')}"
        if appl0002.get("version") != record.get("version"):
            return REFUSE, f"bound-version-{appl0002.get('version')}-not-{record.get('version')}"
    return ACCEPT, f"package-{record.get('version')}-matches-record"


def has_actual(ledger_text: str, exp_id: str) -> bool:
    return re.search(rf"^{re.escape(exp_id)} ACTUAL\b", ledger_text, re.M) is not None


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--mode", choices=MODES, required=True)
    ap.add_argument("--phase", choices=("pre", "post"), required=True)
    ap.add_argument("--pair", required=True, help="m1n1short+mushort, e.g. fab4356f+fd94b781")
    ap.add_argument("--read", type=Path, help="w2-agxpkg.ps1 JSON (live read); absent = use --last-known")
    ap.add_argument("--last-known", type=Path, required=True, help="host-side last-known record (read and written)")
    ap.add_argument("--record", type=Path, help="STANDING.json (standing) or candidate JSON (experiment)")
    ap.add_argument("--experiment")
    ap.add_argument("--ledger", type=Path)
    a = ap.parse_args(argv)
    live = json.loads(a.read.read_text()) if a.read else None
    if live is not None:
        a.last_known.write_text(json.dumps(live, indent=1) + "\n")   # every live read updates the record
        src = "live"
    elif a.last_known.exists():
        live, src = json.loads(a.last_known.read_text()), "last-known"
    else:
        src = "none"
    record = None
    if a.record and a.record.exists():
        r = json.loads(a.record.read_text())
        record = r.get("accepted_gpu_package") if a.mode == "standing" else r
    act = bool(a.experiment and a.ledger and has_actual(a.ledger.read_text(errors="replace"), a.experiment))
    v, why = decide(mode=a.mode, phase=a.phase, pair=a.pair,
                    staged=None if live is None else live.get("staged", []),
                    appl0002=None if live is None else live.get("appl0002"),
                    record=record, experiment_id=a.experiment, experiment_has_actual=act)
    print(f"AGX_GUARD {v} mode={a.mode} phase={a.phase} source={src} {why}")
    return 0 if v == ACCEPT else 3


if __name__ == "__main__":
    sys.exit(main())
