"""Fail closed if the pinned native pipeline address inventory drifts."""
import argparse
import hashlib
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("source", type=Path)
parser.add_argument("inventory", type=Path)
args = parser.parse_args()
raw = args.source.read_bytes()
data = json.loads(args.inventory.read_text())
if hashlib.sha256(raw).hexdigest() != data["source_sha256"]:
    raise SystemExit("pinned agx_state.c hash mismatch")
start = raw.decode().find("static uint32_t\nagx_build_pipeline(")
if start < 0:
    raise SystemExit("agx_build_pipeline missing")
end = raw.decode().find("\nstatic ", start + 1)
body = raw.decode()[start:end]
missing = [field for field in data["address_expressions"] if field not in body]
if missing:
    raise SystemExit("address inventory drift: " + ", ".join(missing))
if data["status"] != "inventory-only":
    raise SystemExit("inventory status must be updated with a materialization proof")
