"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Production completion must synchronize manager state before retirement. Deferred
snapshots must flush Stats each completion, flush all objects before eviction,
and look up the process under its registry lock (safe after process teardown).
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_r153_manager_lifetime import ROOT, SHARED
from test_render_job_timing_rc83 import function_body

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
            '@SAVE@', function('AdmissionSaveG4Manager')).replace(
            '@EVICT@', function_body(platform, 'AdmissionEvictG4Manager')).replace(
            '@CANCEL@', function_body(platform, 'AdmissionCancelUnpublishedManager')).replace(
            '@STORE@', function_body((ROOT / 'drivers/apple-agx/render-admission/src/gpuva_g3_windows.c').read_text(),
                                    'AdmissionGpuvaG3StoreManagerSnapshot'))
        complete = platform[platform.index('static APPLE_AGX_BACKEND_BOOL AdmissionBackendComplete('):]
        self.assertLess(complete.index('AdmissionSaveG4Manager(runtime, Fence, Status)'),
                        complete.index('AdmissionGpuvaG3CompleteJobTimestamp(adapter, g3_context, Fence,'))
        worker = platform[platform.index('  submission.Submission.Kind = AppleAgxSubmissionGdi;'):]
        self.assertLess(worker.index('AdmissionGpuvaG3BeginJob('), worker.index('AdmissionPrepareG4Manager(runtime)'))
        self.assertLess(worker.index('AdmissionPrepareG4Manager(runtime)'),worker.index('AppleAgxBackendRuntimeSubmit('))
        backend_model = (SHARED / 'tests/apple_agx_backend_runtime_test.c').read_text()
        source = backend_model.replace('int main(void)', 'int backend_model_suite(void)') + '\n' + source
        names = ['backend_runtime', 'submission', 'firmware', 'rtkit', 'render_shared_memory', 'g13_compute_work', 'render_template.generated',
                 'render_template_rebase', 'exp208_adapter', 'exp208_gdi',
                 'exp208_framebuffer', 'gdi', 'relocation', 'memory', 'exp208_dynamic',
                 'g13_codec', 'g13_queue_provider', 'g13_queue_runtime']
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp/'callbacks.c').write_text(source)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
                '-ftrivial-auto-var-init=pattern','-fsanitize=address,undefined','-I',str(SHARED/'include'),str(tmp/'callbacks.c'),
                *[str(SHARED/'src'/('apple_agx_'+n+'.c')) for n in names],'-o',str(tmp/'callbacks')],check=True)
            subprocess.run([str(tmp/'callbacks')],check=True)
