"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
RC5 must join full 64-bit allocation identities and distinguish an absent
reference from a resident reference and an actual MakeResident request.
Truncated addresses, overflowed interval tests or missing negative receipts
would make the single permitted diagnostic run inconclusive again.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
INC = ROOT / 'drivers/apple-agx/render-admission/umd/include'

class VaWatch(unittest.TestCase):
    def test_parse_and_full_width_interval(self):
        self.assertTrue((INC / 'umd_va_watch.h').exists(), 'missing full-width watch implementation')
        code = r'''
#include <cassert>
#include "umd_va_watch.h"
int main() {
 assert(AdmissionUmdWatchParse("0x115d960000")==0x115d960000ull);
 assert(AdmissionUmdWatchParse("0XABCDEF0123456789")==0xabcdef0123456789ull);
 for(auto s : {"", "0x", "-1", "1", "0x1z", "0x10000000000000000"})
   assert(!AdmissionUmdWatchParse(s));
 assert(AdmissionUmdWatchContains(0x115d960001ull,0x115d960000ull,0x10000));
 assert(!AdmissionUmdWatchContains(0x125d960001ull,0x115d960000ull,0x10000));
 assert(!AdmissionUmdWatchContains(0x115d970000ull,0x115d960000ull,0x10000));
 assert(!AdmissionUmdWatchContains(1,2,~0ull));
 assert(AdmissionUmdWatchContains(~0ull,~0ull-1,2));
 assert(!AdmissionUmdWatchContains(1,1,0));
}
'''.replace('#include <cassert>', '#include <cassert>\n#include <initializer_list>')
        with tempfile.TemporaryDirectory() as d:
            src=Path(d)/'test.cpp'; exe=Path(d)/'test';src.write_text(code)
            subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-I',str(INC),str(src),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_residency_receipts_from_production_functions(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        A slot omitted from the submission must say member=0; restored slots
        must report the callback and paging fence, including eviction failure.
        """
        from test_umd_persistent_residency import BODY, PersistentResidency
        pre = BODY[:BODY.index('int main(){')]
        pre = pre.replace('static void AdmissionUmdDiagnostic(const char*,HRESULT,const UINT*,UINT){}', r'''
static UINT last[16], summary[8], trims; static HRESULT result;
static void AdmissionUmdDiagnostic(const char *name,HRESULT hr,const UINT *v,UINT n){
 if(!strcmp(name,"measure-watch-slot")){assert(n==16);memcpy(last,v,sizeof(last));result=hr;}
 if(!strcmp(name,"measure-watch-set")){assert(n==8);memcpy(summary,v,sizeof(summary));}
 if(!strcmp(name,"measure-watch-trim")){assert(n==7);++trims;assert(v[3]==0x5d960000 && v[4]==0x11);}
}
''')
        main = r'''
int main(){
 setenv("APPLE_AGX_UMD_WATCH_VA","0x115d960000",1);
 CB cb={mk,ev}; ADMISSION_UMD_DEVICE d={}; d.Magic=ADMISSION_UMD_DEVICE_MAGIC; d.PagingQueue=(HANDLE)1;d.KernelCallbacks=&cb;
 SLOT &s=d.ScreenBuffers[0];s.Active=1;s.Token=0x10000000aull;s.KernelAllocation=0x123;
 s.StagingAllocation=1;s.CanonicalGpuVa=0x115d960000ull;s.Bytes=0x10000;
 uint64_t token=s.Token,fence=0;
 assert(make_resident(&d,&token,1,&fence)==2);
 assert(last[0]==2 && last[1]==10 && last[2]==1 && last[3]==0x123);
 assert(last[4]==0x5d960000 && last[5]==0x11 && last[8]==1 && last[9]==1);
 assert(last[10]==1 && last[12]==7 && result==E_PENDING);
 assert(evict(&d,&token,1)==1);
 assert(make_resident(&d,&token,1,&fence)==1 && made==1);
 assert(last[0]==1 && last[8]==1 && last[9]==0 && last[10]==1);
 assert(evict(&d,&token,1)==1);
 watch_slots(&d,1,nullptr,0,nullptr,0,0,S_OK);
 assert(last[8]==0 && last[9]==0 && last[10]==1 && summary[3]==1);
 UINT trimmed=0;evict_result=E_FAIL;
 trim_residency(&d,1,&trimmed);
 assert(trims==1 && last[0]==5 && result==E_FAIL && s.Resident);
 evict_result=S_OK;trim_residency(&d,1,&trimmed);
 assert(trims==2 && !s.Resident);
 assert(make_resident(&d,&token,1,&fence)==2 && made==2);
 assert(last[0]==2 && last[9]==1 && last[12]==7);
 s.Active=0;watch_slots(&d,1,nullptr,0,nullptr,0,0,S_OK);
 assert(summary[3]==0);
 puts("RC5 watch replay: PASS");
}
'''
        PersistentResidency().run_replay(pre+main,'RC5 watch replay: PASS')

    def test_identity_is_wired_before_creation_and_on_reuse(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Installing the hook after screen creation or missing the cache-hit
        path loses the label/identity of the very BO this run must name.
        """
        from test_umd_persistent_residency import function
        bo=(ROOT/'drivers/apple-agx/mesa/winsys/agx_win32_asahi_bo.c').read_text()
        owner=(ROOT/'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text()
        create=owner[owner.index('extern "C" HRESULT AgxD3d12OwnerCreate('):]
        self.assertLess(create.index('AgxWin32BoIdentityHook = owner_bo_identity'),create.index('HeapAlloc('))
        cache=bo[bo.index('if((bo=cache_take('):bo.index('/* EXP1081 receipt-only:')]
        self.assertLess(cache.index('bo_identity(2u'),cache.index('return &bo->Base'))
        self.assertIn('bo_identity(1u,&bo->Base)',bo)
        self.assertIn('bo_identity(3u,base)',bo)
        body=function(bo,'bo_identity')
        code=r'''
#include <cassert>
struct agx_va { unsigned long long addr; };
struct agx_bo { agx_va *va; unsigned handle; unsigned long long size; unsigned flags; const char *label; };
static unsigned calls;
static void record(unsigned event,unsigned token,unsigned long long va,unsigned long long bytes,unsigned flags,const char *label){
 ++calls;assert(event==2 && token==77 && va==0x115d960000ull && bytes==0x10000 && flags==3 && label[0]=='S');
}
void (*AgxWin32BoIdentityHook)(unsigned,unsigned,unsigned long long,unsigned long long,unsigned,const char *);
''' + body + r'''
int main(){agx_va va={0x115d960000ull};agx_bo b={&va,77,0x10000,3,"Staging"};
 bo_identity(2,&b);assert(!calls);AgxWin32BoIdentityHook=record;
 bo_identity(2,&b);assert(calls==1);bo_identity(2,nullptr);assert(calls==1);}
'''
        with tempfile.TemporaryDirectory() as d:
            src=Path(d)/'test.cpp';exe=Path(d)/'test';src.write_text(code)
            subprocess.run(['clang++','-std=c++17',str(src),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_resource_identity_joins_texture_to_canonical_slot(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        BufferGpuAddress deliberately returns zero for textures. Identity
        logging must instead resolve the token's canonical VA under the lock.
        """
        from test_umd_persistent_residency import BODY, function, PersistentResidency
        owner=(ROOT/'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text()
        pre=BODY[:BODY.index('int main(){')].replace(
            'static void AdmissionUmdDiagnostic(const char*,HRESULT,const UINT*,UINT){}',
            'static UINT receipt[16]; static void AdmissionUmdDiagnostic(const char *name,HRESULT,const UINT *v,UINT n){if(!strcmp(name,"measure-watch-resource")){assert(n==16);memcpy(receipt,v,sizeof(receipt));}}')
        extra=r'''
typedef uintptr_t ULONG_PTR;
struct pipe_resource {unsigned target,format,width0,height0,depth0,array_size;};
struct AGX_WIN32_RELOC_ALLOCATION {uint64_t Token;};
struct AGX_D3D12_OWNER {ADMISSION_UMD_DEVICE Runtime;};
static int AgxWin32AsahiResourceIdentity(pipe_resource*,AGX_WIN32_RELOC_ALLOCATION *id){id->Token=99;return 1;}
''' + function(owner,'owner_watch_resource') + r'''
int main(){setenv("APPLE_AGX_UMD_WATCH_VA","0x115d960000",1);
 AGX_D3D12_OWNER owner={};auto &s=owner.Runtime.ScreenBuffers[0];
 s.Active=1;s.Token=99;s.CanonicalGpuVa=0x115d960000ull;s.Bytes=65536;s.KernelAllocation=77;
 pipe_resource texture={2,28,64,64,1,1};owner_watch_resource(&owner,&texture,3);
 assert(receipt[0]==3 && receipt[3]==99 && receipt[5]==77);
 assert(receipt[6]==0x5d960000 && receipt[7]==0x11 && receipt[8]==65536);
 assert(receipt[12]==64 && receipt[13]==64 && owner.Runtime.ScreenBufferLock==0);
 puts("RC5 resource identity: PASS");}
'''
        PersistentResidency().run_replay(pre+extra,'RC5 resource identity: PASS')
