"""The render-admission UMD runtime accepts the D3D11.0 device interfaces.

EXP1294: with the D3D11 interface advertised (APPLE_AGX_D3D11_DDI=1),
D3D11CreateDevice returned E_INVALIDARG because
AdmissionUmdRuntimeDeviceInitialize knew only the D3D10.0 interfaces (10.0
core-layer callbacks) and WDDM 1.3 (11.0 callbacks).  The D3D11.0 interfaces
pass D3D11DDI_CORELAYER_DEVICECALLBACKS in the same union
(d3d10umddi.h D3D10DDIARG_CREATEDEVICE).

Invariant: AdmissionUmdRuntimeErrorCallback takes pfnSetErrorCb from
pUMCallbacks for D3D10_0/_x/_7, from p11UMCallbacks for D3D11_0/_7 and WDDM
1.3, from pWDDM2_0UMCallbacks for WDDM 2.0 (EXP1303: the runtime chose
D3DWDDM2_0_DDI_INTERFACE_VERSION once the FL 11_0 adapter reported 4.x
compute, and D3D11CreateDevice returned E_INVALIDARG from this function),
and refuses other interfaces and missing callback tables.  The function
is compiled from the source with the interface constants of the local WDK
header copy (skipped where absent).
"""
from pathlib import Path
import re
import os
import subprocess
import tempfile
import unittest

from test_d3d10_shader_fields_init import local_tree

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "drivers/apple-agx/render-admission/umd/src/umd_runtime_device.c"


class UmdD3d11InterfaceCallbacks(unittest.TestCase):
    def test_error_callback_by_interface(self):
        header = local_tree("reference", "wdk26100", "d3d10umddi.h")
        if header is None:
            self.skipTest("Windows SDK reference header not available")
        defines = "\n".join(re.findall(
            r"(?m)^#define (?:D3D1[01]|D3DWDDM1_3)\w*_(?:MAJOR|MINOR)_VERSION .*$|"
            r"^#define (?:D3D1[01]|D3DWDDM1_3)\w*_DDI_INTERFACE_VERSION .*$",
            header.read_text(errors="replace")))
        # the build's header (minor 6+) redefines WDDM 2.0 as 11.32
        self.assertIn("#define D3DWDDM2_0_DDI_MINOR_VERSION 32", header.read_text(errors="replace"))
        defines += "\n#define D3DWDDM2_0_DDI_INTERFACE_VERSION ((D3D11_DDI_MAJOR_VERSION << 16) | 32)\n"
        source = SOURCE.read_text()
        start = source.index("static BOOL\nAdmissionUmdRuntimeErrorCallback(")
        function = source[start:source.index("\n}\n", start) + 2]
        program = defines + r'''
#include <assert.h>
#include <stddef.h>
typedef int BOOL;
typedef unsigned UINT;
#define TRUE 1
#define FALSE 0
typedef void (*PFND3D10DDI_SETERROR_CB)(void);
typedef struct { PFND3D10DDI_SETERROR_CB pfnSetErrorCb; void *pfnStateVsConstBufCb; } D3D10DDI_CORELAYER_DEVICECALLBACKS;
typedef struct { PFND3D10DDI_SETERROR_CB pfnSetErrorCb; void *pfnStateVsConstBufCb; void *pfnStateCsUavCb; } D3D11DDI_CORELAYER_DEVICECALLBACKS;
typedef struct { PFND3D10DDI_SETERROR_CB pfnSetErrorCb; void *pfnStateVsConstBufCb; void *pfnStateCsUavCb; void *pfnCreateContextCb; } D3DWDDM2_0DDI_CORELAYER_DEVICECALLBACKS;
typedef struct {
  UINT Interface;
  union { const D3D10DDI_CORELAYER_DEVICECALLBACKS *pUMCallbacks;
          const D3D11DDI_CORELAYER_DEVICECALLBACKS *p11UMCallbacks;
          const D3DWDDM2_0DDI_CORELAYER_DEVICECALLBACKS *pWDDM2_0UMCallbacks; };
} D3D10DDIARG_CREATEDEVICE;
''' + function + r'''
static void ten(void) {}
static void eleven(void) {}
static void wddm2(void) {}
int main(void) {
  D3D10DDI_CORELAYER_DEVICECALLBACKS c10 = {ten, NULL};
  D3D11DDI_CORELAYER_DEVICECALLBACKS c11 = {eleven, NULL, NULL};
  const UINT v10[] = {D3D10_0_DDI_INTERFACE_VERSION, D3D10_0_x_DDI_INTERFACE_VERSION,
                      D3D10_0_7_DDI_INTERFACE_VERSION};
  const UINT v11[] = {D3D11_0_DDI_INTERFACE_VERSION, D3D11_0_7_DDI_INTERFACE_VERSION,
                      D3DWDDM1_3_DDI_INTERFACE_VERSION};
  PFND3D10DDI_SETERROR_CB cb;
  D3D10DDIARG_CREATEDEVICE a;
  for (unsigned i = 0; i < 3; ++i) {
    a.Interface = v10[i]; a.pUMCallbacks = &c10; cb = NULL;
    assert(AdmissionUmdRuntimeErrorCallback(&a, &cb) && cb == ten);
    a.pUMCallbacks = NULL;
    assert(!AdmissionUmdRuntimeErrorCallback(&a, &cb));
    a.Interface = v11[i]; a.p11UMCallbacks = &c11; cb = NULL;
    assert(AdmissionUmdRuntimeErrorCallback(&a, &cb) && cb == eleven);
    a.p11UMCallbacks = NULL;
    assert(!AdmissionUmdRuntimeErrorCallback(&a, &cb));
  }
  D3DWDDM2_0DDI_CORELAYER_DEVICECALLBACKS c20 = {wddm2, NULL, NULL, NULL};
  a.Interface = D3DWDDM2_0_DDI_INTERFACE_VERSION; a.pWDDM2_0UMCallbacks = &c20; cb = NULL;
  assert(AdmissionUmdRuntimeErrorCallback(&a, &cb) && cb == wddm2);
  a.pWDDM2_0UMCallbacks = NULL;
  assert(!AdmissionUmdRuntimeErrorCallback(&a, &cb));
  a.Interface = 0; a.p11UMCallbacks = &c11;
  assert(!AdmissionUmdRuntimeErrorCallback(&a, &cb));
  return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix="umd-d3d11-callbacks-") as tmp:
            src = Path(tmp) / "t.c"
            exe = Path(tmp) / "t"
            src.write_text(program)
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Werror", "-o",
                            str(exe), str(src)], check=True, capture_output=True)
            self.assertEqual(subprocess.run([str(exe)]).returncode, 0)


if __name__ == "__main__":
    unittest.main()
