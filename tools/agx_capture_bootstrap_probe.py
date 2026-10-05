"""DRM-shim module that qualifies full client bootstrap without starting AGX."""

from __future__ import annotations

import json
import os
from pathlib import Path
import time

from tools.agx_capture_bootstrap import install_bootstrap_override


BOOTSTRAP_TIMEOUT = install_bootstrap_override()


def atomic_json(path: Path, value: dict) -> None:
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")
    temporary.replace(path)


def text(value) -> str:
    if isinstance(value, bytes):
        return value.split(b"\0", 1)[0].decode("ascii")
    return str(value)


class Shim:
    """Stop at the first ioctl after proving the embedded-Python setup path."""

    def __init__(self, memfd):
        self.memfd = memfd
        self.completed = False

    def ioctl(self, fd, request, argument):
        if self.completed:
            raise RuntimeError("full-client bootstrap probe ran more than once")
        self.completed = True
        before = time.monotonic()
        from m1n1.setup import u
        elapsed_ms = round((time.monotonic() - before) * 1000, 3)
        destination = Path(os.environ["AGX_BOOTSTRAP_PROBE_DIR"])
        atomic_json(destination / "before.json", {
            "firmware": text(u.version),
            "format_version": 1,
            "m1n1_base": int(u.base),
            "platform": text(u.adt.target_type),
        })
        atomic_json(destination / "bootstrap-metrics.json", {
            "bootstrap_timeout_seconds": BOOTSTRAP_TIMEOUT,
            "elapsed_ms": elapsed_ms,
            "format_version": 1,
            "path": "ld_preload_egl_embedded_python_first_ioctl",
        })
        os._exit(0)

    def bo_free(self, address):
        return 0
