"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Producer-slot reuse and query destruction must not invalidate queued timestamp
snapshots; an OQ slot must stay allocated through the last resolve reader.
Run actual projected attachment/completion/destruction/reader registration and
the native OQ allocator, with deferred GPU execution and ASan/UBSan.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

from g3_vidmm_replay import body
from test_d3d10_shader_fields_init import ROOT, local_tree, project, function_body


class QueryGenerations(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='agx-query-generations-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.path = Path(cls.tmp.name)
        out = project(cls.path / 'native')
        if out is None:
            raise unittest.SkipTest('reference Mesa tree not available')
        cls.query = (out / 'src/gallium/drivers/asahi/agx_query.c').read_text()
        cls.batch = (out / 'src/gallium/drivers/asahi/agx_batch.c').read_text()

    def replay(self, mutation=None):
        definitions = [body(self.query, name) for name in (
            'is_occlusion', 'agx_destroy_oq_heap', 'agx_alloc_oq_heap',
            'agx_get_oq_heap', 'agx_alloc_oq', 'agx_oq_index', 'agx_free_oq',
            'agx_create_query', 'flush_query_writers', 'is_query_busy',
            'agx_destroy_query')]
        definitions.append('#include "agx_win32_query_lifecycle.h"')
        definitions.append('static enum query_copy_type classify_query_type(enum pipe_query_type type)\n' +
                           function_body(self.query, 'classify_query_type'))
        definitions += [body(self.query, name) for name in (
            'agx_begin_query', 'agx_end_query',
            'result_type_size', 'agx_get_query_result_resource_gpu',
            'agx_add_query_to_batch', 'agx_batch_add_timestamp_query')]
        definitions.append(body(self.batch, 'agx_add_timestamp_end_query'))
        definitions.append(body(self.query, 'agx_finish_batch_queries'))
        source = '\n'.join(definitions)
        if mutation:
            old, new = mutation
            self.assertEqual(source.count(old), 1)
            source = source.replace(old, new, 1)
        fixture = (ROOT / 'tests/d3d12_query_generations_fixture.h').read_text()
        (self.path / 'test.c').write_text(fixture.replace('@@PRODUCTION@@', source))
        mesa = local_tree('reference', 'mesa')
        built = subprocess.run([
            'clang', '-std=c11', '-DAPPLE_AGX_GPUVA_WINSYS',
            '-Werror=implicit-function-declaration', '-fsanitize=address,undefined',
            '-I' + str(ROOT / 'drivers/apple-agx/mesa/winsys'),
            '-I' + str(mesa / 'src'), str(self.path / 'test.c'),
            '-o', str(self.path / 'test'),
        ], capture_output=True, text=True)
        self.assertEqual(built.returncode, 0, built.stderr)
        return subprocess.run([str(self.path / 'test')], capture_output=True, text=True, timeout=10)

    def test_actual_timestamp_snapshots_and_occlusion_reader_generations(self):
        run = self.replay()
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    unittest.main()
