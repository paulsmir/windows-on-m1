"""Apply the one pinned build-local agx_build_pipeline provenance overlay."""
import argparse
import hashlib
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("source", type=Path)
parser.add_argument("overlay", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
raw = args.source.read_bytes()
patch = json.loads(args.overlay.read_text())
if hashlib.sha256(raw).hexdigest() != patch["source_sha256"]:
    raise SystemExit("pinned agx_state.c hash mismatch")
text = raw.decode()
start = text.find(patch["function_anchor"])
if start < 0:
    raise SystemExit("agx_build_pipeline anchor missing")
end = text.find("\nstatic ", start + len(patch["function_anchor"]))
if end < 0:
    raise SystemExit("agx_build_pipeline boundary missing")
prefix, body, suffix = text[:start], text[start:end], text[end:]
if body.count(patch["old"]) != 1:
    raise SystemExit("agx_build_pipeline allocation anchor ambiguous")
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(prefix + body.replace(patch["old"], patch["new"]) + suffix)
