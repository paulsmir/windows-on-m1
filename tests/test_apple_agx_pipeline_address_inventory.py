from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
COMMON = Path(subprocess.check_output(
    ['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'],
    cwd=ROOT, text=True).strip()).parent

class PipelineAddressInventoryTests(unittest.TestCase):
 def test_pinned_native_address_inventory_is_complete(self):
  subprocess.run(['python3', str(ROOT / 'drivers/apple-agx/mesa/scripts/verify-pipeline-address-inventory.py'),
                  str(COMMON / '.local/reference/mesa/src/gallium/drivers/asahi/agx_state.c'),
                  str(ROOT / 'drivers/apple-agx/mesa/windows-overlay/agx-build-pipeline-addresses.json')], check=True)
