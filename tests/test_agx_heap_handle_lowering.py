"""The native state pairs marked descriptor-heap handles with the heap mirror.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? D3D12 C47: the DXBC
translator emits bindless_image_agx(.desc_set = DXBC_HEAP_HANDLE_MARK) for
heap-range accesses; Asahi's sysval pass must replace each with a handle
whose uniform is constant buffer DXBC_HEAP_MIRROR_CB's address
(ubo_base[15]). The translator's constants and the native-state patch live in
different layers: a drifted marker or buffer index would make every heap
access read an arbitrary uniform. The patch must apply to the pinned Asahi
source and compile.
"""
from pathlib import Path
import glob
import hashlib
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'drivers/apple-agx/mesa/scripts/build-native-asahi-state.py'
BINDINGS = ROOT / 'drivers/apple-agx/mesa/dxbc/dxbc_bindings.h'
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_dxbc_to_nir import local_tree, host_flags  # noqa: E402

PATH = 'src/gallium/drivers/asahi/agx_nir_lower_sysvals.c'


def sysvals_change():
    script = SCRIPT.read_text()
    start = script.index("change('%s'," % PATH)
    block = script[start:script.index(')])\n', start) + 3]
    calls = []
    exec(block.strip(), {'change': lambda path, sha, reps: calls.append((path, sha, reps))})
    return calls[0]


class AgxHeapHandleLowering(unittest.TestCase):
    def test_patch_constants_match_the_translator(self):
        defines = dict(re.findall(r'#define (DXBC_HEAP_\w+) (\w+)u', BINDINGS.read_text()))
        _, _, reps = sysvals_change()
        patched = ''.join(new for _, new in reps)
        self.assertIn('nir_intrinsic_desc_set(intr) != %su' % defines['DXBC_HEAP_HANDLE_MARK'], patched)
        self.assertIn('&s->ubo_base[%s]' % defines['DXBC_HEAP_MIRROR_CB'], patched)
        self.assertIn('.flags = ~0', patched)
        # the sysval load must directly precede the handle it pairs with
        load = patched.index('nir_load_sysval_agx(')
        self.assertLess(load, patched.index('return nir_bindless_image_agx(b, intr->src[0].ssa);'))

    def test_patch_applies_to_the_pinned_source_and_compiles(self):
        mesa = local_tree('reference', 'mesa')
        build = local_tree('accelerated-desktop-ad03', 'mesa-build')
        if not mesa or not build:
            self.skipTest('pinned Mesa sources not available')
        path, sha, reps = sysvals_change()
        self.assertEqual(path, PATH)
        raw = (mesa / path).read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(), sha)
        text = raw.decode()
        for old, new in reps:
            self.assertEqual(text.count(old), 1)
            text = text.replace(old, new)
        with tempfile.TemporaryDirectory() as tmp:
            stub = Path(tmp) / 'stub'
            stub.mkdir()
            (stub / 'xf86drm.h').write_text('')
            unit = Path(tmp) / 'agx_nir_lower_sysvals.c'
            unit.write_text(text)
            dirs = [stub] + [mesa / d for d in ('src/gallium/drivers/asahi', 'src/gallium/include',
                                                'src/gallium/auxiliary', 'src/asahi', 'src/asahi/lib',
                                                'src/asahi/libagx', 'src/asahi/layout', 'src/asahi/compiler',
                                                'src', 'include', 'include/drm-uapi', 'src/virtio/vdrm')] + \
                   [build / d for d in ('src/asahi', 'src/asahi/lib', 'src/asahi/genxml', 'src/asahi/libagx',
                                        'src/asahi/compiler', 'src/gallium/drivers/asahi')]
            result = subprocess.run(['clang', '-std=c11', '-fsyntax-only', '-Wno-c23-extensions',
                                     *host_flags(build), *['-I' + str(d) for d in dirs], str(unit)],
                                    capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr[-3000:])


if __name__ == '__main__':
    unittest.main()
