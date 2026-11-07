"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

EXP1979's ACQUIRE receipt said Poisoned=1 but discarded the already-recorded
first poison site, leaving a downstream E_INVALIDARG mistaken for a bad scene
request. Execute the actual capture function: version 2 must retain that cause
without changing the wire size or overwriting the first failure.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_umd_staging_write_watch import function

ROOT = Path(__file__).resolve().parents[1]
D = ROOT / 'drivers/apple-agx'

class PrivateFailureCause(unittest.TestCase):
    def test_real_capture_retains_first_poison_site(self):
        capture = function((D/'render-admission/src/gpuva_g3_windows.c').read_text(),
                           'AdmissionG3CapturePrivateFailure')
        code = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "apple_agx_g3_private_failure.h"
#include "apple_agx_g3_private_abi.h"
typedef unsigned UINT;
typedef unsigned long long ULONGLONG;
typedef uintptr_t ULONG_PTR;
typedef void *HANDLE;
typedef int NTSTATUS, BOOLEAN;
#define TRUE 1
#define FALSE 0
#define NT_SUCCESS(s) ((s)>=0)
#define RtlZeroMemory(p,n) memset(p,0,n)
#define RtlCopyMemory memcpy
#define APPLE_AGX_G3_PRIVATE_UNITS 1024u
#define APPLE_AGX_G3_PROCESS_UNITS 246u
#define HandleToULong(p) ((UINT)(uintptr_t)(p))
#define KeMemoryBarrier() ((void)0)
static HANDLE PsGetCurrentProcessId(void){return (HANDLE)9780;}
static int InterlockedCompareExchange(int *p,int n,int old){int r=*p;if(r==old)*p=n;return r;}
static void InterlockedExchange(int *p,int n){*p=n;}
typedef struct {UINT GlobalUnits,OwnerUnits,LargestFreeUnits;} APPLE_AGX_G3_PRIVATE_POOL_STATS;
typedef struct {UINT Predicate,FailedRange;APPLE_AGX_G3_PRIVATE_POOL_STATS Stats;} APPLE_AGX_G3_PRIVATE_PREPARE_DIAGNOSTIC;
typedef struct S {struct S *Next;HANDLE Context;UINT Queued,ReleaseRequested,Quarantined;} ADMISSION_G3_PRIVATE_SCENE;
typedef struct {struct {ULONGLONG ProcessId,RootIpa,LeaseToken;UINT Uncertain,JobInFlight;} Graph;
 ULONGLONG PrivateVa;struct {ULONGLONG Generation;} PrivateManager;
 UINT Poisoned,PoisonSite;ADMISSION_G3_PRIVATE_SCENE *PrivateScenes;} ADMISSION_G3_PROCESS;
typedef struct {int PrivatePool;} ADMISSION_G3_STATE;
typedef struct {int G3PrivateFailureClaim;APPLE_AGX_G3_PRIVATE_FAILURE G3PrivateFailure;} ADMISSION_CONTEXT;
typedef struct {HANDLE hKmdProcessHandle,hContext,hDevice;} DXGKARG_ESCAPE;
static void AppleAgxG3PrivatePoolStats(int *pool,ULONGLONG owner,APPLE_AGX_G3_PRIVATE_POOL_STATS *s){
 (void)pool;(void)owner;memset(s,0,sizeof(*s));}
@@CAPTURE@@
int main(void) {
 ADMISSION_CONTEXT a={0};ADMISSION_G3_STATE s={0};ADMISSION_G3_PROCESS p;memset(&p,0,sizeof(p));
 DXGKARG_ESCAPE e={0};APPLE_AGX_G3_PRIVATE_REQUEST q={0};UINT required[9]={0};
 q.Operation=APPLE_AGX_G3_PRIVATE_ACQUIRE;q.Width=512;q.Height=256;
 q.UtileWidth=q.UtileHeight=32;q.Layers=q.Samples=1;
 p.Poisoned=1;p.PoisonSite=0x0026059fu;p.Graph.ProcessId=19;
 assert(sizeof(a.G3PrivateFailure)==232);
 assert(AdmissionG3CapturePrivateFailure(&a,&s,&p,&e,&q,required,1,-1,0,0,~0u,0,0));
 assert(a.G3PrivateFailure.Reserved==0x0026059fu);
 assert(a.G3PrivateFailure.Version==2);
 assert(a.G3PrivateFailure.Poisoned==1 && !a.G3PrivateFailure.GraphUncertain);
 assert(a.G3PrivateFailure.Width==512 && a.G3PrivateFailure.UtileWidth==32);
 p.PoisonSite=99;
 assert(!AdmissionG3CapturePrivateFailure(&a,&s,&p,&e,&q,required,1,-1,0,0,~0u,0,0));
 assert(a.G3PrivateFailure.Reserved==0x0026059fu);
 a.G3PrivateFailureClaim=0;p.Poisoned=0;p.PoisonSite=0;
 assert(AdmissionG3CapturePrivateFailure(&a,&s,&p,&e,&q,required,5,-1,0,0,~0u,0,0));
 assert(!a.G3PrivateFailure.Reserved);
 return 0;
}
'''.replace('@@CAPTURE@@',capture)
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp)/'capture.c';src.write_text(code)
            for lang,std in [('c','c11'),('c++','c++17')]:
                exe=Path(tmp)/('capture-'+std)
                built=subprocess.run(['clang','-x',lang,'-std='+std,'-Wall','-Wextra','-Werror',
                    '-Wno-missing-field-initializers','-fsanitize=address,undefined',
                    '-I',str(D/'shared/include'),str(src),'-o',str(exe)],capture_output=True,text=True)
                self.assertEqual(built.returncode,0,built.stdout+built.stderr)
                run=subprocess.run([str(exe)],capture_output=True,text=True)
                self.assertEqual(run.returncode,0,run.stdout+run.stderr)
