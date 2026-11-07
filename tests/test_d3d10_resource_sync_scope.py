"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

ResourceMap(WRITE_DISCARD) drained unrelated GPU work before reaching Asahi's
resource renaming / hazard tracking. Execute the projected synchronization
decision and map dispatch with a busy context: discard must reach the transfer
map with DISCARD flags, while staging reads and DONOTWAIT retain their guards.
Only Windows/Gallium callbacks are substituted; the decisions are production C++.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'drivers/apple-agx/mesa/scripts/build-native-asahi-state.py'


def projected_body(name, unit='Resource.cpp'):
    text = SCRIPT.read_text()
    anchor = "replace_function_body('src/gallium/frontends/d3d10umd/" + unit + "','" + name + "','''"
    return text.split(anchor, 1)[1].split("''')", 1)[0]


PRELUDE = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
typedef int HRESULT;
typedef unsigned UINT;
#define S_OK 0
#define E_FAIL -1
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
enum { D3D10_DDI_MAP_READ=1, D3D10_DDI_MAP_WRITE,
 D3D10_DDI_MAP_READWRITE, D3D10_DDI_MAP_WRITE_DISCARD,
 D3D10_DDI_MAP_WRITE_NOOVERWRITE };
#define D3D10_DDI_MAP_FLAG_DONOTWAIT 0x100000
enum { PIPE_MAP_READ=1, PIPE_MAP_WRITE=2, PIPE_MAP_DISCARD_WHOLE_RESOURCE=4,
 PIPE_MAP_DISCARD_RANGE=8, PIPE_MAP_UNSYNCHRONIZED=16 };
struct pipe_box {};
struct pipe_transfer {};
struct pipe_context;
struct Resource { bool buffer; unsigned NumSubResources; void *resource;
 pipe_transfer *transfers[1]; };
static unsigned drains, polls, maps, seen_usage;
static int error;
static pipe_transfer transfer;
static HRESULT AgxD3d10WindowsFlushRetire(void *) { ++drains; return E_FAIL; }
static HRESULT AgxD3d10WindowsTryFlushRetire(void *) { ++polls; return E_FAIL; }
static HRESULT AgxD3d10WindowsFlushStatus(void *) { return error; }
static void SetError(void *, HRESULT h) { error=h; }
static void *map_resource(pipe_context *, void *, unsigned, unsigned usage,
                          pipe_box *, pipe_transfer **out) {
 ++maps; seen_usage=usage; *out=&transfer; return &transfer;
}
struct pipe_context {
 decltype(&map_resource) buffer_map=map_resource, texture_map=map_resource;
};
struct Device { void *windows; pipe_context *pipe; };
'''


class ResourceSyncScope(unittest.TestCase):
    def run_cpp(self, program):
        with tempfile.TemporaryDirectory(prefix='d3d10-sync-scope-') as tmp:
            source = Path(tmp) / 'scope.cpp'
            binary = Path(tmp) / 'scope'
            source.write_text(PRELUDE + program)
            subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17',
                            '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            '-Wno-unused-variable', '-Wno-unused-parameter',
                            '-fsanitize=address,undefined', str(source), '-o', str(binary)],
                           check=True, capture_output=True, text=True)
            ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_discard_reaches_resource_hazards_without_context_retirement(self):
        body = projected_body('ResourceMap')
        decision = body[body.index('   HRESULT status ='):body.index('   struct pipe_box box;')]
        mapping = body[body.index('   unsigned usage ='):body.index('   if (!map ||')]
        self.run_cpp(r'''
static void run(Device *device, Resource *resource, unsigned DDIMap, unsigned Flags) {
 void *hDevice=device; unsigned SubResource=0, level=0; pipe_box box;
DECISION
MAPPING
 assert(map && resource->transfers[0]);
}
int main() {
 pipe_context pipe; Device device={nullptr,&pipe};
 Resource resource={true,1,&pipe,{nullptr}};
 for (bool buffer : {true, false}) {
  resource.buffer=buffer;
  for (unsigned subresources : {1u, 4u}) {
   resource.NumSubResources=subresources;
   drains=polls=maps=0; error=0;
   run(&device,&resource,D3D10_DDI_MAP_WRITE_DISCARD,0);
   assert(drains==0 && polls==0 && maps==1 && error==0);
   assert(seen_usage==(PIPE_MAP_WRITE | (subresources==1 ?
          PIPE_MAP_DISCARD_WHOLE_RESOURCE : PIPE_MAP_DISCARD_RANGE)));
  }
 }
 for (unsigned mode : {1u, 2u, 3u}) {
  drains=polls=maps=0; error=0;
  run(&device,&resource,mode,0);
  assert(drains==1 && maps==0 && error==E_FAIL);
  drains=polls=maps=0; error=0;
  run(&device,&resource,mode,D3D10_DDI_MAP_FLAG_DONOTWAIT);
  assert(drains==0 && polls==1 && maps==0 && error==E_FAIL);
 }
 // NOOVERWRITE keeps its existing no-drain policy (its early map is separate).
 drains=polls=maps=0; error=0;
 run(&device,&resource,D3D10_DDI_MAP_WRITE_NOOVERWRITE,0);
 assert(!drains && !polls && maps==1 && !error);
 // DISCARD must retain device/backend error propagation without a drain.
 drains=polls=maps=0; error=-42;
 run(&device,&resource,D3D10_DDI_MAP_WRITE_DISCARD,0);
 assert(!drains && !polls && !maps && error==-42);
}
'''.replace('DECISION', decision).replace('MAPPING', mapping)
                     .replace('static void run(', '#include <initializer_list>\nstatic void run('))


if __name__ == '__main__':
    unittest.main()
