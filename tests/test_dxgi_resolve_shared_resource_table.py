"""EXP1331: WDDM 2.0 devices keep ResolveSharedResource and redirection.

Invariant caught: the frontend wrote pfnResolveSharedResource and reported
shared-resource presentation (S_OK instead of DXGI_STATUS_NO_REDIRECTION)
only when IS_DXGI1_1_BASE_FUNCTIONS held. That test reads the low word of
Version for a D3D11-major interface; the WDDM 2.0 runtime passes 0x00090000
(EXP1331 measure-native-device: interface 0x000b0020), so every FL 11_0
device left the entry NULL and returned NO_REDIRECTION, while the FL 10_0
devices (interface 0x000b000a, version 0x00020009) kept both. Every DXGI 1.2+
base table carries pfnResolveSharedResource (DXGI1_2..1_4_DDI_BASE_FUNCTIONS),
so IS_DXGI1_2_BASE_FUNCTIONS counts too. The two WDK predicates are modelled
from d3d10umddi.h's definitions for the pinned 26100 headers.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

from test_d3d10_shader_fields_init import project

PROGRAM = r'''
#include <assert.h>
#include <stdbool.h>
typedef unsigned UINT; typedef long HRESULT;
#define S_OK ((HRESULT)0)
#define DXGI_STATUS_NO_REDIRECTION ((HRESULT)0x087A0004L)
#define APPLE_AGX_GPUVA_WINSYS 1
/* d3d10umddi.h (26100): D3D10 major 10, D3D11 major 11, DXGI_RESOLVE_SHARED_RESOURCE 9,
 * VISTA_GOLD_PRODUCT_VER 0, D3D11_1_DDI_MINOR_VERSION_RC 1 (a D3D11.1 minor). */
#define IS_DXGI1_1_BASE_FUNCTIONS(I, V) \
  ((((I) >> 16) == 10 && ((V) & 0xffffu) >= 9u) || (((I) >> 16) == 11 && ((V) & 0xffffu) >= 9u) || \
   (((I) >> 16) > 11))
#define IS_DXGI1_2_BASE_FUNCTIONS(I, V) ((((I) >> 16) == 11 && ((I) & 0xffffu) >= 1u) || (((I) >> 16) > 11))
@@FUNCTIONS@@
int main(void) {
  /* EXP1326 FL 10_0 devices */
  assert(AgxDxgiHasResolveSharedResource(0x000b000au, 0x00020009u));
  assert(AgxNativeSharedPresentationStatus(0x000b000au, 0x00020009u) == S_OK);
  /* EXP1331 WDDM 2.0 (FL 11_0) devices */
  assert(AgxDxgiHasResolveSharedResource(0x000b0020u, 0x00090000u));
  assert(AgxNativeSharedPresentationStatus(0x000b0020u, 0x00090000u) == S_OK);
  /* A D3D10.0 runtime without the DXGI 1.1 table keeps the base contract. */
  assert(!AgxDxgiHasResolveSharedResource(0x000a0001u, 0x00000001u));
  assert(AgxNativeSharedPresentationStatus(0x000a0001u, 0x00000001u) == DXGI_STATUS_NO_REDIRECTION);
  return 0;
}
'''


def function(text, name):
    m = re.search(r"\n(static (?:bool|HRESULT)\n" + name + r"\(.*?\n\})", text, re.S)
    return m.group(1)


class DxgiResolveSharedResourceTable(unittest.TestCase):
    def test_wddm2_devices_keep_the_entry_and_redirection(self):
        with tempfile.TemporaryDirectory(prefix="dxgi-resolve-") as tmp:
            out = project(Path(tmp) / "out")
            if out is None:
                self.skipTest("reference Mesa tree not available")
            text = (out / "src/gallium/frontends/d3d10umd/Device.cpp").read_text()
            self.assertIn("if(AgxDxgiHasResolveSharedResource(pCreateData->Interface,pCreateData->Version))\n"
                          "      pCreateData->DXGIBaseDDI.pDXGIDDIBaseFunctions2->pfnResolveSharedResource =",
                          text)
            functions = function(text, "AgxDxgiHasResolveSharedResource") + "\n" + \
                function(text, "AgxNativeSharedPresentationStatus")
            src = Path(tmp) / "r.c"; exe = Path(tmp) / "r"
            src.write_text(PROGRAM.replace("@@FUNCTIONS@@", functions))
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Werror",
                            "-Wno-unused-function", str(src), "-o", str(exe)], check=True)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
