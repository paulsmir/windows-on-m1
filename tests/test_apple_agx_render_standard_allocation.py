"""Execute the production standard-allocation callback, not a second translator."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'


class StandardAllocationTests(unittest.TestCase):
    def test_single_allocation_resource_uses_existing_object_owner(self):
        source = (RENDER / 'src/allocation_windows.c').read_text()
        callback = source[source.index('_Use_decl_annotations_ NTSTATUS AdmissionDdiCreateAllocation'):
                          source.index('_Use_decl_annotations_ NTSTATUS AdmissionDdiDestroyAllocation')]
        shim = r'''
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "render_allocation.h"
#define _Use_decl_annotations_
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER ((int32_t)0xc000000d)
#define STATUS_NOT_SUPPORTED ((int32_t)0xc00000bb)
#define STATUS_INSUFFICIENT_RESOURCES ((int32_t)0xc000009a)
#define MAXSIZE_T SIZE_MAX
#define POOL_FLAG_NON_PAGED 0
#define ADMISSION_POOL_TAG 0
#define ADMISSION_MEMORY_LOCAL_SEGMENT 2
#define ADMISSION_LOCAL_SEGMENT_SET 2
#define D3DDDI_ALLOCATIONPRIORITY_NORMAL 0x78000000u
#define RtlZeroMemory(p,n) memset((p),0,(n))
#define ExAllocatePool2(flags,n,tag) malloc(n)
#define ExFreePoolWithTag(p,tag) free(p)
typedef int32_t NTSTATUS;
typedef void *HANDLE;
typedef unsigned int UINT;
typedef unsigned long long ULONGLONG;
typedef size_t SIZE_T;
typedef struct {int Memory;} ADMISSION_CONTEXT;
static int AdmissionMemoryReady(const int *m) {return *m;}
typedef struct {ADMISSION_ALLOCATION_OBJECT Object;} ADMISSION_ALLOCATION_HANDLE;
typedef struct {const void *pPrivateDriverData;UINT PrivateDriverDataSize;
 UINT Alignment;SIZE_T Size,PitchAlignedSize;struct {UINT Value;} HintedBank;
 struct {UINT Value,SegmentId0;} PreferredSegment;
 UINT SupportedReadSegmentSet,SupportedWriteSegmentSet,EvictionSegmentSet;
 HANDLE hAllocation;union {UINT Value;struct {UINT Low:15;
 UINT AccessedPhysically:1;UINT High:16;};} FlagsWddm2;
 void *pAllocationUsageHint;UINT AllocationPriority;struct {UINT Value;} Flags2;
 UINT PhysicalAdapterIndex;} DXGK_ALLOCATIONINFO;
typedef struct {const void *pPrivateDriverData;UINT PrivateDriverDataSize;
 UINT NumAllocations;DXGK_ALLOCATIONINFO *pAllocationInfo;HANDLE hResource;
 struct {UINT Resource;} Flags;} DXGKARG_CREATEALLOCATION;
'''
        cases = r'''
int main(void) {
 ADMISSION_CONTEXT ctx={1};
 ADMISSION_ALLOCATION_DESCRIPTION d;
 assert(AdmissionAllocationDescribe(2560,1600,4,1,21,0,&d));
 DXGK_ALLOCATIONINFO info={0};
 info.pPrivateDriverData=&d;info.PrivateDriverDataSize=48;
 DXGKARG_CREATEALLOCATION a={0};
 a.NumAllocations=1;a.pAllocationInfo=&info;a.Flags.Resource=1;
 assert(AdmissionDdiCreateAllocation(&ctx,&a)==0);
 assert(a.hResource==NULL && info.hAllocation!=NULL);
 ADMISSION_ALLOCATION_HANDLE *h=info.hAllocation;
 assert(AdmissionAllocationDescriptionValid(&h->Object.Description));
 assert(info.Size==16384000 && info.Alignment==65536);
 assert(info.PreferredSegment.SegmentId0==2 && info.SupportedWriteSegmentSet==2);
 assert(info.FlagsWddm2.Value==0x8000);
 assert(AdmissionAllocationOpen(&h->Object));
 assert(!AdmissionAllocationDestroy(&h->Object));
 assert(AdmissionAllocationClose(&h->Object));
 assert(AdmissionAllocationDestroy(&h->Object));free(h);info.hAllocation=NULL;
 a.hResource=&ctx;
 assert(AdmissionDdiCreateAllocation(&ctx,&a)==STATUS_INVALID_PARAMETER);
 assert(info.hAllocation==NULL);a.hResource=NULL;a.NumAllocations=2;
 assert(AdmissionDdiCreateAllocation(&ctx,&a)==STATUS_INVALID_PARAMETER);
 assert(info.hAllocation==NULL);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            program = Path(tmp) / 'owner.c'
            program.write_text(shim + callback + cases)
            binary = Path(tmp) / 'owner'
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall',
                            '-Wextra', '-Werror', '-fsanitize=address,undefined',
                            '-I', str(RENDER / 'include'), str(program),
                            str(RENDER / 'src/render_allocation.c'), '-o', str(binary)],
                           check=True)
            subprocess.run([str(binary)], check=True)

    def test_size_phase_and_native_primary_translation(self):
        source = (RENDER / 'src/allocation_windows.c').read_text()
        callback = source[source.index('static BOOLEAN AdmissionFormatBytesPerPixel'):
                          source.index('_Use_decl_annotations_ NTSTATUS AdmissionDdiCreateAllocation')]
        shim = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "render_allocation.h"
#define _Use_decl_annotations_
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER ((int32_t)0xc000000d)
#define STATUS_BUFFER_TOO_SMALL ((int32_t)0xc0000023)
#define RtlCopyMemory memcpy
#define APPLE_AGX_SCANOUT_J313_WIDTH 2560u
#define APPLE_AGX_SCANOUT_J313_HEIGHT 1600u
typedef void *HANDLE;
typedef int32_t NTSTATUS;
typedef unsigned int UINT, ULONG, *PULONG;
typedef unsigned char BOOLEAN, *PBOOLEAN;
typedef unsigned int D3DDDIFORMAT, D3DKMDT_GDISURFACETYPE;
enum {D3DDDIFMT_A8R8G8B8=21,D3DDDIFMT_X8R8G8B8=22,D3DDDIFMT_A8=28,
 D3DDDIFMT_A8B8G8R8=32,D3DDDIFMT_X8B8G8R8=33};
enum {D3DKMDT_GDISURFACE_TEXTURE=1,D3DKMDT_GDISURFACE_STAGING=2,
 D3DKMDT_GDISURFACE_STAGING_CPUVISIBLE=3,D3DKMDT_GDISURFACE_LOOKUPTABLE=4,
 D3DKMDT_GDISURFACE_EXISTINGSYSMEM=5};
enum {D3DKMDT_STANDARDALLOCATION_SHAREDPRIMARYSURFACE=1,
 D3DKMDT_STANDARDALLOCATION_GDISURFACE=4};
typedef struct {UINT Width,Height;D3DDDIFORMAT Format;
 struct {UINT Numerator,Denominator;} RefreshRate;UINT VidPnSourceId;
} D3DKMDT_SHAREDPRIMARYSURFACEDATA;
typedef struct {UINT Width,Height;D3DDDIFORMAT Format;
 D3DKMDT_GDISURFACETYPE Type;struct {UINT Value;} Flags;UINT Pitch;
} D3DKMDT_GDISURFACEDATA;
typedef struct {UINT StandardAllocationType;union {
 D3DKMDT_SHAREDPRIMARYSURFACEDATA *pCreateSharedPrimarySurfaceData;
 D3DKMDT_GDISURFACEDATA *pCreateGdiSurfaceData;};
 void *pAllocationPrivateDriverData;UINT AllocationPrivateDriverDataSize;
 void *pResourcePrivateDriverData;UINT ResourcePrivateDriverDataSize;
 UINT PhysicalAdapterIndex;
} DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA;
'''
        cases = r'''
int main(void) {
 int adapter=1;
 D3DKMDT_SHAREDPRIMARYSURFACEDATA primary={2560,1600,21,{60,1},0};
 D3DKMDT_SHAREDPRIMARYSURFACEDATA before=primary;
 ADMISSION_ALLOCATION_DESCRIPTION desc, sentinel;
 DXGKARG_GETSTANDARDALLOCATIONDRIVERDATA a={0};
 a.StandardAllocationType=1;a.pCreateSharedPrimarySurfaceData=&primary;
 assert(AdmissionDdiGetStandardAllocationDriverData(&adapter,&a)==0);
 assert(a.AllocationPrivateDriverDataSize==48 && a.ResourcePrivateDriverDataSize==0);
 assert(memcmp(&primary,&before,sizeof(primary))==0);
 a.pAllocationPrivateDriverData=&desc;
 assert(AdmissionDdiGetStandardAllocationDriverData(&adapter,&a)==0);
 assert(desc.Width==2560 && desc.Height==1600 && desc.Pitch==10240);
 assert(desc.Size==16384000ULL && desc.BytesPerPixel==4 && desc.Format==21);
 assert(desc.Type==1 && desc.CpuVisible==0);
 assert(AdmissionAllocationDescriptionValid(&desc));
 assert(memcmp(&primary,&before,sizeof(primary))==0);
 sentinel=desc;primary.VidPnSourceId=1;
 assert(AdmissionDdiGetStandardAllocationDriverData(&adapter,&a)==STATUS_INVALID_PARAMETER);
 assert(memcmp(&desc,&sentinel,sizeof(desc))==0);primary=before;
 primary.Width=1280;
 assert(AdmissionDdiGetStandardAllocationDriverData(&adapter,&a)==STATUS_INVALID_PARAMETER);
 primary=before;primary.Format=28;
 assert(AdmissionDdiGetStandardAllocationDriverData(&adapter,&a)==STATUS_INVALID_PARAMETER);
 primary=before;a.AllocationPrivateDriverDataSize=47;
 assert(AdmissionDdiGetStandardAllocationDriverData(&adapter,&a)==STATUS_BUFFER_TOO_SMALL);
 assert(memcmp(&desc,&sentinel,sizeof(desc))==0);
 a.pCreateSharedPrimarySurfaceData=NULL;
 assert(AdmissionDdiGetStandardAllocationDriverData(&adapter,&a)==STATUS_INVALID_PARAMETER);
 a.pAllocationPrivateDriverData=NULL;
 assert(AdmissionDdiGetStandardAllocationDriverData(&adapter,&a)==0);
 a.StandardAllocationType=99;
 assert(AdmissionDdiGetStandardAllocationDriverData(&adapter,&a)==STATUS_INVALID_PARAMETER);
 a.StandardAllocationType=1;a.PhysicalAdapterIndex=1;
 assert(AdmissionDdiGetStandardAllocationDriverData(&adapter,&a)==STATUS_INVALID_PARAMETER);
 a.PhysicalAdapterIndex=0;
 assert(AdmissionDdiGetStandardAllocationDriverData(NULL,&a)==STATUS_INVALID_PARAMETER);
 D3DKMDT_GDISURFACEDATA gdi={17,2,21,1,{0},0x12345678};
 D3DKMDT_GDISURFACEDATA gdiBefore=gdi;
 a.StandardAllocationType=4;a.pCreateGdiSurfaceData=&gdi;
 assert(AdmissionDdiGetStandardAllocationDriverData(&adapter,&a)==0);
 assert(memcmp(&gdi,&gdiBefore,sizeof(gdi))==0);
 a.pAllocationPrivateDriverData=&desc;
 assert(AdmissionDdiGetStandardAllocationDriverData(&adapter,&a)==0);
 assert(gdi.Pitch==80 && desc.Pitch==80 && desc.Size==160);
 gdi.Flags.Value=1;sentinel=desc;
 assert(AdmissionDdiGetStandardAllocationDriverData(&adapter,&a)==STATUS_INVALID_PARAMETER);
 assert(memcmp(&desc,&sentinel,sizeof(desc))==0);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            program = Path(tmp) / 'standard.c'
            program.write_text(shim + callback + cases)
            binary = Path(tmp) / 'standard'
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall',
                            '-Wextra', '-Werror', '-fsanitize=address,undefined',
                            '-I', str(RENDER / 'include'), str(program),
                            str(RENDER / 'src/render_allocation.c'), '-o', str(binary)],
                           check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
