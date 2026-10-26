"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Timestamp GPU resolves must reduce the immutable End seed and every retained
producer's full unsigned 64-bit value, including 128 outstanding batches.
Execute the actual OpenCL body under sanitizers, reproduce the G13 binaries,
and compile/link the projected dispatch ABI and both native program tables.
"""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from test_d3d10_shader_fields_init import ROOT, local_tree, project
from test_dxbc_to_nir import host_flags

WINSYS = ROOT / 'drivers/apple-agx/mesa/winsys'
SCRIPTS = ROOT / 'drivers/apple-agx/mesa/scripts'
KERNEL = WINSYS / 'agx_win32_query_timestamp.cl'
BUNDLE = WINSYS / 'query-timestamp-generated'


def module(name):
    script = SCRIPTS / ('native-asahi-' + name + '.py')
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), script)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


PROGRAM = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#define global
#include "compiler/libcl/libcl.h"
#undef KERNEL
#define KERNEL(n) void
#include "agx_win32_query_timestamp.cl"

static void run(const uint64_t *values, unsigned count, uint64_t seed,
                uint64_t expected)
{
   uint64_t *addresses = count ? malloc(count * sizeof(uint64_t)) : NULL;
   for (unsigned i = 0; i < count; ++i)
      addresses[i] = (uintptr_t)&values[i];
   uint64_t destination[] = {0xaabbccddeeff0011ull, 17, 0x2233445566778899ull};
   libagx_win32_query_timestamp((uintptr_t)addresses, count, seed,
                               (uintptr_t)&destination[1]);
   assert(destination[0] == 0xaabbccddeeff0011ull);
   assert(destination[1] == expected);
   assert(destination[2] == 0x2233445566778899ull);
   free(addresses);
}

int main(void)
{
   run(NULL, 0, 0, 0);
   run(NULL, 0, 0xffffffffff123456ull, 0xffffffffff123456ull);
   uint64_t small[] = {0, 5, 2};
   run(small, 3, 4, 5);
   run(small, 3, 9, 9);
   uint64_t high[] = {0xffffffffull, 0x100000000ull, 0x8000000000000000ull,
                     0x7fffffffffffffffull, UINT64_MAX, 4};
   run(high, 4, 0, 0x8000000000000000ull);
   run(high, 6, 0x8000000000000001ull, UINT64_MAX);
   run(high, 4, UINT64_MAX, UINT64_MAX);
   uint64_t many[128] = {0};
   for (unsigned n = 1; n <= 128; ++n) {
      many[n - 1] = 0x12345678abcdef00ull;
      run(many, n, 0x12345678abcdeeffull, 0x12345678abcdef00ull);
      many[n - 1] = 0;
   }
   /* A late producer update is visible through its address, never a copied
    * source value; a reused destination cannot seed the next reduction. */
   uint64_t source = 4, address = (uintptr_t)&source, destination = UINT64_MAX;
   source = 0x100000001ull;
   libagx_win32_query_timestamp((uintptr_t)&address, 1, 7, (uintptr_t)&destination);
   assert(destination == 0x100000001ull);
   /* Out-of-contract counts must not dereference an invalid source pointer. */
   destination = 0x1122334455667788ull;
   libagx_win32_query_timestamp(0, 129, 0, (uintptr_t)&destination);
   libagx_win32_query_timestamp(0, UINT32_MAX, 0, (uintptr_t)&destination);
   assert(destination == 0x1122334455667788ull);
   return 0;
}
'''


class QueryTimestampKernel(unittest.TestCase):
    def test_actual_kernel_unsigned_reduction_bounds_and_destination(self):
        self.assertTrue(KERNEL.is_file(), 'GPU timestamp reduction kernel is missing')
        mesa = local_tree('reference', 'mesa')
        self.assertIsNotNone(mesa)
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            (p / 'test.c').write_text(PROGRAM)
            result = subprocess.run([
                'clang', '-std=c11', '-Wall', '-Werror',
                '-fsanitize=address,undefined', '-I' + str(WINSYS),
                '-I' + str(mesa / 'src'), str(p / 'test.c'), '-o', str(p / 'test'),
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(p / 'test')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_real_g13_compilation_reproduces_pinned_bundle(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Editing the kernel without regenerating both chips, changing the host
        compiler, or tampering with a shipped binary must fail verification.
        """
        generator = SCRIPTS / 'generate-query-timestamp.py'
        self.assertTrue(generator.is_file(), 'Timestamp shader generator is missing')
        mesa = local_tree('reference', 'mesa')
        build = local_tree('accelerated-desktop-ad03', 'mesa-build')
        self.assertIsNotNone(mesa)
        self.assertIsNotNone(build)
        projection = module('query-timestamp')
        self.assertEqual(projection.generated(ROOT), BUNDLE)
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            result = subprocess.run([
                sys.executable, str(generator), '--mesa-source', str(mesa),
                '--mesa-build', str(build), '--output', str(p / 'generated'),
            ], capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stderr)
            manifest = json.loads((BUNDLE / 'manifest.json').read_text())
            self.assertEqual(manifest['targets'], ['g13g', 'g13x'])
            self.assertEqual(json.loads((p / 'generated/manifest.json').read_text()), manifest)
            for name, digest in manifest['outputs'].items():
                with self.subTest(output=name):
                    self.assertEqual(hashlib.sha256((p / 'generated' / name).read_bytes()).hexdigest(), digest)
            for corrupt in ('agx_win32_query_timestamp.cl', 'agx_win32_query_timestamp_shaders.c'):
                copy_root = p / corrupt
                copy_winsys = copy_root / 'drivers/apple-agx/mesa/winsys'
                copy_winsys.mkdir(parents=True)
                (copy_winsys.parent / 'scripts').mkdir()
                shutil.copy2(generator, copy_winsys.parent / 'scripts' / generator.name)
                shutil.copytree(BUNDLE, copy_winsys / BUNDLE.name)
                for name in ('agx_win32_query_timestamp.cl', 'agx_win32_query_timestamp.h'):
                    shutil.copy2(WINSYS / name, copy_winsys / name)
                victim = copy_winsys / corrupt if corrupt.endswith('.cl') else copy_winsys / BUNDLE.name / corrupt
                victim.write_bytes(victim.read_bytes() + b'\n')
                with self.assertRaises(RuntimeError):
                    projection.generated(copy_root)

    def test_projected_dispatch_links_both_tables_and_preserves_existing_programs(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Header-only insertion or mismatched enum/table indices would dispatch
        the wrong binary. Link the real projected library and invoke its macro.
        """
        self.assertTrue((SCRIPTS / 'native-asahi-query-timestamp.py').is_file(),
                        'Timestamp projection is missing')
        mesa = local_tree('reference', 'mesa')
        build = local_tree('accelerated-desktop-ad03', 'mesa-build')
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            out = project(p / 'native')
            source = (build / 'src/asahi/lib/libagx_shaders.c').read_text()
            source = module('indirect-stream').project_library_c(source, ROOT)
            source = module('query-timestamp').project_library_c(source, ROOT)
            library = out / 'src/asahi/lib/libagx_shaders.c'
            library.write_text(source)
            (p / 'test.c').write_text(r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "libagx_shaders.h"
static unsigned calls;
static void dispatch(unsigned context, unsigned grid, unsigned barrier,
                     enum libagx_program program, const void *data, unsigned size)
{
   assert(context == 11 && grid == 1 && barrier == 23);
   assert(program == LIBAGX_WIN32_QUERY_TIMESTAMP);
   assert(size == sizeof(struct libagx_win32_query_timestamp_args));
   struct libagx_win32_query_timestamp_args args;
   memcpy(&args, data, sizeof(args));
   assert(args.addresses_array_gpu == 0x1122334455667788ull);
   assert(args.count == 128 && args.seed == 0xffeeddccbbaa9988ull);
   assert(args.destination_gpu == 0x8877665544332211ull);
   ++calls;
}
#define MESA_DISPATCH_PRECOMP dispatch
int main(void)
{
   libagx_win32_query_timestamp(11, 1, 23, 0x1122334455667788ull,
                               128, 0xffeeddccbbaa9988ull, 0x8877665544332211ull);
   assert(calls == 1);
   for (unsigned i = 0; i < LIBAGX_NUM_PROGRAMS; ++i) {
      assert(libagx_g13g[i] && libagx_g13x[i]);
   }
   assert(libagx_g13g[LIBAGX_WIN32_QUERY_TIMESTAMP] != libagx_g13g[LIBAGX_WIN32_INDIRECT_STREAM]);
   assert(libagx_g13x[LIBAGX_WIN32_QUERY_TIMESTAMP] != libagx_g13x[LIBAGX_HELPER]);
   return 0;
}
''')
            result = subprocess.run([
                'clang', '-std=c11', '-Wall', '-Werror', '-Dstatic_assert=_Static_assert',
                '-fsanitize=address,undefined', '-I' + str(out / 'src/asahi/lib'),
                '-I' + str(mesa / 'src'), *host_flags(build), '-UNDEBUG',
                str(p / 'test.c'), str(library),
                '-o', str(p / 'test'),
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(p / 'test')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
