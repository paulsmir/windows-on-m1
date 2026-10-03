"""Native GPUVA contexts must select their implemented shared-resource path.

Compile the actual projected CreateDevice success-tail, keeping legacy/base and
non-GPUVA policy separate. This tests routing, not D3D9 compatibility or pixels.
"""
import ast
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
SCRIPT=ROOT/'drivers/apple-agx/mesa/scripts/build-native-asahi-state.py'


def source_parts():
    strings=[n.value for n in ast.walk(ast.parse(SCRIPT.read_text()))
             if isinstance(n,ast.Constant) and isinstance(n.value,str)]
    tails=[s for s in strings if s.startswith('   pDevice->frontend_ready = true;\n')]
    assert len(tails)==1
    helper=''
    for s in strings:
        marker='static HRESULT\nAgxNativeSharedPresentationStatus('
        if marker not in s:
            continue
        start=s.index(marker);brace=s.index('{',start);end=brace+1;depth=1
        while depth:
            depth+=(s[end]=='{')-(s[end]=='}');end+=1
        helper=s[start:end]
    return tails[0],helper,strings


class NativeSharedPresentationPolicyTests(unittest.TestCase):
    def replay(self,gpuva):
        tail,helper,strings=source_parts()
        # The pinned upstream suffix of this project-owned replacement remains
        # the same legacy fallback; only the success-tail condition is projected.
        code=r'''
#include <cassert>
#include <cstdint>
using UINT=uint32_t;using HRESULT=int32_t;
#define S_OK ((HRESULT)0)
#define DXGI_STATUS_NO_REDIRECTION ((HRESULT)0x087a0004)
// Model a caller-supplied base versus extended table. The actual WDK predicate
// is compiled by the ARM64 build; this shim does not duplicate its ABI logic.
#define IS_DXGI1_1_BASE_FUNCTIONS(i,v) ((void)(v),(i)==0xa0006u)
struct Device {bool frontend_ready;HRESULT cleanup_result;};
struct Args {UINT Interface,Version;};
'''+helper+r'''
static HRESULT success_tail(Device *pDevice,Args *pCreateData){
(void)pCreateData;
'''+tail+r'''
   } else {
      return DXGI_STATUS_NO_REDIRECTION;
   }
}
int main(){
 Device d{};Args extended{0xa0006,0x177a},base{0xa0000,0x177a};
#ifdef APPLE_AGX_GPUVA_WINSYS
 assert(success_tail(&d,&extended)==S_OK);
#else
 assert(success_tail(&d,&extended)==DXGI_STATUS_NO_REDIRECTION);
#endif
 assert(d.frontend_ready && d.cleanup_result==S_OK);
 assert(success_tail(&d,&base)==DXGI_STATUS_NO_REDIRECTION);
}
'''
        with tempfile.TemporaryDirectory(prefix='shared-policy-') as temp:
            source=Path(temp)/'test.cpp';binary=Path(temp)/'test';source.write_text(code)
            cmd=[os.environ.get('CXX','clang++'),'-std=c++17','-Wall','-Wextra','-Werror',
                 '-fsanitize=address,undefined']
            if gpuva:cmd+=['-DAPPLE_AGX_GPUVA_WINSYS=1']
            subprocess.run(cmd+[str(source),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=10)
        if gpuva:
            assert any('AdmissionUmdMeasureNativeDevice,\n      AgxNativeSharedPresentationStatus(' in s
                       for s in strings), 'receipt must report the selected policy'

    def test_native_gpuva_extended_table_uses_shared_path(self):
        self.replay(True)

    def test_legacy_and_software_fallback_unchanged(self):
        self.replay(False)
