from pathlib import Path
import hashlib
import json
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
COMMON_ROOT = Path(subprocess.check_output(
    ["git", "rev-parse", "--path-format=absolute", "--git-common-dir"],
    cwd=ROOT, text=True).strip()).parent
SOURCE = COMMON_ROOT / ".local/reference/mesa/src/gallium/drivers/asahi/agx_state.c"
OVERLAY = ROOT / "drivers/apple-agx/mesa/windows-overlay/agx-state-pipeline-bo.json"
SCRIPT = ROOT / "drivers/apple-agx/mesa/scripts/apply-pipeline-bo-overlay.py"

class PipelineBoOverlayTests(unittest.TestCase):
    def test_changes_only_pipeline_allocation(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "agx_state.c"
            subprocess.run(["python3", SCRIPT, SOURCE, OVERLAY, output], check=True)
            before, after = SOURCE.read_text(), output.read_text()
            patch = json.loads(OVERLAY.read_text())
            self.assertEqual(hashlib.sha256(SOURCE.read_bytes()).hexdigest(), patch["source_sha256"])
            self.assertEqual(after.count("agx_pool_alloc_aligned_with_bo(&batch->pipeline_pool, usc_size, 64,"), 1)
            self.assertEqual(after.count("agx_pool_alloc_aligned(&batch->pipeline_pool, usc_size, 64);"), 1)
            self.assertEqual(len(after), len(before) + len(patch["new"]) - len(patch["old"]))

if __name__ == "__main__":
    unittest.main()
