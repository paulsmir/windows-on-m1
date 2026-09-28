"""Exercise manager switches through the production active materializer."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / 'drivers/apple-agx/shared'

class ManagerLifetimeTests(unittest.TestCase):
    def test_real_materializer_manager_switch(self):
        source = (SHARED / 'tests/apple_agx_render_shared_memory_test.c').read_text()
        marker = '  assert(AppleAgxRenderSharedMemoryDestroy(&owner) =='
        self.assertEqual(source.count(marker), 1)
        source = source.replace(marker, (ROOT / 'tests/fixtures/r153_manager.inc').read_text() + marker)
        names = ['render_shared_memory', 'g13_compute_work', 'render_template.generated',
                 'render_template_rebase', 'exp208_adapter', 'exp208_gdi',
                 'exp208_framebuffer', 'gdi', 'relocation', 'memory', 'exp208_dynamic']
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / 'probe.c').write_text(source)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                '-Werror', '-fsanitize=address,undefined', '-I', str(SHARED / 'include'),
                str(tmp / 'probe.c'), *[str(SHARED / 'src' / ('apple_agx_' + n + '.c')) for n in names],
                '-o', str(tmp / 'probe')], check=True)
            subprocess.run([str(tmp / 'probe')], check=True)
