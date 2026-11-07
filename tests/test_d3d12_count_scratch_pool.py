"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
GPU count scratch can approach the Gallium 32-bit buffer limit. A wrapping
WOW64 pool capacity check must not reuse a small slab for that allocation.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_d3d10_shader_fields_init import project

class CountScratchPool(unittest.TestCase):
    def test_pool_does_not_reuse_slab_on_32bit_extent_wrap(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Near-4GiB scratch on WOW64 must allocate backing, never wrap offset+
        size into an existing 256KiB slab. Compile the projected decision
        with explicit uint32_t inputs so this executes on a 64-bit host too.
        """
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);out=project(p/'out')
            pool=(out/'src/asahi/lib/pool.c').read_text()
            line=next(s.strip() for s in pool.splitlines() if s.strip().startswith('if (unlikely(bo == NULL'))
            code=r'''#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#define POOL_SLAB_SIZE (256*1024)
#define unlikely(x) (x)
static int allocates(void *bo,uint32_t offset,uint32_t sz){
'''+line+r'''return 1; } return 0; }
int main(void){
 assert(allocates((void *)1,32768,0xffffbff0u));
 assert(allocates((void *)1,0xffffbff0u,32768));
 assert(allocates((void *)1,262140,4));
 assert(!allocates((void *)1,32768,4));
 assert(allocates(NULL,0,4));
}
'''
            (p/'pool.c').write_text(code)
            run=subprocess.run(['clang','-std=c11','-Wall','-Werror',str(p/'pool.c'),'-o',str(p/'pool')],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)
            run=subprocess.run([str(p/'pool')],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)

