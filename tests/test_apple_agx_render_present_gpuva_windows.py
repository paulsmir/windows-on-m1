import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers/apple-agx/render-admission"
SHARED = ROOT / "drivers/apple-agx/shared"


class GpuvaPresentWindowsTests(unittest.TestCase):
    def test_real_kmd_graph_pagewalk_copies_scattered_system_source(self):
        source = (RENDER / "src/gpuva_g3_windows.c").read_text()
        extracted = []
        for name in (
            "AdmissionG3PresentTranslate",
            "AdmissionG3PresentTranslateSource",
            "AdmissionG3PresentRead",
            "AdmissionG3PresentWrite",
            "AdmissionGpuvaG3ExecutePresentVirtual",
        ):
            match = re.search(
                rf"(?:static int|_Use_decl_annotations_ NTSTATUS) {name}\(.*?^}}", source, re.S | re.M
            )
            self.assertIsNotNone(match, name)
            extracted.append(match.group(0))
        copy_type = re.search(
            r"typedef struct _ADMISSION_G3_PRESENT_COPY \{.*?\} ADMISSION_G3_PRESENT_COPY;",
            source,
            re.S,
        )
        self.assertIsNotNone(copy_type)
        shim = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "render_present.h"
#define _Use_decl_annotations_
#define FALSE 0
#define TRUE 1
#define PASSIVE_LEVEL 0
#define POOL_FLAG_NON_PAGED 0
#define ADMISSION_POOL_TAG 0
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER -1
#define STATUS_INVALID_HANDLE -2
#define STATUS_INVALID_ADDRESS -3
#define STATUS_INSUFFICIENT_RESOURCES -4
#define STATUS_PARTIAL_COPY -5
#define MAXUINT 0xffffffffu
#define ADMISSION_MEMORY_LOCAL_SEGMENT 2u
#define MM_COPY_MEMORY_PHYSICAL 1
#define NT_SUCCESS(status) ((status)>=0)
#define RtlZeroMemory(pointer,bytes) memset(pointer,0,bytes)
#define RtlCopyMemory(destination,source,bytes) memcpy(destination,source,bytes)
#define KeMemoryBarrier() ((void)0)
typedef int NTSTATUS;
typedef unsigned UINT,ULONG;
typedef uint64_t ULONGLONG;
typedef long long LONGLONG;
typedef unsigned char *PUCHAR;
typedef void VOID,*PVOID;
typedef uintptr_t ULONG_PTR;
typedef size_t SIZE_T;
typedef struct {struct {LONGLONG QuadPart;} PhysicalAddress;} MM_COPY_ADDRESS;
typedef struct _ADMISSION_CONTEXT ADMISSION_CONTEXT;
typedef struct {int Lock;ADMISSION_CONTEXT *Adapter;} ADMISSION_G3_STATE;
typedef struct {int Uncertain,DestinationMapped;ULONGLONG RootIpa;} APPLE_AGX_GPUVA_G3_GRAPH;
typedef struct {ULONGLONG Va;} APPLE_AGX_GPUVA_G3_WALK_FAILURE;
struct _ADMISSION_CONTEXT {int Started;ULONGLONG PresentCopyFaultVa;ULONG PresentCopyFaultWrite;};
typedef struct _APPLE_AGX_GPUVA_G3_LOGICAL_PTE {
  ULONGLONG GuestIpa;UINT SegmentId,Flags;ULONGLONG Allocation,AllocationOffset;
} APPLE_AGX_GPUVA_G3_LOGICAL_PTE;
#define APPLE_AGX_GPUVA_G3_VALID 1u
#define APPLE_AGX_GPUVA_G3_WRITE 2u
typedef struct _ADMISSION_G3_PROCESS {
  ADMISSION_G3_STATE *State;
  APPLE_AGX_GPUVA_G3_GRAPH Graph;
  int Poisoned;
  APPLE_AGX_GPUVA_G3_LOGICAL_PTE ResidentPtes[8];
} ADMISSION_G3_PROCESS;
typedef struct {
  void *CpuAddress;
  ULONGLONG GuestIpaAddress,Bytes;
} ADMISSION_SCANOUT_MEMORY_VIEW;
typedef struct {void *GpuvaG3Process;int GpuvaG3Poisoned;ULONGLONG GpuvaG3RootIpa;} ADMISSION_RENDER_CONTEXT;
static ADMISSION_CONTEXT adapter;
static unsigned char source_memory[32768],destination_memory[32768];
static const ULONGLONG source_ipas[8]={0x900000ULL,0x901000ULL,0x903000ULL,
  0x904000ULL,0x910000ULL,0x911000ULL,0x913000ULL,0x914000ULL};
static int reject_destination;
static __attribute__((unused)) const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *AdmissionG3CopyPte(
    const ADMISSION_G3_PROCESS *process,ULONGLONG va){
  if(va<0x100000ULL || va>=0x108000ULL)return NULL;
  return &process->ResidentPtes[(va-0x100000ULL)/0x1000ULL];}
static int KeGetCurrentIrql(void){return 0;}
static void *ExAllocatePool2(unsigned flags,size_t bytes,unsigned tag){(void)flags;(void)tag;return malloc(bytes);}
static void ExFreePoolWithTag(void *pointer,unsigned tag){(void)tag;free(pointer);}
static void ExAcquireFastMutex(int *lock){assert(!*lock);*lock=1;}
static void ExReleaseFastMutex(int *lock){assert(*lock);*lock=0;}
static NTSTATUS AdmissionMemoryRuntimeLocalView(ADMISSION_CONTEXT *context,
    ADMISSION_SCANOUT_MEMORY_VIEW *view){assert(context==&adapter);
  view->CpuAddress=destination_memory;view->GuestIpaAddress=0xa00000ULL;
  view->Bytes=sizeof(destination_memory);return 0;}
static int AppleAgxGpuvaG3GraphTranslateVa(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    ULONGLONG va,ULONGLONG *ipa){(void)graph;
  if(va>=0x200000ULL && va<0x208000ULL){
    *ipa=0xa00000ULL+(va-0x200000ULL);return 1;}
  return 0;}
static int AppleAgxGpuvaG3GraphContainsRangeAccess(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    ULONGLONG va,UINT bytes,int write){ULONGLONG ipa,offset;
  if(write && (reject_destination || !graph->DestinationMapped))return 0;
  for(offset=0;offset<bytes;offset+=0x4000ULL)
    if(!AppleAgxGpuvaG3GraphTranslateVa(graph,va+offset,&ipa))return 0;
  return 1;}
static int AppleAgxGpuvaG3GraphInspectRangeAccess(APPLE_AGX_GPUVA_G3_GRAPH *graph,
    ULONGLONG va,UINT bytes,int write,APPLE_AGX_GPUVA_G3_WALK_FAILURE *failure){
  int valid=AppleAgxGpuvaG3GraphContainsRangeAccess(graph,va,bytes,write);
  failure->Va=valid?0:va;return valid;}
static NTSTATUS MmCopyMemory(void *bytes,MM_COPY_ADDRESS address,SIZE_T count,
    unsigned flags,SIZE_T *copied){ULONGLONG ipa=(ULONGLONG)address.PhysicalAddress.QuadPart;
  unsigned index;assert(flags==MM_COPY_MEMORY_PHYSICAL);
  for(index=0;index<8;++index)if(ipa>=source_ipas[index] &&
      ipa-source_ipas[index]<=0x1000ULL && count<=0x1000ULL-(ipa-source_ipas[index])){
    memcpy(bytes,source_memory+index*0x1000u+(unsigned)(ipa-source_ipas[index]),count);
    *copied=count;return 0;}
  return STATUS_INVALID_ADDRESS;}
'''
        cases = r'''
int main(void){
  ADMISSION_G3_STATE state={0};ADMISSION_G3_PROCESS process={0};
  ADMISSION_RENDER_CONTEXT context={0};ADMISSION_PRESENT_BLT_INPUT input={0};
  APPLE_AGX_GDI_RECT rect={0,0,8192,1};unsigned char command[4096];
  unsigned used,next;ULONGLONG copied;
  state.Lock=0;state.Adapter=&adapter;process.State=&state;
  process.Graph.DestinationMapped=1;process.Graph.RootIpa=0x4000ULL;
  context.GpuvaG3Process=&process;context.GpuvaG3RootIpa=0x4000ULL;
  for(unsigned page=0;page<8;++page){
    process.ResidentPtes[page].GuestIpa=source_ipas[page];
    process.ResidentPtes[page].SegmentId=0;
    process.ResidentPtes[page].Flags=APPLE_AGX_GPUVA_G3_VALID|APPLE_AGX_GPUVA_G3_WRITE;
    process.ResidentPtes[page].Allocation=0x1234ULL;
    process.ResidentPtes[page].AllocationOffset=page*0x1000ULL;
  }
  assert(AdmissionAllocationDescribe(8192,1,4,1,21,1,&input.Command.SourceDescription));
  assert(AdmissionAllocationDescribe(8192,1,4,1,21,0,&input.Command.DestinationDescription));
  input.Command.Version=ADMISSION_PRESENT_BLT_GPUVA_VERSION;
  input.Command.SourceLocation=0x100000ULL;input.Command.DestinationLocation=0x200000ULL;
  input.Command.ContextToken=(ULONGLONG)(ULONG_PTR)&context;
  input.Command.SourceRect=input.Command.DestinationRect=rect;
  input.Rects=&rect;input.RectCount=1;
  memset(source_memory,0x79,sizeof(source_memory));
  assert(AdmissionPresentBltEncode(&input,command,sizeof(command),&used,&next));
  assert(AdmissionGpuvaG3ExecutePresentVirtual(&adapter,&context,command,used,&copied)==0);
  assert(copied==sizeof(source_memory));
  assert(memcmp(source_memory,destination_memory,sizeof(source_memory))==0);
  memset(destination_memory,0,sizeof(destination_memory));
  process.ResidentPtes[2].Flags=0;
  assert(AdmissionGpuvaG3ExecutePresentVirtual(&adapter,&context,command,used,&copied)!=0);
  assert(copied==0 && destination_memory[0]==0 && destination_memory[32767]==0);
  assert(adapter.PresentCopyFaultVa==0x102000ULL && adapter.PresentCopyFaultWrite==0);
  process.ResidentPtes[2].Flags=APPLE_AGX_GPUVA_G3_VALID|APPLE_AGX_GPUVA_G3_WRITE;
  memset(destination_memory,0,sizeof(destination_memory));reject_destination=1;
  assert(AdmissionGpuvaG3ExecutePresentVirtual(&adapter,&context,command,used,&copied)!=0);
  assert(copied==0 && destination_memory[0]==0 && destination_memory[32767]==0);
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            program = Path(directory) / "gpuva_present.c"
            program.write_text(shim + copy_type.group(0) + "\n" + "\n".join(extracted) + cases)
            binary = Path(directory) / "gpuva_present"
            subprocess.run(
                [os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=address,undefined", "-I", str(RENDER / "include"),
                 "-I", str(SHARED / "include"), str(program),
                 str(RENDER / "src/render_present.c"),
                 str(RENDER / "src/render_allocation.c"), "-o", str(binary)],
                check=True,
            )
            subprocess.run([str(binary)], check=True)
