"""Offline guards for the C76A process boundary, not a GPU simulation."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PROBE = ROOT / 'drivers/apple-agx/windows/icd/agx_d3d12_probe.c'

class SharedOpenProbe(unittest.TestCase):
    def test_open_uses_child_device_and_retains_import(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        EXP1913 never reached OpenSharedHandle because a second in-process
        device factory refused creation. Require process dispatch before normal
        adapter enumeration and preserve the two-write import lifetime test.
        """
        source = PROBE.read_text()
        section = source[source.index('/* C76A:'):source.index('static int texture_clear_copy')]
        self.assertNotIn('ID3D12DeviceFactory', section)
        self.assertIn('CreateProcessW(', section)
        self.assertIn('PROC_THREAD_ATTRIBUTE_HANDLE_LIST', section)
        self.assertIn('ID3D12Device_OpenSharedHandle(device,', section)
        self.assertIn('IDXGIFactory4_EnumAdapterByLuid', source)
        main = source[source.index('int main('):]
        self.assertLess(main.index('c76_child_main('), main.index('CreateDXGIFactory1('))
        self.assertIn('run_isolated(adapter, "C76A_CHILD", c76_child_read)', source)
        self.assertIn('GetExitCodeProcess', section)
        self.assertIn('TerminateProcess', section)
        self.assertIn('passes == 2', section)
        self.assertLess(section.index('ID3D12Resource_Release(created); created = NULL;'),
                        section.index('SetEvent(go)'))
        self.assertIn('kmd_open=UNVERIFIED', section)

    def test_child_preserves_warp_debug_validation(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Moving the consumer into a process must not silently disable debug
        validation and message collection for the existing warp C76A route.
        """
        source = PROBE.read_text()
        child = source[source.index('static int c76_child_main('):source.index('/* Original C25:')]
        self.assertIn('g_warp = (int)values[7]', child)
        self.assertLess(child.index('ID3D12Debug_EnableDebugLayer'),
                        child.index('run_isolated(adapter, "C76A_CHILD"'))
        parent = source[source.index('static int c76_shared_open('):source.index('static int texture_clear_copy')]
        self.assertIn('(unsigned)g_warp', parent)

    def test_wait_refuses_timeout_peer_exit_and_failure(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        A dead child or timeout must never count as an acknowledged open/read.
        Execute the actual wait helper with each Windows wait result.
        """
        source = PROBE.read_text()
        self.assertIn('static HRESULT c76_wait(', source)
        start = source.index('static HRESULT c76_wait(')
        helper = source[start:source.index('\n}', start)+2]
        code = r'''
#include <assert.h>
#include <stdio.h>
typedef void *HANDLE; typedef unsigned long DWORD; typedef int HRESULT;
#define FALSE 0
#define WAIT_OBJECT_0 0
#define S_OK 0
#define E_FAIL (-1)
static DWORD result;
static DWORD WaitForMultipleObjects(DWORD n, const HANDLE *h, int all, DWORD ms) {
 assert(n==2 && h[0]==(HANDLE)1 && h[1]==(HANDLE)2 && !all && ms==15000);
 return result;
}
'''+helper+r'''
int main(void) {
 for (result=0; result<260; ++result)
  assert(c76_wait((HANDLE)1,(HANDLE)2,"test")== (result==0?S_OK:E_FAIL));
 result=0xffffffff; assert(c76_wait((HANDLE)1,(HANDLE)2,"test")==E_FAIL);
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp)/'wait.c'; exe=Path(tmp)/'wait';src.write_text(code)
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror',str(src),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,stdout=subprocess.DEVNULL)

    def test_child_requires_two_fresh_readbacks_from_one_open(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        The consumer must open once, keep the import, check every pixel twice,
        and fail on an open error, stale second write, or failed handoff.
        Execute the production consumer with external Windows/D3D calls faked;
        this checks probe control/data validation, not GPU or runtime behavior.
        """
        source = PROBE.read_text()
        start = source.index('static int c76_child_read(')
        body = source[start:source.index('\n}', start)+2]
        shim = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int HRESULT;
typedef unsigned long long UINT64;
typedef void *HANDLE;
typedef int ID3D12Device, ID3D12CommandQueue, ID3D12Fence;
typedef int ID3D12Resource, ID3D12CommandAllocator, ID3D12GraphicsCommandList;
typedef struct {unsigned Dimension,Width,Height,Format,MipLevels,DepthOrArraySize;
 struct {unsigned Count,Quality;} SampleDesc; unsigned Flags;} D3D12_RESOURCE_DESC;
typedef struct {ID3D12Resource *pResource; unsigned Type;} D3D12_TEXTURE_COPY_LOCATION;
typedef struct {unsigned Begin,End;} D3D12_RANGE;
#define SUCCEEDED(h) ((h)>=0)
#define FAILED(h) ((h)<0)
#define E_FAIL (-1)
#define HRESULT_FROM_WIN32(x) (-(int)(x))
#define D3D12_HEAP_TYPE_READBACK 3
#define D3D12_RESOURCE_STATE_COPY_DEST 4
#define D3D12_RESOURCE_STATE_COMMON 0
#define D3D12_RESOURCE_STATE_COPY_SOURCE 8
#define D3D12_COMMAND_LIST_TYPE_DIRECT 0
#define D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX 0
static int IID_ID3D12Resource, IID_ID3D12CommandAllocator, IID_ID3D12GraphicsCommandList;
static HANDLE c76_shared=(HANDLE)1,c76_ready=(HANDLE)2,c76_go=(HANDLE)3,c76_done=(HANDLE)4,c76_parent=(HANDLE)5;
static int device,queue,fence,opened,readback,allocator,list;
static unsigned opens,reads,done,waits,releases,mode;
static unsigned char data[29*256];
static D3D12_RESOURCE_DESC c76_description(void) {
 D3D12_RESOURCE_DESC d={2,37,29,28,1,1,{1,0},1}; return d;
}
static D3D12_TEXTURE_COPY_LOCATION c76_footprint(ID3D12Resource *r) {
 D3D12_TEXTURE_COPY_LOCATION l={r,0};return l;
}
static HRESULT ID3D12Device_OpenSharedHandle(ID3D12Device *d,HANDLE h,const int *iid,void **out) {
 assert(d==&device && h==(HANDLE)1 && iid==&IID_ID3D12Resource); ++opens;
 if(mode==1)return E_FAIL;*out=&opened;return 0;
}
static void ID3D12Resource_GetDesc(ID3D12Resource *r,D3D12_RESOURCE_DESC *d) {
 assert(r==&opened);*d=c76_description(); if(mode==4)d->Width++;
}
static ID3D12Resource *committed_buffer(ID3D12Device *d,int h,int s) {
 assert(d==&device && h==3 && s==4);return &readback;
}
static HRESULT ID3D12Device_CreateCommandAllocator(ID3D12Device *d,int t,const int *iid,void **out) {
 assert(d==&device && !t && iid==&IID_ID3D12CommandAllocator);*out=&allocator;return 0;
}
static HRESULT ID3D12Device_CreateCommandList(ID3D12Device *d,int n,int t,ID3D12CommandAllocator *a,void *p,const int *iid,void **out) {
 assert(d==&device && !n && !t && a==&allocator && !p && iid==&IID_ID3D12GraphicsCommandList);*out=&list;return 0;
}
static int SetEvent(HANDLE h) {assert(h==c76_ready || h==c76_done);if(h==c76_done)++done;return 1;}
static unsigned long GetCurrentProcessId(void){return 123;}
static unsigned GetLastError(void){return 1;}
static void CloseHandle(HANDLE h){assert(h==(HANDLE)1);}
static HRESULT c76_wait(HANDLE e,HANDLE p,const char *s) {
 assert(e==c76_go && p==c76_parent && !strcmp(s,"consumer-go"));++waits;return mode==3?E_FAIL:0;
}
static HRESULT ID3D12CommandAllocator_Reset(ID3D12CommandAllocator *a){assert(a==&allocator && reads==1);return 0;}
static HRESULT ID3D12GraphicsCommandList_Reset(ID3D12GraphicsCommandList *l,ID3D12CommandAllocator *a,void *p){assert(l==&list && a==&allocator && !p);return 0;}
static void transition(ID3D12GraphicsCommandList *l,ID3D12Resource *r,int b,int a){assert(l==&list && r==&opened && (a+b)==8);}
static void ID3D12GraphicsCommandList_CopyTextureRegion(ID3D12GraphicsCommandList *l,const D3D12_TEXTURE_COPY_LOCATION *t,int x,int y,int z,const D3D12_TEXTURE_COPY_LOCATION *s,void *box){
 assert(l==&list && t->pResource==&readback && s->pResource==&opened && !x && !y && !z && !box);
 for(unsigned row=0;row<29;++row)for(unsigned col=0;col<37*4;++col)
  data[row*256+col]=(unsigned char)(col*7+row*11+(mode==2?0:reads)*83);
}
static HRESULT run_list(ID3D12CommandQueue *q,ID3D12Fence *f,ID3D12GraphicsCommandList *l,UINT64 s,const char *name){
 assert(q==&queue && f==&fence && l==&list && s==reads+1 && !strcmp(name,"C76A consumer"));return 0;
}
static HRESULT ID3D12Resource_Map(ID3D12Resource *r,int sub,D3D12_RANGE *range,void **p){
 assert(r==&readback && !sub && range->End==sizeof(data));*p=data;++reads;return 0;
}
static void ID3D12Resource_Unmap(ID3D12Resource *r,int sub,void *range){assert(r==&readback && !sub && !range);}
static void ID3D12Resource_Release(ID3D12Resource *r){assert(r==&opened || r==&readback);++releases;}
static void ID3D12CommandAllocator_Release(ID3D12CommandAllocator *a){assert(a==&allocator);}
static void ID3D12GraphicsCommandList_Release(ID3D12GraphicsCommandList *l){assert(l==&list);}
#define ExitProcess(code) exit(code)
'''
        main = r'''
int main(void) {
 for(mode=0;mode<5;++mode) {
  opens=reads=done=waits=releases=0;c76_shared=(HANDLE)1;
  int result=c76_child_read(&device,&queue,&fence,0);
  assert(result==(mode?1:0));assert(opens==1 && !c76_shared);
  if(!mode)assert(reads==2 && done==2 && waits==2 && releases==2);
  if(mode==1)assert(!reads && !done && !waits && !releases);
  if(mode==2)assert(reads==2 && done==1 && releases==2);
  if(mode==3)assert(!reads && !done && waits==1 && releases==2);
  if(mode==4)assert(!reads && !done && !waits && releases==1);
 }
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp)/'child.c';exe=Path(tmp)/'child';src.write_text(shim+body+main)
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(src),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,stdout=subprocess.DEVNULL,timeout=15)
