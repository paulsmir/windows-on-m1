"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Accepted graphics/compute shaders were absent from SHADER_DUMP_DIR, making a
successful-but-wrong image impossible to investigate with that switch. Execute
production translation entry and file writer under ASan/UBSan; only external
Win32 file/environment and compiler boundaries are replaced by host adapters.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_d3d12_owner_allocation_failure import OWNER, function

WINSYS = Path(__file__).resolve().parents[1] / 'drivers/apple-agx/mesa/winsys'

PRELUDE = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <map>
#include <vector>
#include <cstdarg>
#include "agx_win32_shader_stats.h"
using UINT=uint32_t; using DWORD=uint32_t; using HANDLE=int;
using mesa_shader_stage=unsigned;
#define MESA_SHADER_COMPUTE 5u
#define MAX_PATH 260
#define GENERIC_WRITE 1
#define CREATE_NEW 1
#define FILE_ATTRIBUTE_NORMAL 0
#define INVALID_HANDLE_VALUE (-1)
#define S_OK 0
#define E_FAIL (-1)
static std::map<std::string,std::string> env;
static std::map<std::string,std::vector<unsigned char>> files;
static std::string opened;
static unsigned writes, creates, translations, receipts;
static bool fail_open, fail_write, short_write;
static unsigned compiler_stage;
static std::vector<UINT> last_receipt;
static DWORD GetEnvironmentVariableA(const char *key,char *out,DWORD size) {
 auto it=env.find(key);if(it==env.end())return 0;
 if(it->second.size()>=size)return it->second.size()+1;
 memcpy(out,it->second.c_str(),it->second.size()+1);return it->second.size();
}
static int wsprintfA(char *out,const char *fmt,...) {
 va_list args;va_start(args,fmt);int n=vsprintf(out,fmt,args);va_end(args);return n;
}
static HANDLE CreateFileA(const char *path,unsigned,unsigned,void *,unsigned mode,unsigned,void *) {
 assert(mode==CREATE_NEW);++creates;
 if(fail_open||files.count(path))return INVALID_HANDLE_VALUE;
 opened=path;files[opened]={};return 1;
}
static bool WriteFile(HANDLE h,const void *data,DWORD n,DWORD *written,void *) {
 assert(h==1);++writes;*written=fail_write?0:short_write?n-1:n;
 auto p=static_cast<const unsigned char *>(data);
 files[opened]=std::vector<unsigned char>(p,p+*written);return !fail_write;
}
static void CloseHandle(HANDLE h){assert(h==1);}
static bool DeleteFileA(const char *path){return files.erase(path)!=0;}
template<class... T> static void receipt(const char *name,int status,T... fields){
 assert(!strcmp(name,"measure-d3d12-shader-input") && status==S_OK);
 last_receipt={static_cast<UINT>(fields)...};++receipts;
}
struct nir_shader {}; struct nir_shader_compiler_options {};
struct dxbc_binding_map {}; struct dxbc_nir_xfb {}; struct dxbc_nir_error {uint32_t opcode,offset;};
static nir_shader translated;
static const unsigned *shader_binding_limits(){return nullptr;}
static nir_shader *dxbc_translate_tokens_bindings_xfb(const UINT *,const nir_shader_compiler_options *,
 const unsigned *,dxbc_binding_map *,uint32_t,uint32_t,const dxbc_nir_xfb *,dxbc_nir_error *) {
 ++translations;return &translated;
}
enum agx_dxil_nir_status {AGX_DXIL_NIR_OK,AGX_DXIL_NIR_UNSUPPORTED};
struct agx_dxil_nir_result {nir_shader *nir;unsigned stage;dxbc_binding_map bindings;};
static bool agx_dxil_is_program(const UINT *input,size_t bytes) {
 assert(input[2]==0x4c495844 && bytes==24);return true;
}
static agx_dxil_nir_status agx_dxil_to_nir(const UINT *,size_t,const unsigned *,
 const nir_shader_compiler_options *,agx_dxil_nir_result *out) {
 ++translations;*out={&translated,compiler_stage,{}};return AGX_DXIL_NIR_OK;
}
static bool agx_dxil_nir_xfb(nir_shader *,const dxbc_nir_xfb *){return true;}
// RC74: the frontend NIR text of dump_shader_nir is the only allocation freed
static char nir_text[]="shader: MESA_SHADER_COMPUTE\n";
static unsigned nir_texts;
static char *nir_shader_as_str(nir_shader *nir,void *){assert(nir==&translated);++nir_texts;return nir_text;}
static void ralloc_free(void *p){assert(p==nir_text);}
static void reset(){env.clear();files.clear();writes=creates=translations=receipts=nir_texts=0;fail_open=fail_write=short_write=false;}
static const UINT tokens[]={0x10065,6,0x4c495844,0x100,0x10,0};
static void translate(unsigned stage){
 compiler_stage=stage;dxbc_binding_map map;dxbc_nir_error error;
 assert(translate_shader(tokens,stage,nullptr,&map,0,0,nullptr,&error)==&translated);
}
'''

class ShaderCapture(unittest.TestCase):
    def run_case(self, main):
        text=OWNER.read_text()
        name='dump_shader_input' if 'static void dump_shader_input(' in text else 'dump_failed_shader'
        # Define the file writer before the actual common translation entry.
        code=PRELUDE[:PRELUDE.index('static void translate(unsigned stage)')]
        code+=function(text,'static void '+name+'(')+'\n'
        code+=function(text,'static void dump_shader_nir(')+'\n'
        code+=function(text,'static struct nir_shader *translate_shader(')+'\n'
        code+=PRELUDE[PRELUDE.index('static void translate(unsigned stage)'):]
        code+=main.replace('DUMP_INPUT',name)
        with tempfile.TemporaryDirectory(prefix='shader-capture-') as td:
            path=Path(td);(path/'test.cpp').write_text(code)
            for dxil in (False, True):
                with self.subTest(dxil=dxil):
                    flags=['-DAPPLE_AGX_D3D12_DXIL'] if dxil else []
                    r=subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined',*flags,'-I',str(WINSYS),str(path/'test.cpp'),'-o',str(path/'test')],capture_output=True,text=True)
                    self.assertEqual(r.returncode,0,r.stderr)
                    r=subprocess.run([str(path/'test')],capture_output=True,text=True)
                    self.assertEqual(r.returncode,0,r.stdout+r.stderr)

    def test_accepted_vertex_fragment_compute_are_captured_exactly(self):
        self.run_case(r'''
int main(){reset();env["APPLE_AGX_D3D12_SHADER_DUMP_DIR"]="capture";
 env["APPLE_AGX_D3D12_SHADER_DUMP_ALL"]="1";
 for(unsigned stage:{0u,4u,5u})translate(stage);
 assert(translations==3);assert(files.size()==3 && writes==3 && receipts==3);
 for(unsigned stage:{0u,4u,5u})assert(files.count("capture\\8f28162bed108d89-s"+std::to_string(stage)+"-r0.bin"));
 assert((last_receipt==std::vector<UINT>{5,0,0x8f28162bu,0xed108d89u,24}));
 for(const auto &f:files){assert(f.first.find("-r0.bin")!=std::string::npos);
 assert(f.second.size()==sizeof(tokens));assert(!memcmp(f.second.data(),tokens,sizeof(tokens)));}
 translate(0);assert(translations==4 && writes==3); // CREATE_NEW dedup
 assert(tokens[0]==0x10065 && tokens[1]==6);
}
''')

    def test_default_and_failure_capture_contract(self):
        self.run_case(r'''
int main(){reset();translate(0);assert(!creates);
 env["APPLE_AGX_D3D12_SHADER_DUMP_ALL"]="1";translate(0);assert(!creates);
 env.erase("APPLE_AGX_D3D12_SHADER_DUMP_ALL");
 env["APPLE_AGX_D3D12_SHADER_DUMP_DIR"]="capture";translate(0);assert(!creates);
 for(const char *v:{"0","true","10",""}){env["APPLE_AGX_D3D12_SHADER_DUMP_ALL"]=v;translate(0);assert(!creates);}
 DUMP_INPUT(tokens,4,2);assert(writes==1 && files.size()==1);
 DUMP_INPUT(tokens,4,2);assert(writes==1); // existing failure dumps remain deduplicated
 env["APPLE_AGX_D3D12_SHADER_DUMP_DIR"]=std::string(240,'a');
 DUMP_INPUT(tokens,4,1);assert(writes==1);
 env["APPLE_AGX_D3D12_SHADER_DUMP_DIR"]="capture";
 DUMP_INPUT(nullptr,4,1);UINT bad[2]={0,1};DUMP_INPUT(bad,4,1);
 bad[1]=0x4000000;DUMP_INPUT(bad,4,1);assert(writes==1);
}
''')

    def test_capture_io_failure_preserves_translation_and_retries(self):
        self.run_case(r'''
int main(){reset();env["APPLE_AGX_D3D12_SHADER_DUMP_DIR"]="capture";
 env["APPLE_AGX_D3D12_SHADER_DUMP_ALL"]="1";
 fail_open=true;translate(0);assert(files.empty());fail_open=false;
 fail_write=true;translate(0);assert(files.empty());fail_write=false;
 short_write=true;translate(0);assert(files.empty());short_write=false;
 translate(0);assert(translations==4 && files.size()==1 && writes==3);
 assert(files.begin()->second.size()==sizeof(tokens) && receipts==1);
}
''')

    def test_frontend_nir_is_dumped_beside_the_tokens_on_request(self):
        # WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? RC74: the NIR dump
        # must be opt-in (APPLE_AGX_D3D12_SHADER_DUMP_NIR=1 with the dump
        # directory), named by the same FNV-1a 64 hash and stage as the token
        # capture so the two pair up, written once (CREATE_NEW), skip a
        # refused translation, and leave no file after a failed write.
        self.run_case(r'''
int main(){reset();env["APPLE_AGX_D3D12_SHADER_DUMP_DIR"]="capture";
 dump_shader_nir(tokens,MESA_SHADER_COMPUTE,&translated);assert(!nir_texts && files.empty());
 env["APPLE_AGX_D3D12_SHADER_DUMP_NIR"]="1";
 dump_shader_nir(tokens,MESA_SHADER_COMPUTE,nullptr);assert(!nir_texts && files.empty());
 dump_shader_nir(tokens,MESA_SHADER_COMPUTE,&translated);assert(nir_texts==1 && files.size()==1);
 const auto &f=files.at("capture\\8f28162bed108d89-s5.nir");
 assert(f.size()==strlen(nir_text) && !memcmp(f.data(),nir_text,f.size()));
 dump_shader_nir(tokens,MESA_SHADER_COMPUTE,&translated);assert(files.size()==1 && writes==1); // CREATE_NEW dedup
 files.clear();fail_write=true;dump_shader_nir(tokens,4,&translated);assert(files.empty());fail_write=false;
 env["APPLE_AGX_D3D12_SHADER_DUMP_NIR"]="0";dump_shader_nir(tokens,4,&translated);assert(files.empty());
}
''')

if __name__=='__main__':unittest.main()
