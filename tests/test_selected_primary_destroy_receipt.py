from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]

class SelectedPrimaryDestroyReceiptTests(unittest.TestCase):
    def test_real_wrapper_keeps_selected_history_after_unrelated_destroys(self):
        source = (ROOT / "drivers/apple-agx/render-admission/src/allocation_windows.c").read_text()
        start = source.index("_Use_decl_annotations_ NTSTATUS AdmissionDdiDestroyAllocation(")
        end = source.index("\nstatic NTSTATUS AdmissionDescribeAllocationImpl", start)
        wrapper = source[start:end]
        shim = r'''#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "render_qualification.h"
#define _Use_decl_annotations_
#define APPLE_AGX_GPUVA_G3_QUALIFICATION 1
#define NT_SUCCESS(x) ((x)>=0)
#define TRUE 1
#define FALSE 0
#define RtlZeroMemory(p,n) memset(p,0,n)
#define KeMemoryBarrier() __sync_synchronize()
typedef int LONG,NTSTATUS,BOOLEAN;typedef unsigned int UINT,ULONG;typedef uint64_t ULONGLONG;typedef uintptr_t ULONG_PTR;typedef void *HANDLE;
typedef struct {volatile LONG SourceAddressReceiptState;struct {ULONGLONG Allocation;} SourceAddressReceipt;volatile LONG SelectedPrimaryDestroyEnter,SelectedPrimaryDestroySuccess,SelectedPrimaryDestroyFailure;} ADMISSION_CONTEXT;
typedef struct {HANDLE hResource;UINT NumAllocations;HANDLE *pAllocationList;union{UINT Value;}Flags;} DXGKARG_DESTROYALLOCATION;
static LONG InterlockedCompareExchange(volatile LONG*p,LONG v,LONG old){return __sync_val_compare_and_swap(p,old,v);}
static LONG InterlockedIncrement(volatile LONG*p){return __sync_add_and_fetch(p,1);}
static void *PsGetCurrentProcessId(void){return (void*)4;}static void *PsGetCurrentThreadId(void){return (void*)8;}
static NTSTATUS result;static ADMISSION_DWM_DDI_EVENT last;
static NTSTATUS AdmissionDestroyAllocationImpl(HANDLE a,const DXGKARG_DESTROYALLOCATION*b){(void)a;(void)b;return result;}
static void AdmissionDwmDdiProbeRecordWindows(ADMISSION_CONTEXT*a,const ADMISSION_DWM_DDI_EVENT*b){(void)a;last=*b;}
'''
        cases = r'''int main(void){ADMISSION_CONTEXT c={0};HANDLE a[2]={(void*)1,(void*)2};DXGKARG_DESTROYALLOCATION d={0};d.NumAllocations=2;d.pAllocationList=a;c.SourceAddressReceipt.Allocation=2;
assert(AdmissionDdiDestroyAllocation(&c,&d)==0);assert(last.SourceCount==0);
c.SourceAddressReceiptState=2;assert(AdmissionDdiDestroyAllocation(&c,&d)==0);assert(last.SourceCount==1&&last.DestinationCount==1&&last.Fence==0);
a[1]=(void*)3;assert(AdmissionDdiDestroyAllocation(&c,&d)==0);assert(last.SourceCount==1&&last.DestinationCount==1);
a[1]=(void*)2;result=-1;assert(AdmissionDdiDestroyAllocation(&c,&d)==-1);assert(last.SourceCount==2&&last.DestinationCount==1&&last.Fence==1);return 0;}
'''
        with tempfile.TemporaryDirectory() as tmp:
            source_path=Path(tmp)/"replay.c";binary=Path(tmp)/"replay"
            source_path.write_text(shim+wrapper+cases)
            subprocess.run(["clang","-std=c11","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-I",str(ROOT/"drivers/apple-agx/render-admission/include"),str(source_path),"-o",str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
