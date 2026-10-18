"""EXP691: the standard client must recognize the measured ACPI adapter ID."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StandardAdapterIdentityTests(unittest.TestCase):
    def test_exact_air_identity_and_reject_foreign_adapter(self):
        source = (ROOT / 'drivers/apple-agx/windows/one-shot/apple_agx_d3d10_standard.c').read_text()
        guard = re.search(r'if\(([^\n]+)\)\{result=DXGI_ERROR_UNSUPPORTED;goto done;\}', source)
        self.assertIsNotNone(guard)
        program = '''
#include <assert.h>
struct { unsigned VendorId, DeviceId; } adapterDesc;
static int rejected(unsigned vendor, unsigned device) {
  adapterDesc.VendorId=vendor; adapterDesc.DeviceId=device;
  return CONDITION;
}
int main(void) {
  assert(!rejected(0x4c505041u,0x32303030u));
  assert(rejected(0x4c505041u,0x32303031u));
  assert(rejected(0x1414u,0x8cu));
  assert(rejected(0x106bu,0x32303030u));
  return 0;
}
'''.replace('CONDITION', guard.group(1))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / 'identity.c').write_text(program)
            subprocess.run([os.environ.get('CC', 'clang'), '-Wall', '-Werror',
                            str(path / 'identity.c'), '-o', str(path / 'identity')], check=True)
            subprocess.run([str(path / 'identity')], check=True)
