#include "umd_asahi_owner.h"
static ADMISSION_UMD_DEVICE PoolDevice;
static ADMISSION_UMD_ADAPTER PoolAdapter;
static D3DDDI_DEVICECALLBACKS PoolCallbacks;
static void *PoolMemory[ADMISSION_UMD_SCREEN_BUFFER_LIMIT];
static D3DKMT_HANDLE PoolHandles[ADMISSION_UMD_SCREEN_BUFFER_LIMIT];
static unsigned PoolNextHandle;
static unsigned PoolCreates,PoolMaps,PoolUnlocks,PoolDeletes,PoolErrors;
static unsigned PoolPresentationDeletes;
static unsigned PoolLastPresentationFormat;
static int PoolFailAllocation;
static int PoolFailMap;
static unsigned PoolFailUnlock;
static unsigned PoolFailDeallocation;
static HRESULT APIENTRY PoolAllocate(HANDLE h,D3DDDICB_ALLOCATE *a) {
  (void)h;
  if(PoolFailAllocation) return E_OUTOFMEMORY;
  if(a->NumAllocations==1 && a->pAllocationInfo &&
     a->pAllocationInfo->PrivateDriverDataSize==sizeof(ADMISSION_ALLOCATION_DESCRIPTION)) {
    const ADMISSION_ALLOCATION_DESCRIPTION *present=
        (const ADMISSION_ALLOCATION_DESCRIPTION *)a->pAllocationInfo->pPrivateDriverData;
    if(!AdmissionAllocationDescriptionValid(present) ||
       present->Width!=2560u || present->Height!=1600u ||
       present->Pitch!=10240u || present->Size!=0xfa0000ULL)
      return E_INVALIDARG;
    unsigned slot;
    for(slot=0;slot<ADMISSION_UMD_SCREEN_BUFFER_LIMIT && PoolMemory[slot];++slot) {}
    if(slot==ADMISSION_UMD_SCREEN_BUFFER_LIMIT) return E_OUTOFMEMORY;
    PoolMemory[slot]=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,
                              (SIZE_T)present->Size);
    if(!PoolMemory[slot]) return E_OUTOFMEMORY;
    PoolHandles[slot]=a->hResource==(HANDLE)(UINT_PTR)0x778u ? 0x778u : 0x775u;
    PoolLastPresentationFormat=present->Format;++PoolCreates;
    a->pAllocationInfo->hAllocation=PoolHandles[slot];
    a->hKMResource=0x776u;
    return S_OK;
  }
  const ADMISSION_WIN32_ALLOCATION_CREATE *desc=a->pAllocationInfo->pPrivateDriverData;
  if(a->NumAllocations!=1 || !desc->Allocation.Size || desc->Allocation.Size>0x100000 ||
     (desc->Allocation.Size&0x3fff) || desc->ClassId<AgxWin32BufferClassGeneral ||
     desc->ClassId>AgxWin32BufferClassEncoder) return E_INVALIDARG;
  unsigned slot;
  for(slot=0;slot<ADMISSION_UMD_SCREEN_BUFFER_LIMIT && PoolMemory[slot];++slot) {}
  if(slot==ADMISSION_UMD_SCREEN_BUFFER_LIMIT) return E_OUTOFMEMORY;
  PoolMemory[slot]=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(SIZE_T)desc->Allocation.Size);
  if(!PoolMemory[slot]) return E_OUTOFMEMORY;
  ++PoolCreates;
  do { PoolHandles[slot]=0x700+(++PoolNextHandle); }
  while(PoolHandles[slot]>=0x771u && PoolHandles[slot]<=0x782u);
  a->pAllocationInfo->hAllocation=PoolHandles[slot];
  /* Allocation must already reserve the UMD slot against callback reentry. */
  if(PoolDevice.Magic==ADMISSION_UMD_DEVICE_MAGIC &&
     AdmissionUmdScreenBeginClose(&PoolDevice)!=HRESULT_FROM_WIN32(ERROR_BUSY)) ++PoolErrors;
  return S_OK;
}
static HRESULT APIENTRY PoolLock(HANDLE h,D3DDDICB_LOCK *a) {
  (void)h;
  if(PoolFailMap) return E_OUTOFMEMORY;
  for(unsigned slot=0;slot<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++slot) if(PoolMemory[slot] && a->hAllocation==PoolHandles[slot]) {
    a->pData=PoolMemory[slot]; ++PoolMaps; return S_OK;
  }
  return E_INVALIDARG;
}
static HRESULT APIENTRY PoolUnlock(HANDLE h,const D3DDDICB_UNLOCK *a) {
  (void)h;
  if(!a || !a->NumAllocations || !a->phAllocations) return E_INVALIDARG;
  PoolUnlocks+=a->NumAllocations;
  if(PoolFailUnlock) { --PoolFailUnlock; return E_FAIL; }
  return S_OK;
}
static HRESULT APIENTRY PoolDeallocate(HANDLE h,const D3DDDICB_DEALLOCATE *a) {
  (void)h;
  if(a->NumAllocations==0 &&
     (a->hResource==(HANDLE)(UINT_PTR)0x773u ||
      a->hResource==(HANDLE)(UINT_PTR)0x777u ||
      a->hResource==(HANDLE)(UINT_PTR)0x778u ||
      a->hResource==(HANDLE)(UINT_PTR)0x782u)) {
    if(a->hResource==(HANDLE)(UINT_PTR)0x777u ||
       a->hResource==(HANDLE)(UINT_PTR)0x778u) {
      D3DKMT_HANDLE expected=a->hResource==(HANDLE)(UINT_PTR)0x778u ? 0x778u : 0x775u;
      for(unsigned slot=0;slot<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++slot)
        if(PoolMemory[slot] && PoolHandles[slot]==expected) {
          HeapFree(GetProcessHeap(),0,PoolMemory[slot]);
          PoolMemory[slot]=NULL;PoolHandles[slot]=0;break;
        }
    }
    ++PoolPresentationDeletes; return S_OK;
  }
  if(a->NumAllocations!=1) return E_INVALIDARG;
  unsigned slot;
  for(slot=0;slot<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++slot) if(PoolMemory[slot] && a->HandleList[0]==PoolHandles[slot]) break;
  if(slot==ADMISSION_UMD_SCREEN_BUFFER_LIMIT) return E_INVALIDARG;
  if(PoolFailDeallocation) { --PoolFailDeallocation; return E_FAIL; }
  HeapFree(GetProcessHeap(),0,PoolMemory[slot]); PoolMemory[slot]=NULL; ++PoolDeletes; return S_OK;
}
static void PoolHolds(void *context,int active) {
  ADMISSION_UMD_ASAHI_OWNER *c=context;
  if(active==2) { PoolFailAllocation=1; return; }
  if(active==3) { PoolFailAllocation=0; return; }
  if(active==4) { PoolFailDeallocation=1; return; }
  AcquireSRWLockExclusive(&c->Device->ScreenBufferLock);
  for(unsigned i=0;i<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++i) {
    ADMISSION_UMD_SCREEN_BUFFER *b=&c->Device->ScreenBuffers[i];
    if(b->Active && b->NativeBackend==c->Backend) b->SubmissionHolds=active?1:0;
  }
  ReleaseSRWLockExclusive(&c->Device->ScreenBufferLock);
}
unsigned AgxWin32AsahiPoolTest(AGX_WIN32_SCREEN *,const AGX_WIN32_ASAHI_OWNER_OPS *,
    void *,AGX_WIN32_ASAHI_BACKEND *,void (*)(void *,int));
unsigned AgxWin32AsahiPipelineTest(AGX_WIN32_SCREEN *,const AGX_WIN32_ASAHI_OWNER_OPS *,
    void *,AGX_WIN32_ASAHI_BACKEND *);
unsigned AgxWin32AsahiStateDirtyZeroTest(AGX_WIN32_SCREEN *,
    const AGX_WIN32_ASAHI_OWNER_OPS *,void *,AGX_WIN32_ASAHI_BACKEND *);
#if defined(ADMISSION_UMD_NATIVE_RUNTIME_TEST)
#include "apple_agx_dynamic_job.h"
#include "render_dynamic_dma.h"
static unsigned RuntimeRenders,RuntimeSignals,RuntimeMaterializations;
static D3DKMT_HANDLE RuntimeExpectedTargetAllocation;
static APPLE_AGX_U64 RuntimeExpectedTargetBytes;
static APPLE_AGX_U32 RuntimeExpectedCommandVersion;
static ADMISSION_UMD_DEVICE *RuntimeActiveDevice;
static HANDLE RuntimeMarker;
static HANDLE RuntimeQueryMarkers[ADMISSION_UMD_SCREEN_FENCE_LIMIT*2];
static unsigned RuntimeQueryMarkerCount;
static unsigned RuntimeImmediateMarker;
static unsigned RuntimeFailSignals;
static unsigned RuntimeFailedSignalCalls;
static unsigned RuntimeTeardownDeletes,RuntimeTeardownUnlocks;
static const void *RuntimeTeardownBo;
static APPLE_AGX_U64 RuntimeCommand[APPLE_AGX_WIN32_COMMAND_MAX_BYTES/8];
static D3DDDI_ALLOCATIONLIST RuntimeAllocations[APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES];
static D3DDDI_PATCHLOCATIONLIST RuntimePatches[APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES];
static unsigned char RuntimeImages[2][APPLE_AGX_DYNAMIC_JOB_MAX_STORAGE_BYTES];
static APPLE_AGX_DYNAMIC_JOB RuntimeJobs[2];
static APPLE_AGX_U64 RuntimeDma[2][ADMISSION_DYNAMIC_DMA_MAX_BYTES/8];
typedef struct {
  ADMISSION_BACKEND_IMAGE Backend;
  ADMISSION_DYNAMIC_OVERLAY_PLAN Plan,WorkerPlan;
  ADMISSION_DYNAMIC_OVERLAY_BINDINGS Bindings;
  ADMISSION_DYNAMIC_OVERLAY_STATE State;
  ADMISSION_DYNAMIC_DMA_VIEW Dma;
  const APPLE_AGX_WIN32_COMMAND_VIEW *Source;
  unsigned char *Arena;
  APPLE_AGX_U64 DestinationGpu;
  unsigned DmaBytes;
} RUNTIME_CONSUMER;
static RUNTIME_CONSUMER RuntimeConsumers[2];
static APPLE_AGX_U32 RuntimeConsumerFence;
static unsigned RuntimeConsumerGates,RuntimeConsumerRetirements;
static int RuntimeExpectedTextureSubresource;
void AdmissionUmdRuntimeExpectTextureSubresource(int enabled) {
  RuntimeExpectedTextureSubresource=enabled;
}
#define RUNTIME_REQUIRE(x) do { if(!(x)) {++PoolErrors;fprintf(stderr,"RUNTIME_OWNER line=%u %s\n",(unsigned)__LINE__,#x);} } while(0)
static ADMISSION_UMD_SCREEN_BUFFER *RuntimeBuffer(ADMISSION_UMD_DEVICE *device,APPLE_AGX_U64 token) {
  if(!device) return NULL;
  for(unsigned i=0;i<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++i)
    if(device->ScreenBuffers[i].Active && device->ScreenBuffers[i].KernelAllocation==token)
      return &device->ScreenBuffers[i];
  return NULL;
}
static int RuntimeLookup(void *context,APPLE_AGX_U32 index,ADMISSION_WIN32_ALLOCATION_FACT *fact) {
  (void)context;
  ADMISSION_UMD_DEVICE *device=RuntimeActiveDevice;
  if(!device || index>=device->DrawSubmission->Count) return 0;
  ADMISSION_UMD_SCREEN_BUFFER *b=RuntimeBuffer(device,device->AllocationList[index].hAllocation);
  if(!b) return 0;
  memset(fact,0,sizeof(*fact));fact->AllocationToken=b->KernelAllocation;fact->Bytes=b->Bytes;
  fact->Generation=device->Win32Generation;fact->ClassId=b->ClassId;
  fact->Flags=b->Flags;fact->Writable=(b->Flags&AppleAgxWin32BufferGpuWrite)!=0;
  return 1;
}
static int RuntimeRead(void *context,APPLE_AGX_U64 token,APPLE_AGX_U32 ref,
    APPLE_AGX_U32 role,APPLE_AGX_U64 offset,APPLE_AGX_U32 bytes,void *out) {
  (void)context;(void)ref;(void)role;
  ADMISSION_UMD_SCREEN_BUFFER *b=RuntimeBuffer(RuntimeActiveDevice,token);
  if(!b || offset>b->Bytes || bytes>b->Bytes-offset) return 0;
  for(unsigned i=0;i<ARRAYSIZE(PoolMemory);++i) if(PoolMemory[i] && PoolHandles[i]==token) {
    memcpy(out,(unsigned char *)PoolMemory[i]+offset,bytes);return 1;
  }
  return 0;
}
static int RuntimeResolve(void *context,APPLE_AGX_U64 token,APPLE_AGX_U32 cls,
    APPLE_AGX_U32 ref,APPLE_AGX_U32 role,APPLE_AGX_U64 offset,APPLE_AGX_U32 bytes,APPLE_AGX_U64 *out) {
  RUNTIME_CONSUMER *consumer=context;
  (void)cls;
  ADMISSION_UMD_SCREEN_BUFFER *b=RuntimeBuffer(RuntimeActiveDevice,token);
  if(!consumer || !consumer->Source || !b || !bytes || offset>b->Bytes || bytes>b->Bytes-offset) return 0;
  if(role==AppleAgxWin32RoleRenderTarget && ref==consumer->Bindings.DestinationReference) {
    const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *source=&consumer->Source->References[ref];
    if(offset<source->Offset || offset-source->Offset>=source->Bytes ||
        bytes>source->Bytes-(offset-source->Offset)) return 0;
    *out=consumer->DestinationGpu+offset-source->Offset;
    return 1;
  }
  if(role==AppleAgxWin32RoleTexture && consumer->Source->Draw &&
     ref==consumer->Source->Draw->TextureReference) {
    const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *source=&consumer->Source->References[ref];
    if(offset<source->Offset || offset-source->Offset>=source->Bytes ||
       bytes>source->Bytes-(offset-source->Offset)) return 0;
    *out=consumer->DestinationGpu+0x2000000ULL+offset-source->Offset;
    return 1;
  }
  if(role==AppleAgxWin32RoleDepthAttachment && consumer->Source->NativeBatch &&
     (ref==consumer->Source->NativeBatch->DepthReference ||
      ref==consumer->Source->NativeBatch->StencilReference)) {
    const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *source=&consumer->Source->References[ref];
    if(offset<source->Offset || offset-source->Offset>=source->Bytes ||
       bytes>source->Bytes-(offset-source->Offset)) return 0;
    *out=consumer->DestinationGpu+
        (ref==consumer->Source->NativeBatch->DepthReference ?
          0x4000000ULL : 0x6000000ULL)+offset-source->Offset;
    return 1;
  }
  return AdmissionDynamicOverlayResolve(&consumer->Plan,ref,offset,bytes,out)==AdmissionDynamicOverlaySuccess;
}
static HRESULT RuntimeConsumerFailure(const char *stage,unsigned placement,unsigned result) {
  ++PoolErrors;
  fprintf(stderr,"NATIVE_KMD_GATE_FAIL: stage=%s placement=%u result=%u\n",stage,placement,result);
  return E_INVALIDARG;
}
static HRESULT APIENTRY RuntimeRender(HANDLE h,D3DDDICB_RENDER *r) {
  ADMISSION_UMD_DEVICE *device=RuntimeActiveDevice;
  ++RuntimeRenders;
  APPLE_AGX_WIN32_COMMAND_VIEW view;
  ADMISSION_WIN32_ALLOCATION_FACT facts[APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES];
  RUNTIME_REQUIRE(device && h==device->RuntimeDevice.handle &&
      r->hContext==device->KernelContext && r->CommandOffset==0 && r->NumPatchLocations==0);
  RUNTIME_REQUIRE(r && r->RenderCBSequence!=0u &&
      (r->RenderCBSequence&0x80000000u)==0u);
  RUNTIME_REQUIRE(device && device->DrawSubmission &&
      device->DrawSubmission->ResidencyHeld);
  if(device) for(unsigned allocation=0;allocation<r->NumAllocations;++allocation) {
    ADMISSION_UMD_SCREEN_BUFFER *buffer=NULL;
    for(unsigned slot=0;slot<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++slot)
      if(device->ScreenBuffers[slot].Active &&
         device->ScreenBuffers[slot].KernelAllocation==
             device->AllocationList[allocation].hAllocation) {
        buffer=&device->ScreenBuffers[slot];break;
      }
    RUNTIME_REQUIRE(buffer && (!buffer->NativeBo || !buffer->Mapped));
  }
  if(!device || AppleAgxWin32CommandValidate(device->CommandBuffer,r->CommandLength,
      device->Win32Generation,r->NumAllocations,&view)!=AppleAgxWin32AbiSuccess) return E_INVALIDARG;
  RUNTIME_REQUIRE((APPLE_AGX_WIN32_COMMAND_IS_NATIVE(view.Header->Version)) &&
      view.NativeBatch);
  if(RuntimeExpectedCommandVersion)
    RUNTIME_REQUIRE(view.Header->Version==RuntimeExpectedCommandVersion);
  if(AdmissionWin32ValidateReferences(&view,device->Win32Generation,RuntimeLookup,NULL,
      facts,ARRAYSIZE(facts))!=AdmissionWin32TransportSuccess) return E_INVALIDARG;
  if(RuntimeExpectedTextureSubresource) {
    unsigned texture=view.Draw->TextureReference;
    RUNTIME_REQUIRE(texture!=APPLE_AGX_WIN32_OPTIONAL_REFERENCE &&
        texture<view.Header->ReferenceCount && view.References[texture].Bytes>0 &&
        view.References[texture].Bytes<facts[texture].Bytes);
  }
  if(RuntimeExpectedTargetAllocation) {
    unsigned target=view.Draw->DestinationReference;
    RUNTIME_REQUIRE(target<view.Header->ReferenceCount &&
        view.References[target].AllocationIndex<r->NumAllocations &&
        device->AllocationList[view.References[target].AllocationIndex].hAllocation==
            RuntimeExpectedTargetAllocation &&
        view.References[target].Bytes==RuntimeExpectedTargetBytes);
  }
  RuntimeConsumerFence=device->NextScreenFence+1;
  if(!RuntimeConsumerFence) return RuntimeConsumerFailure("fence-range",0,0);
  printf("NATIVE_KMD_INPUT: references=%u relocations=%u allocations=%u encoder_bytes=%llu rt_bytes=%llu\n",
      view.Header->ReferenceCount,view.Draw->RelocationCount,r->NumAllocations,
      view.References[view.Draw->EncoderReference].Bytes,
      view.References[view.Draw->DestinationReference].Bytes);
  for(unsigned ref=0;ref<view.Header->ReferenceCount;++ref)
    printf("NATIVE_KMD_REFERENCE: index=%u role=%u class=%u bytes=%llu offset=%llu\n",
        ref,view.References[ref].Role,facts[ref].ClassId,
        view.References[ref].Bytes,view.References[ref].Offset);
  for(unsigned i=0;i<2;++i) {
    RUNTIME_CONSUMER *consumer=&RuntimeConsumers[i];
    ADMISSION_LOCAL_MEMORY_VIEW backend={0};
    unsigned result;
    /* The host backing models the existing KMD backend image. All object
     * placement comes from its production plan and existing mapped windows. */
    consumer->Arena=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,0x800000);
    if(!consumer->Arena) return E_OUTOFMEMORY;
    backend.CpuAddress=consumer->Arena;backend.Bytes=0x800000;
    backend.GpuVirtualAddress=0x1500800000ULL+i*0x10000000ULL;
    backend.HostPhysicalAddress=0x9d0800000ULL+i*0x10000000ULL;
    if(!AdmissionBackendImagePrepare(&consumer->Backend,&backend))
      return RuntimeConsumerFailure("backend-image",i,0);
    result=AdmissionDynamicOverlayPlan(&consumer->Backend,&view,&consumer->Plan);
    if(result!=AdmissionDynamicOverlaySuccess) return RuntimeConsumerFailure("plan",i,result);
    result=AdmissionDynamicOverlayBindingsFromView(&view,&consumer->Bindings);
    if(result!=AdmissionDynamicOverlaySuccess) return RuntimeConsumerFailure("bindings",i,result);
    consumer->Source=&view;
    consumer->DestinationGpu=0x1500200000ULL+i*0x10000000ULL;
    if(view.Header->Version==APPLE_AGX_WIN32_COMMAND_VERSION_DEPTH_BATCH) {
      unsigned depth=view.NativeBatch->DepthReference;
      const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *reference=&view.References[depth];
      if(!RuntimeResolve(consumer,facts[depth].AllocationToken,facts[depth].ClassId,
          depth,AppleAgxWin32RoleDepthAttachment,reference->Offset,1u,
          &consumer->Bindings.DepthGpuVirtualAddress))
        return RuntimeConsumerFailure("depth-resolve",i,0);
      if(view.NativeBatch->StencilReference!=APPLE_AGX_WIN32_OPTIONAL_REFERENCE) {
        unsigned stencil=view.NativeBatch->StencilReference;
        reference=&view.References[stencil];
        if(!RuntimeResolve(consumer,facts[stencil].AllocationToken,
            facts[stencil].ClassId,stencil,AppleAgxWin32RoleDepthAttachment,
            reference->Offset,1u,&consumer->Bindings.StencilGpuVirtualAddress))
          return RuntimeConsumerFailure("stencil-resolve",i,0);
      }
    }
    result=AppleAgxDynamicJobMaterialize(&view,facts,ARRAYSIZE(facts),0x1100000000ULL,
        RuntimeRead,RuntimeResolve,consumer,RuntimeImages[i],sizeof(RuntimeImages[i]),&RuntimeJobs[i]);
    if(result!=AppleAgxDynamicJobSuccess) return RuntimeConsumerFailure("materializer",i,result);
    ++RuntimeMaterializations;
  }
  RUNTIME_REQUIRE(RuntimeJobs[0].ObjectCount==RuntimeJobs[1].ObjectCount && RuntimeJobs[0].RelocationCount==view.Draw->RelocationCount);
  for(unsigned i=0;i<RuntimeJobs[0].RelocationCount;++i) {
    const APPLE_AGX_WIN32_RELOCATION *edge=&view.Relocations[i];
    const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *target=&view.References[edge->TargetReference];
    for(unsigned placement=0;placement<2;++placement) {
      APPLE_AGX_U64 expected=0;
      RUNTIME_REQUIRE(RuntimeResolve(&RuntimeConsumers[placement],facts[edge->TargetReference].AllocationToken,
          facts[edge->TargetReference].ClassId,edge->TargetReference,target->Role,
          target->Offset+edge->TargetOffset,1,&expected));
      RUNTIME_REQUIRE(RuntimeJobs[placement].Relocations[i].ResolvedAddress==expected);
    }
  }
  unsigned native_fields[3]={0};
  for(unsigned i=0;i<RuntimeJobs[0].ObjectCount;++i)
    RUNTIME_REQUIRE(RuntimeJobs[0].Objects[i].SourceHash==RuntimeJobs[1].Objects[i].SourceHash);
  for(unsigned i=0;i<RuntimeJobs[0].RelocationCount;++i) {
    const APPLE_AGX_DYNAMIC_JOB_RELOCATION *edge=&RuntimeJobs[0].Relocations[i];
    unsigned field;
    APPLE_AGX_U64 mask,shift;
    if(edge->Kind==AppleAgxWin32RelocationUniformAddress64) {field=0;mask=~0ULL;shift=0;}
    else if(edge->Kind==AppleAgxWin32RelocationTextureAddress40) {field=1;mask=((1ULL<<36)-1)<<2;shift=2;}
    else if(edge->Kind==AppleAgxWin32RelocationPbeAddress40) {field=2;mask=(1ULL<<36)-1;shift=0;}
    else continue;
    ++native_fields[field];
    unsigned object;
    for(object=0;object<RuntimeJobs[0].ObjectCount;++object)
      if(RuntimeJobs[0].Objects[object].ReferenceIndex==edge->DestinationReference) break;
    RUNTIME_REQUIRE(object<RuntimeJobs[0].ObjectCount);
    if(object==RuntimeJobs[0].ObjectCount) return E_INVALIDARG;
    APPLE_AGX_U64 source=0;
    const APPLE_AGX_WIN32_ALLOCATION_REFERENCE *ref=&view.References[edge->DestinationReference];
    RUNTIME_REQUIRE(RuntimeRead(NULL,facts[edge->DestinationReference].AllocationToken,
        edge->DestinationReference,ref->Role,ref->Offset+edge->DestinationOffset,8,&source));
    for(unsigned placement=0;placement<2;++placement) {
      APPLE_AGX_U64 packed=0;
      memcpy(&packed,RuntimeImages[placement]+RuntimeJobs[placement].Objects[object].StorageOffset+edge->DestinationOffset,8);
      APPLE_AGX_U64 address=field?((packed&mask)>>shift)<<4:packed;
      RUNTIME_REQUIRE(address==RuntimeJobs[placement].Relocations[i].ResolvedAddress);
      RUNTIME_REQUIRE((packed&~mask)==(source&~mask));
    }
  }
  RUNTIME_REQUIRE(native_fields[0] && native_fields[1] && native_fields[2]);
  RUNTIME_REQUIRE(AppleAgxDynamicDmaBytesHash(RuntimeImages[0],RuntimeJobs[0].StorageBytes)==RuntimeJobs[0].MaterializedHash);
  for(unsigned i=0;i<2;++i) {
    RUNTIME_CONSUMER *consumer=&RuntimeConsumers[i];
    APPLE_AGX_EXP208_GDI_BINDING outputBinding;
    ADMISSION_RENDER_PACKET_DESCRIPTION packet={0};
    APPLE_AGX_BACKEND_JOB_IMAGE staged;
    unsigned target=view.Draw->DestinationReference,targetSlot,result;
    for(targetSlot=0;targetSlot<ARRAYSIZE(PoolMemory);++targetSlot)
      if(PoolMemory[targetSlot] && PoolHandles[targetSlot]==facts[target].AllocationToken) break;
    if(targetSlot==ARRAYSIZE(PoolMemory)) return RuntimeConsumerFailure("destination-owner",i,0);
    result=AdmissionDynamicDmaBuild(view.Header->Generation,view.Header->ContentHash,
        consumer->DestinationGpu,view.References[target].AllocationIndex,0,0,
        &consumer->Bindings,&RuntimeJobs[i],RuntimeImages[i],RuntimeJobs[i].StorageBytes,
        RuntimeDma[i],sizeof(RuntimeDma[i]),&consumer->DmaBytes);
    if(result!=AdmissionDynamicDmaSuccess) return RuntimeConsumerFailure("dma-build",i,result);
    /* Exercise an actual Patch placement change, not a stale Render address. */
    consumer->DestinationGpu+=0x10000ULL;
    result=AdmissionDynamicDmaPatchDestination(RuntimeDma[i],consumer->DmaBytes,consumer->DestinationGpu);
    if(result!=AdmissionDynamicDmaSuccess) return RuntimeConsumerFailure("dma-patch",i,result);
    result=AdmissionDynamicDmaOpen(RuntimeDma[i],consumer->DmaBytes,&consumer->Dma);
    if(result!=AdmissionDynamicDmaSuccess) return RuntimeConsumerFailure("dma-open",i,result);
    result=AdmissionDynamicOverlayPlanFromJob(&consumer->Backend,consumer->Dma.Bindings,
        consumer->Dma.Job,&consumer->WorkerPlan);
    if(result!=AdmissionDynamicOverlaySuccess) return RuntimeConsumerFailure("worker-plan",i,result);
    RUNTIME_REQUIRE(consumer->Plan.EntryCount==consumer->WorkerPlan.EntryCount);
    for(unsigned e=0;e<consumer->Plan.EntryCount;++e) {
      RUNTIME_REQUIRE(consumer->Plan.Entries[e].ReferenceIndex==consumer->WorkerPlan.Entries[e].ReferenceIndex);
      RUNTIME_REQUIRE(consumer->Plan.Entries[e].GpuVirtualAddress==consumer->WorkerPlan.Entries[e].GpuVirtualAddress);
      RUNTIME_REQUIRE(consumer->Plan.Entries[e].Bytes==consumer->WorkerPlan.Entries[e].Bytes);
    }
    packet.Fence=RuntimeConsumerFence;
    packet.DestinationGpuVa=consumer->DestinationGpu;
    packet.DestinationPhysical=0x9d0200000ULL+i*0x10000000ULL;
    packet.DestinationBytes=(unsigned)consumer->Bindings.DestinationBytes;
    packet.DestinationCpuToken=(APPLE_AGX_U64)(UINT_PTR)((unsigned char *)PoolMemory[targetSlot]+view.References[target].Offset);
    /* The real producer's allocation list must survive the same prepatched
     * adoption and packet admission used between KMD Render and Submit. */
    {
      ADMISSION_PREPATCHED_RENDER pending;
      ADMISSION_RENDER_PACKET admitted;
      ADMISSION_RENDER_PACKET_DESCRIPTION adopted;
      packet.Fence=0;
      packet.ContextToken=(APPLE_AGX_U64)(UINT_PTR)device->KernelContext;
      packet.AllocationToken=facts[target].AllocationToken;
      packet.PrivateDataToken=(APPLE_AGX_U64)(UINT_PTR)RuntimeDma[i];
      packet.PrivateDataBytes=consumer->DmaBytes;
      packet.PrivateDataEnd=consumer->DmaBytes;
      packet.DmaEnd=consumer->DmaBytes;
      packet.PatchOffset=AdmissionDynamicDmaDestinationPatchOffset();
      packet.DestinationIndex=view.References[target].AllocationIndex;
      packet.AllocationCount=r->NumAllocations;
      printf("NATIVE_PACKET_ADMISSION: destination_reference=%u destination_index=%u allocation_count=%u\n",
          target,packet.DestinationIndex,r->NumAllocations);
      AdmissionPrepatchedInitialize(&pending);
      AdmissionRenderPacketInitialize(&admitted);
      if(!AdmissionPrepatchedCapture(&pending,&packet))
        return RuntimeConsumerFailure("packet-capture",i,packet.DestinationIndex);
      if(!AdmissionPrepatchedAdopt(&pending,RuntimeConsumerFence,packet.ContextToken,
          packet.PrivateDataToken,packet.DmaStart,packet.DmaEnd,&adopted))
        return RuntimeConsumerFailure("packet-adopt",i,packet.DestinationIndex);
      if(!AdmissionRenderPacketPrepare(&admitted,&adopted))
        return RuntimeConsumerFailure("packet-prepare",i,packet.DestinationIndex);
      packet=adopted;
    }
    if(!AdmissionBackendImageBindNativeSubmission(&consumer->Backend,&packet,
        (void *)(UINT_PTR)packet.DestinationCpuToken,consumer->Dma.Bindings,&outputBinding))
      return RuntimeConsumerFailure("native-output-bind",i,0);
    AdmissionDynamicOverlayStateInitialize(&consumer->State);
    result=AdmissionDynamicOverlayApply(&consumer->Backend,&consumer->WorkerPlan,
        consumer->Dma.Job,consumer->Dma.Storage,consumer->Dma.StorageBytes,RuntimeConsumerFence,&consumer->State);
    if(result!=AdmissionDynamicOverlaySuccess) return RuntimeConsumerFailure("overlay-apply",i,result);
    if(!AdmissionBackendImageStageJob(&consumer->Backend,RuntimeConsumerFence,0,1,2,2,APPLE_AGX_TRUE,&staged))
      return RuntimeConsumerFailure("stage-job",i,0);
    result=AdmissionDynamicOverlayRouteNative(&consumer->WorkerPlan,consumer->Dma.Bindings,
        consumer->Backend.Objects,APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT);
    if(result!=AdmissionDynamicOverlaySuccess) return RuntimeConsumerFailure("native-roots",i,result);
    if(view.Header->Version==APPLE_AGX_WIN32_COMMAND_VERSION_DEPTH_BATCH) {
      APPLE_AGX_U32 initialIsp=0,reloadIsp=0;
      const unsigned char *work=consumer->Backend.Objects[18].Data;
      memcpy(&initialIsp,work+0xb0u,sizeof(initialIsp));
      memcpy(&reloadIsp,work+0x6d8u,sizeof(reloadIsp));
      APPLE_AGX_U32 expectedIsp=0xc000u |
          ((view.NativeBatch->RenderFlags&
            APPLE_AGX_WIN32_NATIVE_RENDER_DEPTH_BIAS_IS_INT)?0x40000u:0u);
      RUNTIME_REQUIRE(initialIsp==expectedIsp && reloadIsp==expectedIsp);
      if(view.NativeBatch->StencilReference!=APPLE_AGX_WIN32_OPTIONAL_REFERENCE) {
        APPLE_AGX_U64 initialLoad=0,initialStore=0,initialLoadStride=0,
            initialStoreStride=0,initialLoadCompression=0,
            initialLoadCompressionStride=0,initialStoreCompression=0,
            initialStoreCompressionStride=0,reloadLoad=0,reloadStride=0,
            reloadCompressionStride=0,reloadStore=0,reloadPartial=0,
            reloadCompression=0;
        APPLE_AGX_U32 initialValues=0,reloadValues=0;
        memcpy(&initialLoad,work+0xf0u,8); memcpy(&initialStore,work+0xf8u,8);
        memcpy(&initialLoadStride,work+0x110u,8);
        memcpy(&initialStoreStride,work+0x118u,8);
        memcpy(&initialLoadCompression,work+0x140u,8);
        memcpy(&initialLoadCompressionStride,work+0x148u,8);
        memcpy(&initialStoreCompression,work+0x150u,8);
        memcpy(&initialStoreCompressionStride,work+0x158u,8);
        memcpy(&reloadLoad,work+0x690u,8); memcpy(&reloadStride,work+0x698u,8);
        memcpy(&reloadCompressionStride,work+0x6a0u,8);
        memcpy(&reloadStore,work+0x6a8u,8); memcpy(&reloadPartial,work+0x6b0u,8);
        memcpy(&reloadCompression,work+0x6b8u,8);
        memcpy(&initialValues,work+0x3fcu,4); memcpy(&reloadValues,work+0x744u,4);
        RUNTIME_REQUIRE(initialLoad==consumer->Bindings.StencilGpuVirtualAddress &&
            initialStore==consumer->Bindings.StencilGpuVirtualAddress &&
            initialLoadStride==view.NativeBatch->StencilStride &&
            initialStoreStride==view.NativeBatch->StencilStride &&
            !initialLoadCompression && !initialLoadCompressionStride &&
            !initialStoreCompression && !initialStoreCompressionStride &&
            reloadLoad==consumer->Bindings.StencilGpuVirtualAddress &&
            reloadStride==view.NativeBatch->StencilStride &&
            !reloadCompressionStride &&
            reloadStore==consumer->Bindings.StencilGpuVirtualAddress &&
            reloadPartial==consumer->Bindings.StencilGpuVirtualAddress &&
            !reloadCompression &&
            initialValues==(view.NativeBatch->IspBgobjValues|0x400u) &&
            reloadValues==view.NativeBatch->IspBgobjValues);
      }
    }
    ADMISSION_NATIVE_GRAPH_RECEIPT receipt;
    result=AdmissionDynamicOverlayCaptureNativeGraph(consumer->Dma.Bindings,&consumer->WorkerPlan,
        consumer->Dma.Job,consumer->Backend.Objects,APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
        consumer->Dma.Header->CommandHash,RuntimeConsumerFence,&receipt);
    if(result!=AdmissionDynamicOverlaySuccess) return RuntimeConsumerFailure("native-receipt",i,result);
    RUNTIME_REQUIRE(receipt.Valid && receipt.Fence==RuntimeConsumerFence &&
        receipt.CommandHash==view.Header->ContentHash &&
        receipt.GraphObjectCount==consumer->Dma.Job->ObjectCount &&
        receipt.GraphEdgeCount==consumer->Dma.Job->RelocationCount && !receipt.ReadbackAvailable &&
        receipt.RenderTargetGpuVa==consumer->DestinationGpu &&
        receipt.RenderTargetPhysical==packet.DestinationPhysical);
    for(unsigned e=0;e<consumer->Dma.Job->RelocationCount;++e) {
      const APPLE_AGX_DYNAMIC_JOB_RELOCATION *edge=&consumer->Dma.Job->Relocations[e];
      if(edge->TargetReference==target)
        RUNTIME_REQUIRE(edge->ResolvedAddress==consumer->DestinationGpu+edge->TargetOffset);
    }
    consumer->Source=NULL;
    ++RuntimeConsumerGates;
    printf("NATIVE_KMD_GATE: placement=%u objects=%u source_bytes=%u dma_bytes=%u encoder_bytes=%llu roots=PASS\n",
        i,consumer->WorkerPlan.EntryCount,consumer->Dma.StorageBytes,consumer->DmaBytes,
        view.References[view.Draw->EncoderReference].Bytes);
  }
  RUNTIME_REQUIRE(AdmissionUmdScreenBeginClose(device)==HRESULT_FROM_WIN32(ERROR_BUSY));
  printf("NATIVE_RUNTIME_GRAPH: references=%u relocations=%u allocations=%u objects=%u bytes=%u\n",
      view.Header->ReferenceCount,view.Draw->RelocationCount,r->NumAllocations,RuntimeJobs[0].ObjectCount,RuntimeJobs[0].StorageBytes);
  r->pNewCommandBuffer=RuntimeCommand;r->NewCommandBufferSize=sizeof(RuntimeCommand);
  r->pNewAllocationList=RuntimeAllocations;r->NewAllocationListSize=ARRAYSIZE(RuntimeAllocations);
  r->pNewPatchLocationList=RuntimePatches;r->NewPatchLocationListSize=ARRAYSIZE(RuntimePatches);
  return S_OK;
}
static HRESULT APIENTRY RuntimeSignal(HANDLE h,const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECT2 *signal) {
  ADMISSION_UMD_DEVICE *device=RuntimeActiveDevice;
  ++RuntimeSignals;
  RUNTIME_REQUIRE(device && h==device->RuntimeDevice.handle &&
      signal->hContext==device->KernelContext && signal->Flags.EnqueueCpuEvent);
  if(RuntimeFailSignals) {
    --RuntimeFailSignals;++RuntimeFailedSignalCalls;return E_FAIL;
  }
  if(device && device->DrawSubmission && !device->DrawSubmission->Fence) {
    RUNTIME_REQUIRE(device->NextScreenFence==
        RuntimeConsumerFence+RuntimeFailedSignalCalls);
    RuntimeMarker=signal->CpuEventHandle;
    if(RuntimeImmediateMarker) RUNTIME_REQUIRE(SetEvent(RuntimeMarker));
  } else {
    RUNTIME_REQUIRE(RuntimeQueryMarkerCount<ARRAYSIZE(RuntimeQueryMarkers));
    if(RuntimeQueryMarkerCount<ARRAYSIZE(RuntimeQueryMarkers))
      RuntimeQueryMarkers[RuntimeQueryMarkerCount++]=signal->CpuEventHandle;
  }
  return S_OK; /* first case deliberately pending; second signals in callback */
}
static void RuntimeCheckpoint(void *context,unsigned phase) {
  ADMISSION_UMD_ASAHI_OWNER *owner=context;
  if(phase==1 || phase==6) {
    if(phase==6) RUNTIME_REQUIRE(RuntimeImmediateMarker && WaitForSingleObject(RuntimeMarker,0)==WAIT_OBJECT_0);
    RUNTIME_REQUIRE(RuntimeRenders==1 &&
        RuntimeSignals==1+RuntimeQueryMarkerCount+RuntimeFailedSignalCalls && RuntimeMaterializations==2 &&
        RuntimeConsumerGates==2 && RuntimeMarker);
    RUNTIME_REQUIRE(owner->Device->NativeBatchTransaction && owner->Device->DrawSubmission);
    unsigned holds=0;
    for(unsigned i=0;i<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++i) holds+=owner->Device->ScreenBuffers[i].SubmissionHolds;
    RUNTIME_REQUIRE(holds>0 && AdmissionUmdScreenBeginClose(owner->Device)==HRESULT_FROM_WIN32(ERROR_BUSY));
    RUNTIME_REQUIRE(owner->Device->DrawSubmission && owner->Device->DrawSubmission->Fence==RuntimeConsumerFence);
    for(unsigned i=0;i<2;++i) {
      RUNTIME_CONSUMER *consumer=&RuntimeConsumers[i];
      if(consumer->State.Applied) {
        RUNTIME_REQUIRE(AdmissionDynamicOverlayRelease(&consumer->Backend,&consumer->WorkerPlan,
            consumer->Dma.Job,consumer->Dma.Storage,consumer->Dma.StorageBytes,
            RuntimeConsumerFence+1,&consumer->State)==AdmissionDynamicOverlayState);
        RUNTIME_REQUIRE(consumer->State.Applied && consumer->Backend.BoundFence==RuntimeConsumerFence);
        RUNTIME_REQUIRE(AdmissionDynamicOverlayRelease(&consumer->Backend,&consumer->WorkerPlan,
            consumer->Dma.Job,consumer->Dma.Storage,consumer->Dma.StorageBytes,
            RuntimeConsumerFence,&consumer->State)==AdmissionDynamicOverlaySuccess);
      }
      if(consumer->Backend.BoundFence) {
        RUNTIME_REQUIRE(AdmissionBackendImageReleaseSubmission(&consumer->Backend,RuntimeConsumerFence));
        ++RuntimeConsumerRetirements;
      }
      RUNTIME_REQUIRE(!consumer->State.Applied && !consumer->Backend.BoundFence);
    }
    /* Windows/native source holds are still live until the same ordered marker
     * is released after the consumer's retirement, never at native call return. */
    RUNTIME_REQUIRE(AdmissionUmdScreenBeginClose(owner->Device)==HRESULT_FROM_WIN32(ERROR_BUSY));
    RUNTIME_REQUIRE(SetEvent(RuntimeMarker));
  } else if(phase==5) {
    RUNTIME_REQUIRE(!owner->Device->NativeBatchTransaction && !owner->Device->DrawSubmission &&
        RuntimeRenders==1 && RuntimeSignals==1+RuntimeQueryMarkerCount+RuntimeFailedSignalCalls && RuntimeMaterializations==2 &&
        RuntimeConsumerGates==2 && RuntimeConsumerRetirements==2);
    for(unsigned i=0;i<2;++i) {
      RUNTIME_CONSUMER *consumer=&RuntimeConsumers[i];
      RUNTIME_REQUIRE(!consumer->State.Applied && !consumer->Backend.BoundFence);
      if(consumer->Arena && !consumer->State.Applied && !consumer->Backend.BoundFence)
        HeapFree(GetProcessHeap(),0,consumer->Arena);
    }
    memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
    RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;
    RuntimeConsumerGates=RuntimeConsumerRetirements=0;RuntimeConsumerFence=0;
    RuntimeMarker=NULL;RuntimeQueryMarkerCount=0;
    memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));RuntimeFailedSignalCalls=0;
    RuntimeImmediateMarker=1;
  } else if(phase==3) {
    RUNTIME_REQUIRE(!owner->Device->NativeBatchTransaction && !owner->Device->DrawSubmission &&
        owner->Device->NativeBackendCount==1 && owner->Backend->Native && owner->Backend->LiveBos==1);
    unsigned found=0;
    for(unsigned i=0;i<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++i) {
      ADMISSION_UMD_SCREEN_BUFFER *b=&owner->Device->ScreenBuffers[i];
      if(b->Active && b->NativeBackend==owner->Backend) {
        ++found;RuntimeTeardownBo=b->NativeBo;
        RUNTIME_REQUIRE(b->NativeBo && b->Mapped && !b->SubmissionHolds);
      }
    }
    RUNTIME_REQUIRE(found==1);
    RuntimeTeardownDeletes=PoolDeletes;RuntimeTeardownUnlocks=PoolUnlocks;
    /* First destroy attempts dispose at rodata unref and again at Detach's
     * Collect. Both must fail so the second destructor call is a real retry. */
    PoolFailDeallocation=2;
  } else if(phase==4) {
    RUNTIME_REQUIRE(!owner->Device->NativeBatchTransaction && !owner->Device->DrawSubmission &&
        owner->Device->NativeBackendCount==1 && owner->Backend->Native && owner->Backend->LiveBos==1);
    RUNTIME_REQUIRE(PoolFailDeallocation==0 && PoolDeletes==RuntimeTeardownDeletes &&
        PoolUnlocks==RuntimeTeardownUnlocks+1);
    unsigned found=0;
    for(unsigned i=0;i<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++i) {
      ADMISSION_UMD_SCREEN_BUFFER *b=&owner->Device->ScreenBuffers[i];
      if(b->Active && b->NativeBackend==owner->Backend) {
        ++found;
        RUNTIME_REQUIRE(b->NativeBo==RuntimeTeardownBo && !b->Mapped && !b->SubmissionHolds);
      }
    }
    RUNTIME_REQUIRE(found==1);
    PoolFailDeallocation=0;
  } else {
    RUNTIME_REQUIRE(!owner->Device->NativeBatchTransaction && !owner->Device->DrawSubmission && !owner->Device->NativeBackendCount);
    if(RuntimeConsumerGates==2) RUNTIME_REQUIRE(RuntimeConsumerRetirements==2);
    if(RuntimeTeardownBo) {
      RUNTIME_REQUIRE(PoolDeletes==RuntimeTeardownDeletes+1 &&
          PoolUnlocks==RuntimeTeardownUnlocks+1);
      RuntimeTeardownBo=NULL; /* The next normal screen lifetime has new counts. */
    }
  }
}
unsigned AgxWin32AsahiRuntimeTest(AGX_WIN32_SCREEN *,const AGX_WIN32_ASAHI_OWNER_OPS *,
    void *,AGX_WIN32_ASAHI_BACKEND *,const AGX_WIN32_ASAHI_BATCH_OPS *,void (*)(void *,unsigned));
#endif

static unsigned TestAsahiNativePoolOwner(void) {
  AGX_WIN32_ASAHI_BACKEND backend={0};
  AGX_WIN32_ASAHI_OWNER_OPS ops;
  ADMISSION_UMD_ASAHI_OWNER owner={&PoolDevice,&backend};
  memset(&PoolDevice,0,sizeof(PoolDevice)); memset(&PoolAdapter,0,sizeof(PoolAdapter));
  memset(&PoolCallbacks,0,sizeof(PoolCallbacks));
  PoolErrors=PoolCreates=PoolMaps=PoolUnlocks=PoolDeletes=0;
  PoolNextHandle=0; memset(PoolMemory,0,sizeof(PoolMemory)); memset(PoolHandles,0,sizeof(PoolHandles));
  PoolFailAllocation=0;
  PoolFailMap=0;
  PoolFailDeallocation=0;
  PoolDevice.Magic=ADMISSION_UMD_DEVICE_MAGIC; PoolDevice.Win32Generation=27;
  PoolDevice.Adapter=&PoolAdapter; PoolDevice.KernelCallbacks=&PoolCallbacks;
  PoolAdapter.DeviceInfo.Magic=AGX_WIN32_DEVICE_INFO_MAGIC;
  PoolAdapter.DeviceInfo.Version=AGX_WIN32_DEVICE_INFO_VERSION;
  PoolAdapter.DeviceInfo.Bytes=sizeof(PoolAdapter.DeviceInfo);
  PoolAdapter.DeviceInfo.BootGeneration=9; PoolAdapter.DeviceInfo.GpuGeneration=13;
  PoolAdapter.DeviceInfo.GpuVariant=AgxWin32GpuG13G;
  PoolAdapter.DeviceInfo.PageBytes=0x4000; PoolAdapter.DeviceInfo.ClassCount=3;
  for(unsigned i=0;i<3;++i) {
    PoolAdapter.DeviceInfo.Classes[i].ClassId=i+1;
    PoolAdapter.DeviceInfo.Classes[i].MinimumAlignment=0x4000;
    PoolAdapter.DeviceInfo.Classes[i].MaximumBytes=0x100000;
    PoolAdapter.DeviceInfo.Classes[i].Flags=i==0?0xf:0x6;
  }
  PoolCallbacks.pfnAllocateCb=PoolAllocate; PoolCallbacks.pfnLockCb=PoolLock;
  PoolCallbacks.pfnUnlockCb=PoolUnlock; PoolCallbacks.pfnDeallocateCb=PoolDeallocate;
  PoolCallbacks.pfnCreatePagingQueueCb=TestCreatePagingQueue;
  PoolCallbacks.pfnDestroyPagingQueueCb=TestDestroyPagingQueue;
  PoolCallbacks.pfnMakeResidentCb=TestMakeResident;
  PoolCallbacks.pfnEvictCb=TestEvict;
  if(AdmissionUmdScreenInitialize(&PoolDevice)!=S_OK) return 1;
  AdmissionUmdAsahiOwnerOperations(&ops);
#if defined(ADMISSION_UMD_NATIVE_RUNTIME_TEST)
  RuntimeActiveDevice=&PoolDevice;
  RuntimeRenders=RuntimeSignals=RuntimeMaterializations=0;RuntimeMarker=NULL;RuntimeQueryMarkerCount=0;memset(RuntimeQueryMarkers,0,sizeof(RuntimeQueryMarkers));RuntimeFailedSignalCalls=0;RuntimeImmediateMarker=0;
  RuntimeTeardownBo=NULL;RuntimeTeardownDeletes=RuntimeTeardownUnlocks=0;
  RuntimeConsumerGates=RuntimeConsumerRetirements=0;RuntimeConsumerFence=0;
  memset(RuntimeConsumers,0,sizeof(RuntimeConsumers));
  PoolDevice.KernelContext=(HANDLE)(UINT_PTR)0x707;
  PoolDevice.RuntimeDevice.handle=(VOID *)(UINT_PTR)0x706;
  PoolDevice.PagingQueue=0x601u;
  PoolDevice.CommandBuffer=RuntimeCommand;PoolDevice.CommandBufferSize=sizeof(RuntimeCommand);
  PoolDevice.AllocationList=RuntimeAllocations;PoolDevice.AllocationListSize=ARRAYSIZE(RuntimeAllocations);
  PoolDevice.PatchList=RuntimePatches;PoolDevice.PatchListSize=ARRAYSIZE(RuntimePatches);
  PoolCallbacks.pfnRenderCb=RuntimeRender;
  PoolCallbacks.pfnSignalSynchronizationObject2Cb=RuntimeSignal;
  PoolErrors+=AgxWin32AsahiRuntimeTest(&PoolDevice.Screen,&ops,&owner,&backend,
      AdmissionUmdAsahiBatchOperations(),RuntimeCheckpoint);
  RUNTIME_REQUIRE(PoolCreates==PoolDeletes && PoolMaps==PoolUnlocks && !PoolDevice.NativeBackendCount);
  for(unsigned i=0;i<ARRAYSIZE(PoolMemory);++i) RUNTIME_REQUIRE(!PoolMemory[i]);
  for(unsigned i=0;i<2;++i) {
    RUNTIME_CONSUMER *consumer=&RuntimeConsumers[i];
    if(consumer->Arena && !consumer->State.Applied && !consumer->Backend.BoundFence) {
      HeapFree(GetProcessHeap(),0,consumer->Arena);consumer->Arena=NULL;
    }
    RUNTIME_REQUIRE(!consumer->Arena);
  }
  if(!PoolErrors && RuntimeRenders==1 && RuntimeMaterializations==2 && RuntimeConsumerGates==2 && RuntimeConsumerRetirements==2 && RuntimeImmediateMarker)
    printf("NATIVE_RUNTIME_EXECUTION: actual_producer=PASS materializer=PASS kmd_plan=PASS dma_patch=PASS native_roots=PASS retirement=PASS\n");
#else
  PoolErrors+=AgxWin32AsahiPoolTest(&PoolDevice.Screen,&ops,&owner,&backend,PoolHolds);
  PoolErrors+=AgxWin32AsahiPipelineTest(&PoolDevice.Screen,&ops,&owner,&backend);
  /* Original pool/pipeline seven plus dedicated root and reserve BOs. */
  unsigned expectedCreates=9;
#ifdef ADMISSION_UMD_NATIVE_STATE_TEST
  /* Opt-in until the real state emitter runtime closure is linked. */
  PoolErrors+=AgxWin32AsahiStateDirtyZeroTest(&PoolDevice.Screen,&ops,&owner,&backend);
  ++expectedCreates;
#endif
  printf("NATIVE_OWNER_BALANCE: creates=%u maps=%u unlocks=%u deletes=%u expected=%u backends=%u\n",
      PoolCreates,PoolMaps,PoolUnlocks,PoolDeletes,expectedCreates,PoolDevice.NativeBackendCount);
  if(PoolCreates!=expectedCreates || PoolMaps!=expectedCreates ||
     PoolUnlocks!=expectedCreates || PoolDeletes!=expectedCreates || PoolDevice.NativeBackendCount)
    ++PoolErrors;
  for(unsigned i=0;i<ADMISSION_UMD_SCREEN_BUFFER_LIMIT;++i) if(PoolMemory[i]) ++PoolErrors;
#endif
  return PoolErrors;
}
