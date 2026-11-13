"""Machine-specific bench values for Python tools (see scripts/lib/bench-env.sh for the keys).

Reads KEY=VALUE lines from $AGX_BENCH_ENV or ~/.config/agx-bench.env; the process environment
wins over the file.  Nothing machine-specific is kept in the repository.
"""
import os
from pathlib import Path

REQUIRED = ("AGX_BENCH_ROOT", "AGX_PROXY_PORT_A", "AGX_PROXY_PORT_B", "AGX_GUEST_SSH",
            "AGX_GUEST_HOME", "AGX_BUILDER_KEY")


def load(required=REQUIRED):
    values = {}
    for candidate in (os.environ.get("AGX_BENCH_ENV"), str(Path.home() / ".config/agx-bench.env")):
        if candidate and Path(candidate).is_file():
            for line in Path(candidate).read_text().splitlines():
                line = line.strip()
                if line and not line.startswith("#") and "=" in line:
                    key, value = line.split("=", 1)
                    values[key.strip()] = value.strip()
            break
    values.update({k: v for k, v in os.environ.items() if k.startswith("AGX_")})
    missing = [k for k in required if not values.get(k)]
    if missing:
        raise SystemExit("bench_env: not set: %s (see scripts/lib/bench-env.sh)" % ", ".join(missing))
    root = values["AGX_BENCH_ROOT"]
    values.setdefault("AGX_TANDEM", root + "/.local/tandem")
    values.setdefault("AGX_PROXY_PYTHON", root + "/proxyenv/bin/python")
    values.setdefault("AGX_INTEGRATION", root + "/.worktrees/integration-ad04-windows-compiler")
    return values


def bench_root():
    """AGX_BENCH_ROOT from the bench config, else the main checkout that holds this repository
    (the directory above .worktrees/ when this file runs from a worktree)."""
    try:
        return Path(load(("AGX_BENCH_ROOT",))["AGX_BENCH_ROOT"])
    except SystemExit:
        here = Path(__file__).resolve()
        parts = here.parts
        if ".worktrees" in parts:
            return Path(*parts[:parts.index(".worktrees")])
        return here.parents[1]
