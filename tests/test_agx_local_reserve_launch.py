"""Assisted launch must retry the AGX receipt after installing stage-2 RAM."""

import ast
import os
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch


class ReachedGuestSetup(Exception):
    pass


def test_agx_local_receipt_after_stage2_ram():
    source = Path(__file__).resolve().parents[1] / "m1n1_windows/proxyclient/m1n1/hv/__init__.py"
    module = ast.parse(source.read_text())
    cls = next(node for node in module.body if isinstance(node, ast.ClassDef) and node.name == "HV")
    start = next(node for node in cls.body if isinstance(node, ast.FunctionDef) and node.name == "start")
    namespace = {"IODEV": [], "os": os}
    exec(compile(ast.Module(body=[start], type_ignores=[]), str(source), "exec"), namespace)

    calls = []

    def stage2_ready():
        calls.append("stage2-ready")

    def retry_receipt():
        calls.append("reserve-retry")
        assert calls[-2] == "stage2-ready"
        return True

    def stop_before_guest():
        raise ReachedGuestSetup

    fake = SimpleNamespace(
        iodev=3,
        map_essential=lambda: calls.append("essential"),
        pt_update=stage2_ready,
        p=SimpleNamespace(hv_map_agx_power_broker=retry_receipt),
        adt=SimpleNamespace(build=stop_before_guest),
    )
    with patch.dict(os.environ, {"WOM1_AGX_G2_POWER_BROKER": "1"}):
        try:
            namespace["start"](fake)
        except ReachedGuestSetup:
            pass
        else:
            assert False, "the test must stop before guest entry"
    assert calls == ["essential", "stage2-ready", "reserve-retry"]


if __name__ == "__main__":
    test_agx_local_receipt_after_stage2_ram()
