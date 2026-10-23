"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

C75/T6b: the obsolete 16 MiB UMD cutoff sends admitted large unshared buffers
through staging copies. Execute the actual policy expression with reported
class limits, boundary sizes and borrowed storage: only admitted unshared
buffers may select the single CPU/GPU allocation.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SystemDirectLimit(unittest.TestCase):
    def test_threshold_within_kmd_class_limit(self):
        screen = (ROOT/'drivers/apple-agx/render-admission/umd/src/umd_win32_screen.c').read_text()
        internal = (ROOT/'drivers/apple-agx/render-admission/umd/src/umd_internal.h').read_text()
        decision = re.search(r'  systemDirect = ([^;]+);', screen).group(1)
        old_define = re.search(r'^#define ADMISSION_UMD_SYSTEM_DIRECT_BYTES .*$', internal, re.M)
        harness = r'''
#include <assert.h>
#include <stdint.h>
@@DEFINE@@
struct Class { uint64_t MaximumBytes; };
static int direct(uint64_t Bytes, int BorrowedStaging, const struct Class *classInfo) {
  return @@DECISION@@;
}
int main(void) {
  struct Class c = {256ull << 20};
  const uint64_t sizes[] = {4096,16ull<<20,(16ull<<20)+65536,25ull<<20,64ull<<20,256ull<<20};
  for(unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);++i) {
    assert(direct(sizes[i],0,&c)); assert(!direct(sizes[i],1,&c));
  }
  assert(!direct((256ull<<20)+1,0,&c));
  c.MaximumBytes=16ull<<20;
  assert(direct(16ull<<20,0,&c)); assert(!direct((16ull<<20)+1,0,&c));
}
'''.replace('@@DECISION@@',decision).replace('@@DEFINE@@',old_define.group(0) if old_define else '')
        with tempfile.TemporaryDirectory(prefix='t6b-direct-policy-') as tmp:
            src,exe=Path(tmp)/'test.c',Path(tmp)/'test'
            src.write_text(harness)
            subprocess.run(['clang','-std=c11',str(src),'-o',str(exe)],check=True)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
