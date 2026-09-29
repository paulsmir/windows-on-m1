#include "apple_agx_render_template.h"
#include "j313_agx_abi_admission.generated.h"
/* PRODUCTION_ALIAS_TYPES */
#define STATUS_INVALID_IMAGE_FORMAT ((NTSTATUS)0xc000007b)
#define ADMISSION_MEMORY_RUNTIME_TAG 0
#define KeMemoryBarrier() ((void)0)
#define ExInitializeFastMutex(p) (*(p)=0)
#define ADMISSION_UAT_PAGE_CAPACITY 64
#define ADMISSION_UAT_MAPPING_CAPACITY 8
#define ADMISSION_APERTURE_GPU_VA 0x1600000000ULL
#define ADMISSION_APERTURE_BYTES 0x10000000ULL
#define ADMISSION_APERTURE_PAGE_COUNT (ADMISSION_APERTURE_BYTES/PAGE_SIZE)
typedef int LONG;
/* PRODUCTION_STAGE_ENUM */
/* PRODUCTION_ALIAS_DATA */
static int lifecycle_fault, table_count, table_calls, publication_active;
static void AdmissionMemoryRecordStart(ADMISSION_CONTEXT *c, ADMISSION_MEMORY_START_STAGE stage, NTSTATUS status) {
  c->MemoryStartStage=stage; c->MemoryStartStatus=status;
}
static void AdmissionRecordMemoryStartFailure(ADMISSION_CONTEXT *c, ULONG s, NTSTATUS st, ULONGLONG n, ULONG r, LONG o, ULONG p, NTSTATUS ps, ULONGLONG b) {
 (void)c;(void)s;(void)st;(void)n;(void)r;(void)o;(void)p;(void)ps;(void)b;
}
static void AdmissionRecordMemoryStop(ADMISSION_CONTEXT *c, NTSTATUS s, LONG b, LONG a) {(void)c;(void)s;(void)b;assert(a==0);}
static NTSTATUS AdmissionRecordLocalReserve(ADMISSION_CONTEXT *c, ADMISSION_PHYSICAL_ALLOCATION *a) {
 (void)c;
 for(SIZE_T i=0;i<a->Size;++i) assert(a->CpuBase[i]==0);
 return lifecycle_fault==1 ? STATUS_INSUFFICIENT_RESOURCES : STATUS_SUCCESS;
}
static NTSTATUS AdmissionPhysicalOwnerInitialize(DXGKRNL_INTERFACE *i, void *d, ADMISSION_PHYSICAL_OWNER *o) {
 (void)d; memset(o,0,sizeof(*o));o->Initialized=TRUE;o->Interface=i;return STATUS_SUCCESS;
}
static NTSTATUS AdmissionPhysicalOwnerDestroy(ADMISSION_PHYSICAL_OWNER *o) {
 assert(o->AllocationCount==0);o->Initialized=FALSE;return STATUS_SUCCESS;
}
static NTSTATUS AdmissionPhysicalAllocate(ADMISSION_PHYSICAL_OWNER *o, SIZE_T bytes, ADMISSION_PHYSICAL_ALLOCATION **out) {
 ++table_calls;
 if((lifecycle_fault==2 && table_calls==5) || (lifecycle_fault==5 && table_calls==36)) return STATUS_INSUFFICIENT_RESOURCES;
 ADMISSION_PHYSICAL_ALLOCATION *a=calloc(1,sizeof(*a));assert(a);
 a->Interface=o->Interface; a->MappedBase=aligned_alloc(0x4000,bytes); assert(a->MappedBase);
 a->MappedSize=bytes;a->CpuBase=a->MappedBase;a->Size=bytes;
 a->Adl=calloc(1,sizeof(*a->Adl));a->Adl->Flags.Contiguous=1;
 a->PhysicalMemoryObject=a; a->AdapterMemoryObject=a;
 a->HostPhysicalBase=0x700000000ULL+(ULONGLONG)table_calls*0x10000;
 ++o->AllocationCount;++table_count;*out=a;return STATUS_SUCCESS;
}
static unsigned char AdmissionUatMap(void *opaque,unsigned long long pa,unsigned int n,volatile unsigned char **out) {
 (void)opaque; assert(pa==J313_AGX_G2_GPU_BASE && n==0x4000);
 if(lifecycle_fault==3)return 0;
 assert(local_mapping && local_mapping[0]==0 && local_mapping[0x3fffffff]==0);
 *out=publication_registers;publication_active=1;return 1;
}
static void AdmissionUatBarrier(void *opaque) {(void)opaque;}
static unsigned char AdmissionUatUnmap(void *opaque,volatile unsigned char *va) {
 (void)opaque;assert(va==publication_registers); publication_active=0;return 1;
}
static APPLE_AGX_BOOL lifecycle_mark_ready(ADMISSION_MEMORY_CONTRACT *m,APPLE_AGX_U32 c,APPLE_AGX_U64 leaf,APPLE_AGX_U64 va,APPLE_AGX_U64 n) {
 if(lifecycle_fault==4)return APPLE_AGX_FALSE;
 return AdmissionMemoryMarkUatReady(m,c,leaf,va,n);
}
/* PRODUCTION_MEMORY_LIFECYCLE */
static void replay_memory_lifecycle(ADMISSION_CONTEXT *context) {
 context->DeviceInformation.TranslatedResourceList->List[0].PartialResourceList.Count=11;
 context->InterfaceValid=TRUE; context->Interface.DxgkCbMapMemory=(void *)1;
 context->Interface.DxgkCbUnmapMemory=(void *)1;
 context->Interface.DxgkCbUnmapPhysicalMemory=dxgk_unmap;
 context->Interface.DxgkCbFreeAdl=dxgk_free_adl;
 context->Interface.DxgkCbDestroyPhysicalMemoryObject=dxgk_destroy;
 context->MemoryRuntime=NULL;context->PhysicalDeviceObject=context;
 for(lifecycle_fault=0;lifecycle_fault<=5;++lifecycle_fault) {
  unsigned prior_frees=dxgk_unmaps;
  table_calls=0;table_count=0;local_unmaps=0;
  fill_borrowed=true;
  NTSTATUS status=AdmissionMemoryRuntimeStart(context);
  if(lifecycle_fault==0) {
   assert(status==STATUS_SUCCESS);
   ADMISSION_MEMORY_RUNTIME *r=context->MemoryRuntime;
   assert(r->Residency.Inventory.PageCount==36);
   assert(context->Memory.LocalAllocationBytes==0x3d800000);
   assert(AdmissionMemoryRuntimeStop(context)==STATUS_SUCCESS);
  } else {
   if(NT_SUCCESS(status) || status==STATUS_DEVICE_BUSY)
    fprintf(stderr,"lifecycle fault=%d status=%x stage=%d\n",lifecycle_fault,(unsigned)status,context->MemoryStartStage);
   assert(!NT_SUCCESS(status) && status!=STATUS_DEVICE_BUSY);
  }
  assert(!context->MemoryRuntime && !local_mapping && local_unmaps==1);
  assert(dxgk_unmaps-prior_frees==(unsigned)table_count);
  assert(!publication_active);
 }
 fill_borrowed=false;
 puts("R143 production memory Start/Stop and rollback: PASS");
}
