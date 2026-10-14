"""EXP1287: SoSetTargets admits what the D3D10 DDI allows.

EXP1287 stream-output smoke (PACKAGE1287, modes i/h): SoSetTargets set
E_NOTIMPL (projected Shader.cpp line 1019) and the device was removed; the
EXP-era guard admitted exactly one VERTEX_BUFFER|STREAM_OUTPUT buffer at
offset 0, the smoke binds a STREAM_OUTPUT-only buffer. The DDI contract: at
most four slots, a NULL handle unbinds, every bound buffer belongs to the
device and has the stream-output bind flag, an offset is a 4-byte-aligned
byte offset or ~0u (append, Gallium's -1).
Bug caught: refusing a valid binding (the EXP1287 removal) or admitting a
foreign, non-SO or misaligned one.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "drivers/apple-agx/mesa/scripts/build-native-asahi-state.py"

PROGRAM = r'''
#include <assert.h>
#include <stddef.h>
typedef unsigned int UINT;
typedef long HRESULT;
#define E_INVALIDARG ((HRESULT)0x80070057L)
#define E_NOTIMPL ((HRESULT)0x80004001L)
#define D3D10_DDI_BIND_VERTEX_BUFFER 0x1u
#define D3D10_DDI_BIND_STREAM_OUTPUT 0x10u
#define D3D10_DDI_BIND_SHADER_RESOURCE 0x8u
enum pipe_texture_target { PIPE_BUFFER, PIPE_TEXTURE_2D };
struct pipe_resource { enum pipe_texture_target target; };
typedef struct Device { int unused; } Device;
typedef struct Resource { UINT bind_flags; Device *owner_device; struct pipe_resource *resource; } Resource;
typedef struct { void *pDrvPrivate; } D3D10DDI_HDEVICE;
typedef struct { void *pDrvPrivate; } D3D10DDI_HRESOURCE;
static HRESULT last_error;
static void SetError(D3D10DDI_HDEVICE h, HRESULT hr) { (void)h; last_error = hr; }
static Device *CastDevice(D3D10DDI_HDEVICE h) { return (Device *)h.pDrvPrivate; }
static Resource *CastResource(D3D10DDI_HRESOURCE h) { return (Resource *)h.pDrvPrivate; }
static int bound;
static void SoSetTargets(D3D10DDI_HDEVICE hDevice, UINT SOTargets, UINT ClearTargets,
                         const D3D10DDI_HRESOURCE *phResource, const UINT *pOffsets)
@@GUARD@@
   bound = 1;
}
int main(void) {
  Device dev = {0}, other = {0};
  struct pipe_resource buf = {PIPE_BUFFER}, tex = {PIPE_TEXTURE_2D};
  Resource so = {D3D10_DDI_BIND_STREAM_OUTPUT, &dev, &buf};
  Resource sovb = {D3D10_DDI_BIND_STREAM_OUTPUT | D3D10_DDI_BIND_VERTEX_BUFFER, &dev, &buf};
  Resource vb = {D3D10_DDI_BIND_VERTEX_BUFFER, &dev, &buf};
  Resource foreign = {D3D10_DDI_BIND_STREAM_OUTPUT, &other, &buf};
  Resource texture = {D3D10_DDI_BIND_STREAM_OUTPUT, &dev, &tex};
  D3D10DDI_HDEVICE h = {&dev};
  D3D10DDI_HRESOURCE r[4] = {{&so}, {&sovb}, {NULL}, {&so}};
  UINT zero[4] = {0, 0, 0, 0}, mixed[4] = {16, ~0u, 0, 64}, odd[1] = {2};
#define CALL(n, c, res, off) (last_error = 0, bound = 0, SoSetTargets(h, n, c, res, off))
  CALL(1, 0, r, zero); assert(!last_error && bound);        /* SO-only (EXP1287) */
  CALL(1, 0, &r[1], zero); assert(!last_error && bound);    /* VB|SO (EXP-era) */
  CALL(4, 0, r, mixed); assert(!last_error && bound);       /* four slots, NULL, append */
  CALL(0, 4, NULL, NULL); assert(!last_error && bound);     /* unbind all */
  CALL(2, 2, r, zero); assert(!last_error && bound);
  CALL(4, 1, r, zero); assert(last_error == E_INVALIDARG && !bound);
  CALL(1, 0, r, odd); assert(last_error == E_INVALIDARG && !bound);
  { D3D10DDI_HRESOURCE x[1] = {{&vb}}; CALL(1, 0, x, zero); assert(last_error == E_INVALIDARG && !bound); }
  { D3D10DDI_HRESOURCE x[1] = {{&foreign}}; CALL(1, 0, x, zero); assert(last_error == E_INVALIDARG && !bound); }
  { D3D10DDI_HRESOURCE x[1] = {{&texture}}; CALL(1, 0, x, zero); assert(last_error == E_INVALIDARG && !bound); }
  CALL(1, 0, r, NULL); assert(last_error == E_INVALIDARG && !bound);
  return 0;
}
'''


def guard(source):
    start = source.index("   Device *windows_device=CastDevice(hDevice);\n   if(!windows_device) { SetError(hDevice,E_INVALIDARG); return; }\n   ")
    start = source.rindex("{\n   unsigned i;\n", 0, start)
    end = source.index("\n   Device *pDevice = windows_device;", start)
    return source[start:end] + "\n"


class SoSetTargetsContract(unittest.TestCase):
    def test_guard_follows_the_ddi_contract(self):
        program = PROGRAM.replace("@@GUARD@@", guard(GENERATOR.read_text()))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "so_targets.c"
            exe = Path(directory) / "so_targets"
            path.write_text(program)
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                            "-Werror", "-Wno-unused-function", "-Wno-unused-variable",
                            "-fsanitize=address,undefined", str(path), "-o", str(exe)],
                           check=True, cwd=ROOT)
            ran = subprocess.run([str(exe)], cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
