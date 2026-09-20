"""EXP695: execute the actual projected sampler-binding body on legal ranges."""
import ast
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SamplerRangeTests(unittest.TestCase):
    def test_full_partial_zero_and_atomic_rejection(self):
        tree = ast.parse((ROOT / 'drivers/apple-agx/mesa/scripts/build-native-asahi-state.py').read_text())
        bodies = [ast.literal_eval(n.args[2]) for n in ast.walk(tree)
                  if isinstance(n, ast.Call) and isinstance(n.func, ast.Name)
                  and n.func.id == 'replace_function_body' and len(n.args) >= 3
                  and isinstance(n.args[1], ast.Constant) and n.args[1].value == 'SetSamplers']
        self.assertEqual(len(bodies), 1)
        shim = r'''
#include <cassert>
#include <cstring>
#include <cstdint>
using UINT=unsigned;using HRESULT=int;

#define S_OK 0
#define E_NOTIMPL (-1)

#define PIPE_MAX_SAMPLERS 16
#define D3D10_COMMONSHADER_SAMPLER_SLOT_COUNT 16
enum mesa_shader_stage { MESA_SHADER_VERTEX, MESA_SHADER_FRAGMENT, MESA_SHADER_GEOMETRY };
struct Device;
struct SamplerState { Device *owner_device; void *handle; };
struct D3D10DDI_HSAMPLER { void *pDrvPrivate; };
struct Pipe {
  void (*bind_sampler_states)(Pipe *,mesa_shader_stage,unsigned,unsigned,void **);
};
struct Device { void *samplers[3][16]; Pipe *pipe; };
using D3D10DDI_HDEVICE=Device *;
static Device *CastDevice(Device *d){return d;}
static SamplerState *CastSamplerState(D3D10DDI_HSAMPLER h){return (SamplerState *)h.pDrvPrivate;}
static void *CastPipeSamplerState(D3D10DDI_HSAMPLER h){return h.pDrvPrivate?CastSamplerState(h)->handle:nullptr;}
static unsigned errors,calls,lastStart,lastCount;
static void SetError(Device *,int){++errors;}
static void AgxD3d10WindowsDiagnostic(const char *,int,const unsigned *,unsigned){}
static void bind(Pipe *,mesa_shader_stage,unsigned start,unsigned count,void **){
  assert(start<=16 && count<=16-start);++calls;lastStart=start;lastCount=count;
}
static void SetSamplers(mesa_shader_stage shader_type,Device *hDevice,
                       UINT Offset,UINT NumSamplers,const D3D10DDI_HSAMPLER *phSamplers) {
'''
        test = r'''
}
int main(){
 Pipe p={bind};Device d={},other={};d.pipe=&p;
 SamplerState owned={&d,(void *)(uintptr_t)0x1234},foreign={&other,(void *)(uintptr_t)0x5678};
 D3D10DDI_HSAMPLER empty[16]={},one={&owned},bad[3]={{&owned},{nullptr},{&foreign}};
 for(unsigned stage=0;stage<3;++stage){
  for(unsigned i=0;i<16;++i)d.samplers[stage][i]=owned.handle;
  unsigned before=calls;
  SetSamplers((mesa_shader_stage)stage,&d,0,16,empty);
  assert(errors==0 && calls==before+1 && lastStart==0 && lastCount==16);
  for(unsigned i=0;i<16;++i)assert(d.samplers[stage][i]==nullptr);
 }
 SetSamplers(MESA_SHADER_FRAGMENT,&d,0,1,&one);
 unsigned before=calls;
 SetSamplers(MESA_SHADER_FRAGMENT,&d,0,0,nullptr);
 assert(errors==0 && calls==before && d.samplers[1][0]==owned.handle);
 SetSamplers(MESA_SHADER_FRAGMENT,&d,15,1,&one);
 assert(errors==0 && d.samplers[1][15]==owned.handle && lastStart==15 && lastCount==1);
 SetSamplers(MESA_SHADER_FRAGMENT,&d,4,8,empty);
 assert(errors==0 && d.samplers[1][0]==owned.handle && d.samplers[1][15]==owned.handle);
 for(unsigned i=4;i<12;++i)assert(d.samplers[1][i]==nullptr);
 void *snapshot[16];std::memcpy(snapshot,d.samplers[1],sizeof(snapshot));before=calls;
 SetSamplers(MESA_SHADER_FRAGMENT,&d,0,3,bad);
 assert(errors==1 && calls==before && std::memcmp(snapshot,d.samplers[1],sizeof(snapshot))==0);
 SetSamplers(MESA_SHADER_FRAGMENT,&d,15,2,empty);
 SetSamplers(MESA_SHADER_FRAGMENT,&d,0xffffffffu,2,empty);
 SetSamplers(MESA_SHADER_FRAGMENT,&d,1,1,nullptr);
 assert(errors==4 && calls==before && std::memcmp(snapshot,d.samplers[1],sizeof(snapshot))==0);
 SetSamplers(MESA_SHADER_FRAGMENT,&d,0,16,empty);
 for(unsigned i=0;i<16;++i)assert(d.samplers[1][i]==nullptr);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            (p / 'samplers.cpp').write_text(shim + bodies[0] + test)
            subprocess.run([os.environ.get('CC', 'clang'), '-x', 'c++', '-std=c++17',
                            '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                            str(p / 'samplers.cpp'), '-o', str(p / 'samplers')], check=True)
            subprocess.run([str(p / 'samplers')], check=True)
