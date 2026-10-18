"""Offline defect probe, not a passing driver regression or hardware test.

Run from any directory. Exit 0 means the observed stale-pointer defect and
the unsafe naive InitBM workaround were both reproduced. The second run
asserts the desired pointer invariant and MUST fail on the recorded source.
"""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SHARED = ROOT / "drivers/apple-agx/shared"
SOURCES = [
    "apple_agx_render_shared_memory.c", "apple_agx_g13_compute_work.c",
    "apple_agx_render_template.generated.c", "apple_agx_render_template_rebase.c",
    "apple_agx_exp208_adapter.c", "apple_agx_exp208_gdi.c",
    "apple_agx_exp208_framebuffer.c", "apple_agx_gdi.c",
    "apple_agx_relocation.c", "apple_agx_memory.c", "apple_agx_exp208_dynamic.c",
]

fixture = SHARED / "tests/apple_agx_render_shared_memory_test.c"
source = fixture.read_text()
marker = "  assert(AppleAgxRenderSharedMemoryDestroy(&owner) =="
assert source.count(marker) == 1
source = "#include <stdio.h>\n" + source.replace(
    marker, (HERE / "manager-probe.inc").read_text() + marker
)
inputs = [fixture, *[SHARED / "src" / n for n in SOURCES]]
record = {"inputs": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                     for p in inputs}}
with tempfile.TemporaryDirectory() as temp:
    temp = Path(temp)
    (temp / "probe.c").write_text(source)
    command = [os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
               "-Werror", "-fsanitize=address,undefined", "-I", str(SHARED / "include"),
               str(temp / "probe.c"), *[str(SHARED / "src" / n) for n in SOURCES],
               "-o", str(temp / "probe")]
    subprocess.run(command, check=True, cwd=ROOT)
    for mode in ("observe", "require-current"):
        env = os.environ.copy()
        env.pop("R152_REQUIRE_CURRENT", None)
        if mode == "require-current":
            env["R152_REQUIRE_CURRENT"] = "1"
        result = subprocess.run([str(temp / "probe")], env=env,
                                capture_output=True, text=True)
        record[mode] = {"exit": result.returncode,
                        "output": result.stdout + result.stderr}
        print(mode, result.returncode, result.stdout + result.stderr)
    assert record["observe"]["exit"] == 0
    assert record["require-current"]["exit"] != 0
    assert "second_page" in record["require-current"]["output"]
(HERE / "manager-probe-result.json").write_text(json.dumps(record, indent=2) + "\n")
