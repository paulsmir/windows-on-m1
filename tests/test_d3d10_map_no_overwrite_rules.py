"""D3D11.1 DDI: WRITE_NOOVERWRITE maps every dynamic buffer.

EXP1325: on the FL 11_0 desktop one process had 5760 ResourceMap calls
refused with E_INVALIDARG (reject-seterror fn=ResourceMap) and kept
recreating its devices; FL 10_0 runs had none. The map gate kept the D3D10
rules: NO_OVERWRITE only for vertex/index buffers without a shader-resource
bind, and constant buffers only with WRITE_DISCARD. The D3D11.1 runtime
reports MapNoOverwriteOnDynamicConstantBuffer together with ClearView and
ConstantBufferOffsetting, and MapNoOverwriteOnDynamicBufferSRV together with
MultisampleRTVWithForcedSampleCountOne (D3D11_FEATURE_DATA_D3D11_OPTIONS
remarks), and then passes those maps to the driver.
Invariant: a dynamic buffer of any bind accepts WRITE_DISCARD and
WRITE_NOOVERWRITE; dynamic textures keep WRITE_DISCARD only, staging
resources their CPU access, and the flag, ownership and busy-subresource
refusals stand. Runs the projected rule block.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py"
REFUSAL = "      SetError(hDevice, E_INVALIDARG); return;\n   }\n"


def rule_block():
    text = SCRIPT.read_text()
    anchor = "replace_function_body('src/gallium/frontends/d3d10umd/Resource.cpp','ResourceMap','''"
    start = text.index(anchor) + len(anchor)
    end = text.index(REFUSAL, start)
    return text[start:end] + "      return 0;\n   }\n   return 1;\n"


SHIM = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
typedef unsigned UINT;
typedef long LONG;
typedef int HRESULT;
#define E_INVALIDARG ((HRESULT)0x80070057)
typedef enum { DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_R8G8B8A8_UNORM } DXGI_FORMAT;
typedef enum { D3D10_DDI_MAP_READ = 1, D3D10_DDI_MAP_WRITE, D3D10_DDI_MAP_READWRITE,
  D3D10_DDI_MAP_WRITE_DISCARD, D3D10_DDI_MAP_WRITE_NOOVERWRITE } D3D10_DDI_MAP;
enum { D3D10_DDI_USAGE_DEFAULT, D3D10_DDI_USAGE_IMMUTABLE, D3D10_DDI_USAGE_DYNAMIC,
  D3D10_DDI_USAGE_STAGING };
enum { D3D10_DDI_BIND_VERTEX_BUFFER = 1, D3D10_DDI_BIND_INDEX_BUFFER = 2,
  D3D10_DDI_BIND_CONSTANT_BUFFER = 4, D3D10_DDI_BIND_SHADER_RESOURCE = 8 };
enum { D3D10_DDI_CPU_ACCESS_WRITE = 0x10000, D3D10_DDI_CPU_ACCESS_READ = 0x20000,
  D3D10_DDI_CPU_ACCESS_MASK = 0x30000 };
#define D3D10_DDI_MAP_FLAG_DONOTWAIT 0x100000u
struct pipe_resource { int unused; };
struct pipe_transfer { int unused; };
typedef struct { int unused; } Device;
typedef struct {
  bool buffer, constant_buffer; UINT usage, bind_flags, cpu_access; DXGI_FORMAT Format;
  Device *owner_device; struct pipe_resource *resource; struct pipe_transfer **transfers;
  UINT NumSubResources;
} Resource;
typedef struct { void *pData; UINT RowPitch, DepthPitch; } D3D10DDI_MAPPED_SUBRESOURCE;
typedef Device *D3D10DDI_HDEVICE;
typedef Resource *D3D10DDI_HRESOURCE;
static Device *CastDevice(D3D10DDI_HDEVICE d) { return d; }
static Resource *CastResource(D3D10DDI_HRESOURCE r) { return r; }
static bool AgxD3d10MapFormat(DXGI_FORMAT f) { return f != DXGI_FORMAT_UNKNOWN; }
static LONG InterlockedIncrement(volatile LONG *v) { return ++*v; }
static void AgxD3d10WindowsDiagnostic(const char *n, HRESULT h, const UINT *v, UINT c) {
  (void)n; (void)h; (void)v; (void)c; }
static int rule(D3D10DDI_HDEVICE hDevice, D3D10DDI_HRESOURCE hResource, UINT SubResource,
                D3D10_DDI_MAP DDIMap, UINT Flags, D3D10DDI_MAPPED_SUBRESOURCE *pMappedSubResource) {
@@RULE@@
}
static Device dev, other;
static struct pipe_resource pr;
static struct pipe_transfer pt, *slots[2];
static D3D10DDI_MAPPED_SUBRESOURCE out;
static Resource make(bool buffer, bool cb, UINT usage, UINT bind, UINT cpu) {
  Resource r = {buffer, cb, usage, bind, cpu, DXGI_FORMAT_R8G8B8A8_UNORM, &dev, &pr, slots, 1};
  return r;
}
static int map(Resource *r, D3D10_DDI_MAP m, UINT flags) { return rule(&dev, r, 0, m, flags, &out); }
int main(void) {
  Resource vb = make(true, false, D3D10_DDI_USAGE_DYNAMIC, D3D10_DDI_BIND_VERTEX_BUFFER, D3D10_DDI_CPU_ACCESS_WRITE);
  Resource cb = make(true, true, D3D10_DDI_USAGE_DYNAMIC, D3D10_DDI_BIND_CONSTANT_BUFFER, D3D10_DDI_CPU_ACCESS_WRITE);
  Resource srv = make(true, false, D3D10_DDI_USAGE_DYNAMIC, D3D10_DDI_BIND_SHADER_RESOURCE, D3D10_DDI_CPU_ACCESS_WRITE);
  Resource vbsrv = make(true, false, D3D10_DDI_USAGE_DYNAMIC,
                        D3D10_DDI_BIND_VERTEX_BUFFER | D3D10_DDI_BIND_SHADER_RESOURCE, D3D10_DDI_CPU_ACCESS_WRITE);
  Resource tex = make(false, false, D3D10_DDI_USAGE_DYNAMIC, D3D10_DDI_BIND_SHADER_RESOURCE, D3D10_DDI_CPU_ACCESS_WRITE);
  Resource stage = make(false, false, D3D10_DDI_USAGE_STAGING, 0, D3D10_DDI_CPU_ACCESS_READ);
  Resource defcb = make(true, true, D3D10_DDI_USAGE_DEFAULT, D3D10_DDI_BIND_CONSTANT_BUFFER, 0);
  Resource *dyn[] = {&vb, &cb, &srv, &vbsrv};
  for (unsigned i = 0; i < 4; ++i) {
    assert(map(dyn[i], D3D10_DDI_MAP_WRITE_DISCARD, 0));
    assert(map(dyn[i], D3D10_DDI_MAP_WRITE_NOOVERWRITE, 0));
    assert(!map(dyn[i], D3D10_DDI_MAP_WRITE, 0));
    assert(!map(dyn[i], D3D10_DDI_MAP_READ, 0));
    assert(!map(dyn[i], D3D10_DDI_MAP_WRITE_NOOVERWRITE, D3D10_DDI_MAP_FLAG_DONOTWAIT));
  }
  assert(map(&tex, D3D10_DDI_MAP_WRITE_DISCARD, 0));
  assert(!map(&tex, D3D10_DDI_MAP_WRITE_NOOVERWRITE, 0));
  assert(map(&stage, D3D10_DDI_MAP_READ, D3D10_DDI_MAP_FLAG_DONOTWAIT));
  assert(!map(&stage, D3D10_DDI_MAP_WRITE, 0));
  assert(!map(&defcb, D3D10_DDI_MAP_WRITE_DISCARD, 0));
  assert(!map(&defcb, D3D10_DDI_MAP_WRITE_NOOVERWRITE, 0));
  cb.owner_device = &other;
  assert(!map(&cb, D3D10_DDI_MAP_WRITE_NOOVERWRITE, 0));
  cb.owner_device = &dev; slots[0] = &pt;
  assert(!map(&cb, D3D10_DDI_MAP_WRITE_NOOVERWRITE, 0));
  slots[0] = NULL;
  assert(!rule(&dev, &cb, 1, D3D10_DDI_MAP_WRITE_NOOVERWRITE, 0, &out));
  assert(!rule(&dev, &cb, 0, D3D10_DDI_MAP_WRITE_NOOVERWRITE, 0, NULL));
  return 0;
}
'''


class D3d10MapNoOverwriteRules(unittest.TestCase):
    def test_dynamic_buffers_accept_no_overwrite(self):
        program = SHIM.replace("@@RULE@@", rule_block())
        with tempfile.TemporaryDirectory(prefix="d3d10-map-rules-") as tmp:
            src = Path(tmp) / "rules.c"; exe = Path(tmp) / "rules"
            src.write_text(program)
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-function", "-Wno-unused-parameter",
                            "-fsanitize=address,undefined", str(src), "-o", str(exe)], check=True)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
