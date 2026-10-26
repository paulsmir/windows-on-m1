"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

The native mapping escape drops the GPU lock before allocating/mapping. Its
KMD process must remain owned through that gap and cleanup; concurrent closing
must prevent publishing a pointer. Execute the actual escape with OS boundaries.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_apple_agx_pool_headroom import function

ROOT=Path(__file__).resolve().parents[1]

class NativeStagingEscape(unittest.TestCase):
    def test_real_escape_process_lifetime(self):
        body=function((ROOT/'drivers/apple-agx/render-admission/src/gpuva_g3_windows.c').read_text(),
                      'AdmissionGpuvaG3NativeStagingEscape')
        code=r'''
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include "render_win32_transport.h"
typedef int NTSTATUS;typedef int BOOLEAN;typedef unsigned ULONG;typedef void *PVOID;
typedef unsigned long long ULONGLONG;
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER -1
#define STATUS_INVALID_HANDLE -2
#define STATUS_DEVICE_BUSY -3
#define STATUS_INVALID_DEVICE_STATE -4
#define STATUS_INSUFFICIENT_RESOURCES -5
#define NT_SUCCESS(x) ((x)>=0)
#define TRUE 1
#define FALSE 0
#define MAXULONG 0xffffffffu
#define PASSIVE_LEVEL 0
#define ADMISSION_OBJECT_DEVICE_MAGIC 1
#define ADMISSION_OBJECT_CONTEXT_MAGIC 2
#define RtlCopyMemory memcpy
static int KeGetCurrentIrql(void){return 0;}
static void *PsGetCurrentProcessId(void){return (void*)55;}
#define HandleToULong(p) ((ULONG)(uintptr_t)(p))
typedef struct {ULONG NativeMapRefs;BOOLEAN NativeClosing,Poisoned;struct{int Uncertain;}Graph;ULONG OsProcessId;} ADMISSION_G3_PROCESS;
typedef struct {int Lock;ADMISSION_G3_PROCESS *Process;} ADMISSION_G3_STATE;
typedef struct {int Started,ObjectAdapter;void *GpuvaG3State;} ADMISSION_CONTEXT;
typedef struct {struct{int Magic;void *Adapter;}Object;void *GpuvaG3Process;} ADMISSION_DEVICE;
typedef struct {struct{int Magic;void *Device;}Object;void *GpuvaG3Process;} ADMISSION_RENDER_CONTEXT;
typedef struct {struct{ULONG Value;}Flags;void *pPrivateDriverData;ULONG PrivateDriverDataSize;void *hDevice,*hContext;} DXGKARG_ESCAPE;
static ADMISSION_G3_STATE state;static ADMISSION_G3_PROCESS process;
static int mode,maps,unmaps,cancels;
static ADMISSION_NATIVE_STAGING_REQUEST *published;
static void ExAcquireFastMutex(int *p){assert(!*p);*p=1;}
static void ExReleaseFastMutex(int *p){assert(*p);
 if(mode==0 && maps && !process.NativeMapRefs)
   assert(published->Token==7 && published->UserVa==0x100000 && "publish while process is pinned and closing is excluded");
 *p=0;}
static ADMISSION_G3_PROCESS *AdmissionGpuvaG3FindProcess(ADMISSION_G3_STATE *s,void *p){assert(s->Lock);return p==s->Process?s->Process:0;}
static NTSTATUS AdmissionNativeStagingMap(ADMISSION_CONTEXT *a,void *p,ULONG n,
    ULONGLONG *token,ULONGLONG *va) {
 (void)a;assert(p==&process && n==65536 && !state.Lock);
 assert(process.NativeMapRefs==1);++maps;
 if(mode==1){process.NativeClosing=TRUE;assert(process.NativeMapRefs && "teardown must retain the process");}
 if(mode==2)return STATUS_INSUFFICIENT_RESOURCES;
 *token=7;*va=0x100000;return STATUS_SUCCESS;
}
static NTSTATUS AdmissionNativeStagingCancel(ADMISSION_CONTEXT *a,void *p,ULONGLONG t){(void)a;assert(p==&process && t==7 && process.NativeMapRefs==1 && !state.Lock);++cancels;return STATUS_SUCCESS;}
void AdmissionNativeStagingUnmapProcess(ADMISSION_CONTEXT *a,void *p){(void)a;assert(p==&process && process.NativeMapRefs==1 && !state.Lock);++unmaps;}
@@BODY@@
int main(void) {
 ADMISSION_CONTEXT a={1,0,&state};ADMISSION_DEVICE d={{1,&a.ObjectAdapter},&process};
 ADMISSION_RENDER_CONTEXT c={{2,&d.Object},&process};
 DXGKARG_ESCAPE e={{0},0,sizeof(ADMISSION_NATIVE_STAGING_REQUEST),&d,&c};
 state.Process=&process;
 for(mode=0;mode<3;++mode){
  ADMISSION_NATIVE_STAGING_REQUEST q={ADMISSION_NATIVE_STAGING_MAGIC,1,sizeof(q),65536,0,0};
  e.pPrivateDriverData=&q;published=&q;memset(&process,0,sizeof(process));process.OsProcessId=55;
  maps=unmaps=0;NTSTATUS hr=AdmissionGpuvaG3NativeStagingEscape(&a,&e);
  assert(maps==1 && !process.NativeMapRefs && !state.Lock);
  if(mode==0)assert(hr==0 && q.Token==7 && q.UserVa==0x100000 && !unmaps);
  else assert(hr<0 && !q.UserVa && !q.Token && unmaps==(mode==1));
 }
 {ADMISSION_NATIVE_STAGING_REQUEST q={ADMISSION_NATIVE_STAGING_MAGIC,1,sizeof(q),65536,0,0};
  e.pPrivateDriverData=&q;process.NativeClosing=TRUE;maps=0;
  assert(AdmissionGpuvaG3NativeStagingEscape(&a,&e)<0 && !maps);
  q.Capacity=0;q.Token=7;
  assert(AdmissionGpuvaG3NativeStagingEscape(&a,&e)==0 && cancels==1 && !process.NativeMapRefs);
 }
 return 0;
}
'''.replace('@@BODY@@',body)
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp)/'escape.c';src.write_text(code);exe=Path(tmp)/'escape'
            result=subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined','-I'+str(ROOT/'drivers/apple-agx/render-admission/include'),
                '-I'+str(ROOT/'drivers/apple-agx/shared/include'),str(src),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
