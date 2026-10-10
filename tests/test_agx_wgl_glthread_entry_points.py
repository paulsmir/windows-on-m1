"""EXP1176: every WGL entry point that uses a GL context on the application
thread finishes Mesa's glthread first.

With glthread the worker thread executes the marshalled GL calls; stw's
context, swap and present entry points use the same pipe/st context from
the application thread (flush, present readback, unbind, destroy). An entry
point exported straight to stw, or a wrapper that calls stw before
_mesa_glthread_finish, races the worker on the pipe_context. Invariants: the
ICD's .def routes exactly these entry points through agx_wgl_glthread.c,
every wrapper finishes before it calls stw, only context creation starts
glthread, and the build links the repository .def (not Mesa's).
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
ICD = ROOT / 'drivers/apple-agx/windows/icd'
WRAPPED = ('DrvCopyContext', 'DrvCreateContext', 'DrvCreateLayerContext', 'DrvDeleteContext',
           'DrvPresentBuffers', 'DrvReleaseContext', 'DrvSetContext', 'DrvShareLists',
           'DrvSwapBuffers', 'DrvSwapLayerBuffers')


def body(text, name):
    m = re.search(r'\bAgxWgl' + name + r'\([^)]*\)\s*\{', text)
    if not m:
        return ''
    start = text.index('{', m.start()); depth = 1; end = start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[start:end]


class GlthreadEntryPoints(unittest.TestCase):
    def test_def_routes_the_context_entry_points(self):
        exports = {}
        for line in (ICD / 'agx_wgl_icd.def').read_text().splitlines():
            line = line.split(';')[0].strip()
            if line.startswith('Drv'):
                name, _, target = line.partition('=')
                exports[name] = target
        for name in WRAPPED:
            self.assertEqual(exports.get(name), 'AgxWgl' + name, name)
        for name, target in exports.items():
            if name not in WRAPPED:
                self.assertEqual(target, '', name)

    def test_wrappers_finish_before_calling_stw(self):
        text = (ICD / 'agx_wgl_glthread.c').read_text()
        for name in WRAPPED:
            b = body(text, name)
            self.assertTrue(b, name)
            call = b.find(name + '(')
            self.assertGreater(call, 0, name)
            if name in ('DrvCreateContext', 'DrvCreateLayerContext'):
                self.assertIn('agx_glthread_start(' + name, b)
                continue
            finish = b.find('agx_glthread_finish')
            self.assertTrue(0 <= finish < call, name)
        self.assertEqual(text.count('agx_glthread_init_st('), 2)  # declaration + start

    def test_frontend_hook_is_set_before_glthread_starts(self):
        # EXP1176: glthread's worker start asserts fscreen->set_background_context.
        text = (ICD / 'agx_wgl_glthread_mesa.c').read_text()
        start = text.index('void agx_glthread_init_st(')
        init = text.index('_mesa_glthread_init(', start)
        hook = text.find('set_background_context =', start)
        self.assertTrue(start < hook < init)

    def test_no_stw_state_without_a_device(self):
        # EXP1177: DrvDeleteContext after DllMain cleared stw_dev faulted in
        # stw_lookup_context (hl.exe crash dump at game exit).
        text = (ICD / 'agx_wgl_glthread.c').read_text()
        for helper in ('agx_glthread_finish_current', 'agx_glthread_finish_handle', 'agx_glthread_start'):
            m = re.search(r'static \w+ ' + helper + r'\([^)]*\)\s*\{(.*?)\n\}', text, re.S)
            self.assertTrue(m, helper)
            b = m.group(1)
            uses = [b.find(x) for x in ('stw_lookup_context(', 'stw_current_context(') if x in b]
            self.assertTrue(uses, helper)
            self.assertTrue(0 <= b.find('stw_dev') < min(uses), helper)

    def test_build_links_the_repository_def(self):
        script = (ICD / 'build-icd-x86.ps1').read_text()
        self.assertIn(r'/DEF:$agx\windows\icd\agx_wgl_icd.def', script)
        self.assertNotIn('gallium_wgl.def', script)
        for unit in ('agx_wgl_glthread.c', 'agx_wgl_glthread_mesa.c'):
            self.assertIn(unit, script)


if __name__ == '__main__':
    unittest.main()
