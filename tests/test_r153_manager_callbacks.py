"""Production worker selection and completion ordering for manager snapshots."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_r153_manager_lifetime import ROOT, SHARED

class ManagerCallbackTests(unittest.TestCase):
    def test_production_idle_and_completion_callbacks(self):
        platform = (ROOT / 'drivers/apple-agx/render-admission/src/backend_platform_windows.c').read_text()
        def function(name):
            start = platform.index('static APPLE_AGX_BACKEND_BOOL ' + name + '(')
            end = platform.index('{', start) + 1
            depth = 1
            while depth:
                depth += (platform[end] == '{') - (platform[end] == '}')
                end += 1
            return platform[start:end]
        source = (ROOT / 'tests/fixtures/r153_callbacks.c').read_text().replace(
            '@PREPARE@', function('AdmissionPrepareG4Manager')).replace(
            '@SAVE@', function('AdmissionSaveG4Manager'))
        complete = platform[platform.index('static APPLE_AGX_BACKEND_BOOL AdmissionBackendComplete('):]
        self.assertLess(complete.index('AdmissionSaveG4Manager(runtime, Fence)'),
                        complete.index('AdmissionGpuvaG3CompleteJob(adapter, Fence)'))
        worker = platform[platform.index('  submission.Submission.Kind = AppleAgxSubmissionGdi;'):]
        self.assertLess(worker.index('AdmissionGpuvaG3BeginJob('), worker.index('AdmissionPrepareG4Manager(runtime)'))
        self.assertLess(worker.index('AdmissionPrepareG4Manager(runtime)'),worker.index('AppleAgxBackendRuntimeSubmit('))
        names = ['render_shared_memory', 'g13_compute_work', 'render_template.generated',
                 'render_template_rebase', 'exp208_adapter', 'exp208_gdi',
                 'exp208_framebuffer', 'gdi', 'relocation', 'memory', 'exp208_dynamic',
                 'g13_codec', 'g13_queue_provider', 'g13_queue_runtime']
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp/'callbacks.c').write_text(source)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined','-I',str(SHARED/'include'),str(tmp/'callbacks.c'),
                *[str(SHARED/'src'/('apple_agx_'+n+'.c')) for n in names],'-o',str(tmp/'callbacks')],check=True)
            subprocess.run([str(tmp/'callbacks')],check=True)
