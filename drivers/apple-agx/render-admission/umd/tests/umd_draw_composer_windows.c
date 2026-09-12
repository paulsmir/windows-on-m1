#include <windows.h>
#include <wingdi.h>
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(push)
#pragma warning(disable : 4201)
#include <d3d10umddi.h>
#pragma warning(pop)
#include "../src/umd_internal.h"
#include "apple_agx_dynamic_job.h"
#include <stdio.h>
#include <string.h>

static unsigned Composer_failures, Composer_calls, Composer_signals, Composer_mode;
static ADMISSION_UMD_DEVICE Composer_device;
static ADMISSION_UMD_ADAPTER Composer_adapter;
static ADMISSION_UMD_DRAW_SUBMISSION Composer_tx;
static D3DDDI_DEVICECALLBACKS Composer_callbacks;
static APPLE_AGX_U64 Composer_cmd[512], Composer_nextcmd[512];
static D3DDDI_ALLOCATIONLIST Composer_list[16], Composer_nextlist[16];
static D3DDDI_PATCHLOCATIONLIST Composer_patches[16];
static unsigned char Composer_data[8][0x8000], Composer_output[2][0x40000];
static APPLE_AGX_DYNAMIC_JOB Composer_jobs[2];
static APPLE_AGX_U64 Composer_placement;
static unsigned Composer_unexpectedCallbacks;
static BOOL Composer_reordered;
#define REQUIRE(x) do { if (!(x)) { ++Composer_failures; fprintf(stderr, "COMPOSER line %u: %s\n", (unsigned)__LINE__, #x); } } while (0)

static HRESULT APIENTRY Composer_lock(HANDLE h, D3DDDICB_LOCK *a) {
  (void)h; (void)a; ++Composer_unexpectedCallbacks; return E_UNEXPECTED;
}
static HRESULT APIENTRY Composer_unlock(HANDLE h, const D3DDDICB_UNLOCK *a) {
  (void)h; (void)a; ++Composer_unexpectedCallbacks; return E_UNEXPECTED;
}
static HRESULT APIENTRY Composer_deallocate(HANDLE h, const D3DDDICB_DEALLOCATE *a) {
  (void)h; (void)a; ++Composer_unexpectedCallbacks; return E_UNEXPECTED;
}

static int Composer_read_source(void *c, APPLE_AGX_U64 t, APPLE_AGX_U32 r,
    APPLE_AGX_U32 role, APPLE_AGX_U64 off, APPLE_AGX_U32 n, void *out) {
  (void)c; (void)r; (void)role;
  if (t < 101 || t > 108 || off > 0x8000 || n > 0x8000 - off) return 0;
  memcpy(out, Composer_data[t-101]+off, n); return 1;
}
static int Composer_resolve_source(void *c, APPLE_AGX_U64 t, APPLE_AGX_U32 cls,
    APPLE_AGX_U32 r, APPLE_AGX_U32 role, APPLE_AGX_U64 off,
    APPLE_AGX_U32 n, APPLE_AGX_U64 *out) {
  (void)c; (void)cls; (void)r; (void)role;
  if (t < 101 || t > 108 || off >= 0x8000 || n != 1) return 0;
  *out = Composer_placement + (t-100)*0x10000 + off; return 1;
}
static int Composer_lookup(void *ctx, APPLE_AGX_U32 index, ADMISSION_WIN32_ALLOCATION_FACT *fact) {
  ADMISSION_UMD_DEVICE *d=ctx;
  ADMISSION_UMD_SCREEN_BUFFER *b;
  unsigned slot;
  if(index>=8) return 0;
  for(slot=0;slot<8;++slot)
    if(d->ScreenBuffers[slot].KernelAllocation==d->AllocationList[index].hAllocation) break;
  if(slot==8) return 0;
  b=&d->ScreenBuffers[slot];
  memset(fact,0,sizeof(*fact));
  fact->AllocationToken=d->AllocationList[index].hAllocation;
  fact->Bytes=0x8000; fact->Generation=d->Win32Generation;
  fact->ClassId=b->ClassId;
  fact->Flags=b->Flags;
  fact->Writable=d->AllocationList[index].WriteOperation;
  return 1;
}
static HRESULT APIENTRY Composer_render(HANDLE h, D3DDDICB_RENDER *r) {
  APPLE_AGX_WIN32_COMMAND_VIEW view;
  ADMISSION_WIN32_ALLOCATION_FACT facts[9];
  unsigned i;
  (void)h; ++Composer_calls;
  REQUIRE(r->hContext == Composer_device.KernelContext && r->CommandOffset == 0);
  REQUIRE(r->NumAllocations == 8 && r->NumPatchLocations == 0);
  REQUIRE(AdmissionUmdDrawDispatch(&Composer_device, &Composer_tx) == HRESULT_FROM_WIN32(ERROR_BUSY));
  REQUIRE(AdmissionUmdScreenBeginClose(&Composer_device) == HRESULT_FROM_WIN32(ERROR_BUSY));
  {
    ULONG undeallocated=0;
    void *mapped=NULL;
    unsigned before=Composer_unexpectedCallbacks;
    REQUIRE(!Composer_device.Screen.Transport.Operations.DestroyBuffer(&Composer_device,11));
    REQUIRE(!Composer_device.Screen.Transport.Operations.MapBuffer(&Composer_device,11,0,16,AppleAgxWin32BufferCpuRead,&mapped));
    Composer_device.ScreenBuffers[0].Mapped=TRUE;
    REQUIRE(!Composer_device.Screen.Transport.Operations.UnmapBuffer(&Composer_device,10));
    Composer_device.ScreenBuffers[0].Mapped=FALSE;
    REQUIRE(FAILED(AdmissionUmdScreenDetachNativeBo(&Composer_device,10,&Composer_data[0],1)));
    REQUIRE(AdmissionUmdScreenFinalize(&Composer_device,&undeallocated)==HRESULT_FROM_WIN32(ERROR_BUSY));
    REQUIRE(Composer_device.Magic==ADMISSION_UMD_DEVICE_MAGIC);
    REQUIRE(Composer_unexpectedCallbacks==before);
  }
  for (i=0; i<8; ++i) {
    REQUIRE(Composer_device.ScreenBuffers[i].SubmissionHolds == 1);
    unsigned expected=Composer_reordered && i<2?1-i:i;
    REQUIRE(Composer_device.AllocationList[i].hAllocation == 101+expected);
    REQUIRE(Composer_device.AllocationList[i].Value == (expected == 0 ? 1u : 0u));
    memset(&facts[i], 0, sizeof(facts[i]));
    facts[i].AllocationToken = Composer_device.AllocationList[i].hAllocation;
    facts[i].Bytes = 0x8000;
  }
  REQUIRE(AppleAgxWin32CommandValidate(Composer_device.CommandBuffer, r->CommandLength,
      Composer_device.Win32Generation, 8, &view) == AppleAgxWin32AbiSuccess);
  REQUIRE(view.References[2].AllocationIndex == view.References[3].AllocationIndex);
  REQUIRE(AdmissionWin32ValidateReferences(&view,Composer_device.Win32Generation,
      Composer_lookup,&Composer_device,facts,9)==AdmissionWin32TransportSuccess);
  for(i=0; i<2; ++i) {
    Composer_placement=0x1100000000ULL + i*0x100000;
    REQUIRE(AppleAgxDynamicJobMaterialize(&view, facts, 9, 0x1100000000ULL,
        Composer_read_source, Composer_resolve_source, &Composer_device, Composer_output[i], sizeof(Composer_output[i]),
        &Composer_jobs[i]) == AppleAgxDynamicJobSuccess);
  }
  for(i=0; i<Composer_jobs[0].RelocationCount; ++i)
    REQUIRE(Composer_jobs[1].Relocations[i].ResolvedAddress ==
            Composer_jobs[0].Relocations[i].ResolvedAddress + 0x100000);
  r->pNewCommandBuffer=Composer_nextcmd; r->NewCommandBufferSize=sizeof(Composer_nextcmd);
  r->pNewAllocationList=Composer_nextlist; r->NewAllocationListSize=16;
  r->pNewPatchLocationList=Composer_patches; r->NewPatchLocationListSize=16;
  if(Composer_mode == 1) return E_FAIL;
  if(Composer_mode == 2) r->pNewCommandBuffer=NULL;
  return S_OK;
}
static HRESULT APIENTRY Composer_signal_event(HANDLE h, const D3DDDICB_SIGNALSYNCHRONIZATIONOBJECT2 *s) {
  (void)h; ++Composer_signals;
  REQUIRE(FAILED(AdmissionUmdDrawRetire(&Composer_device,&Composer_tx,0)));
  if(Composer_mode==3) return E_FAIL;
  REQUIRE(s->hContext==Composer_device.KernelContext && s->Flags.EnqueueCpuEvent);
  if(Composer_mode!=4) SetEvent(s->CpuEventHandle);
  return S_OK;
}
static void Composer_setup(void) {
  unsigned i;
  memset(&Composer_device,0,sizeof(Composer_device)); memset(&Composer_tx,0,sizeof(Composer_tx));
  memset(&Composer_callbacks,0,sizeof(Composer_callbacks));
  Composer_calls=Composer_signals=Composer_mode=0;
  Composer_reordered=FALSE;
  Composer_device.Magic=ADMISSION_UMD_DEVICE_MAGIC; Composer_device.Win32Generation=7;
  memset(&Composer_adapter,0,sizeof(Composer_adapter)); Composer_device.Adapter=&Composer_adapter;
  Composer_adapter.DeviceInfo.Magic=AGX_WIN32_DEVICE_INFO_MAGIC;
  Composer_adapter.DeviceInfo.Version=AGX_WIN32_DEVICE_INFO_VERSION;
  Composer_adapter.DeviceInfo.Bytes=sizeof(Composer_adapter.DeviceInfo);
  Composer_adapter.DeviceInfo.BootGeneration=1; Composer_adapter.DeviceInfo.GpuGeneration=13;
  Composer_adapter.DeviceInfo.GpuVariant=AgxWin32GpuG13G;
  Composer_adapter.DeviceInfo.PageBytes=0x4000; Composer_adapter.DeviceInfo.ClassCount=3;
  for(i=0;i<3;++i) {
    Composer_adapter.DeviceInfo.Classes[i].ClassId=i+1;
    Composer_adapter.DeviceInfo.Classes[i].MinimumAlignment=0x4000;
    Composer_adapter.DeviceInfo.Classes[i].MaximumBytes=0x10000;
    Composer_adapter.DeviceInfo.Classes[i].Flags=0xf;
  }
  Composer_device.KernelContext=(HANDLE)(UINT_PTR)9; Composer_device.KernelCallbacks=&Composer_callbacks;
  Composer_callbacks.pfnRenderCb=Composer_render;
  Composer_callbacks.pfnLockCb=Composer_lock;
  Composer_callbacks.pfnUnlockCb=Composer_unlock;
  Composer_callbacks.pfnDeallocateCb=Composer_deallocate;
  Composer_callbacks.pfnSignalSynchronizationObject2Cb=Composer_signal_event;
  REQUIRE(AdmissionUmdScreenInitialize(&Composer_device)==S_OK);
  Composer_device.CommandBuffer=Composer_cmd; Composer_device.CommandBufferSize=sizeof(Composer_cmd);
  Composer_device.AllocationList=Composer_list; Composer_device.AllocationListSize=16;
  Composer_device.PatchList=Composer_patches; Composer_device.PatchListSize=16;
  for(i=0;i<8;++i) {
    ADMISSION_UMD_SCREEN_BUFFER *b=&Composer_device.ScreenBuffers[i];
    b->Active=TRUE; b->Token=10+i; b->Serial=20+i;
    b->KernelAllocation=101+i; b->Bytes=0x8000;
    b->NativeBo=&Composer_data[i]; b->NativeBoSerial=1;
    b->Flags=AppleAgxWin32BufferGpuRead|AppleAgxWin32BufferGpuWrite;
    b->ClassId=i==2?AgxWin32BufferClassShader:
      (i>=3?AgxWin32BufferClassEncoder:AgxWin32BufferClassGeneral);
  }
}

unsigned AdmissionUmdDrawComposerTests(void) {
  APPLE_AGX_WIN32_ALLOCATION_REFERENCE refs[9];
  APPLE_AGX_WIN32_ALLOCATION_REFERENCE savedRefs[9];
  APPLE_AGX_WIN32_RELOCATION relocs[6];
  AGX_WIN32_RELOC_ALLOCATION ids[9];
  AGX_WIN32_RELOC_ALLOCATION savedIds[9];
  AGX_WIN32_DRAW_REQUEST request;
  unsigned i, attempt;
  static const unsigned roles[]={1,2,6,6,9,7,11,12,10};
  static const unsigned slots[]={0,1,2,2,3,4,5,6,7};
  Composer_failures=0;
  for(attempt=0;attempt<20;++attempt) {
    Composer_setup(); memset(&request,0,sizeof(request));
    for(i=0;i<9;++i) {
      unsigned slot=slots[i], access=i==0?2:((i==2||i==3)?5:1);
      ids[i]=(AGX_WIN32_RELOC_ALLOCATION){Composer_device.OwnerCookie,10+slot,20+slot,0x8000,7,55,access};
      refs[i]=(APPLE_AGX_WIN32_ALLOCATION_REFERENCE){55,access,roles[i],0,i==3?0x4000:0,0x4000};
    }
    relocs[0]=(APPLE_AGX_WIN32_RELOCATION){AppleAgxWin32RelocationEncoderAddress,8,0,8,0,0,0,0};
    relocs[1]=(APPLE_AGX_WIN32_RELOCATION){AppleAgxWin32RelocationEncoderAddress,8,0,8,1,8,0,0};
    relocs[2]=(APPLE_AGX_WIN32_RELOCATION){AppleAgxWin32RelocationVdmPipelineOffset32,4,0,8,4,16,0,0};
    relocs[3]=(APPLE_AGX_WIN32_RELOCATION){AppleAgxWin32RelocationUscShaderOffset32,6,0,4,2,0,0,0};
    relocs[4]=(APPLE_AGX_WIN32_RELOCATION){AppleAgxWin32RelocationUscShaderOffset32,6,0,4,3,8,0,0};
    relocs[5]=(APPLE_AGX_WIN32_RELOCATION){AppleAgxWin32RelocationUscBufferAddress40,8,0,4,5,16,0,0};
    request.Generation=7; request.AllocationCount=56;
    request.ReferenceCount=9; request.RelocationCount=6;
    request.References=refs; request.Relocations=relocs;
    request.Draw.Format=1; request.Draw.SurfaceWidth=16; request.Draw.SurfaceHeight=16;
    request.Draw.SurfacePitch=64; request.Draw.Topology=1; request.Draw.VertexCount=3;
    request.Draw.InstanceCount=1; request.Draw.DestinationReference=0;
    request.Draw.VertexReference=1; request.Draw.VertexShaderReference=2;
    request.Draw.FragmentShaderReference=3; request.Draw.UscPipelineReference=4;
    request.Draw.DescriptorReference=5; request.Draw.ScissorReference=6;
    request.Draw.DepthBiasReference=7; request.Draw.EncoderReference=8;
    request.Draw.IndexReference=request.Draw.ConstantReference=request.Draw.TextureReference=
      request.Draw.VertexRodataReference=request.Draw.FragmentRodataReference=APPLE_AGX_WIN32_OPTIONAL_REFERENCE;
    if(attempt==5) ids[8].Serial++;
    if(attempt==6) ids[8].Owner++;
    if(attempt==7) refs[8].Bytes=0x9000;
    if(attempt==8) Composer_device.ScreenBuffers[7].KernelAllocation=101;
    if(attempt==9) ids[8].Generation++;
    if(attempt==10) Composer_device.ScreenBuffers[0].Flags=AppleAgxWin32BufferGpuRead;
    if(attempt==11) Composer_device.ScreenBuffers[7].Transition=TRUE;
    if(attempt==12) Composer_device.ScreenBuffers[7].SubmissionHolds=MAXUINT32;
    if(attempt==13) request.Draw.VertexCount=0;
    if(attempt==14) Composer_device.ScreenBuffers[3].ClassId=AgxWin32BufferClassGeneral;
    if(attempt==18) ids[8].Token=999;
    if(attempt==19) refs[3].Offset=0x2000; /* overlapping reads are legal; overlapping relocation writes are not */
    if(attempt==19) relocs[5].DestinationOffset=4;
    if(attempt==17) {
      ids[1]=ids[0]; ids[1].Access=AppleAgxWin32AccessRead;
      refs[1].Offset=0x4000;
    }
    if((attempt>=5 && attempt<15) || attempt>=18) {
      REQUIRE(FAILED(AdmissionUmdDrawSeal(&Composer_device,1,1,&request,ids,&Composer_tx)));
      REQUIRE(Composer_calls==0 && Composer_device.DrawSubmission==NULL);
      REQUIRE(Composer_tx.Phase==AdmissionDrawEmpty && Composer_tx.Count==0);
      if(attempt==12) {
        REQUIRE(Composer_device.ScreenBuffers[7].SubmissionHolds==MAXUINT32);
        Composer_device.ScreenBuffers[7].SubmissionHolds=0;
      }
    } else {
      REQUIRE(AdmissionUmdDrawSeal(&Composer_device,1,1,&request,ids,&Composer_tx)==S_OK);
      if(attempt==17) {
        REQUIRE(Composer_tx.Count==7 && Composer_tx.Allocations[0].Value==1);
        REQUIRE(AdmissionUmdDrawAbort(&Composer_device,&Composer_tx)==S_OK);
        for(i=0;i<8;++i) REQUIRE(Composer_device.ScreenBuffers[i].SubmissionHolds==0);
        continue;
      }
      REQUIRE(Composer_tx.Count==8 && refs[8].AllocationIndex==55);
      if(attempt==15) {
        REQUIRE(AdmissionUmdDrawAbort(&Composer_device,&Composer_tx)==S_OK);
        REQUIRE(FAILED(AdmissionUmdDrawAbort(&Composer_device,&Composer_tx)));
        REQUIRE(Composer_calls==0);
        continue;
      }
      if(attempt==16) {
        Composer_device.AllocationListSize=7;
        REQUIRE(FAILED(AdmissionUmdDrawDispatch(&Composer_device,&Composer_tx)));
        REQUIRE(Composer_device.DrawSubmission==NULL && Composer_calls==0);
        for(i=0;i<8;++i) REQUIRE(Composer_device.ScreenBuffers[i].SubmissionHolds==0);
        continue;
      }
      memcpy(savedRefs,refs,sizeof(refs)); memcpy(savedIds,ids,sizeof(ids));
      memset(refs,0,sizeof(refs)); memset(ids,0,sizeof(ids));
      Composer_mode=attempt;
      if(attempt==0) REQUIRE(AdmissionUmdDrawDispatch(&Composer_device,&Composer_tx)==S_OK);
      else if(attempt<4) REQUIRE(FAILED(AdmissionUmdDrawDispatch(&Composer_device,&Composer_tx)));
      else REQUIRE(AdmissionUmdDrawDispatch(&Composer_device,&Composer_tx)==S_OK);
      REQUIRE(Composer_calls==1);
      REQUIRE(FAILED(AdmissionUmdDrawDispatch(&Composer_device,&Composer_tx)));
      if(attempt!=1) {
        REQUIRE(FAILED(AdmissionUmdDrawAbort(&Composer_device,&Composer_tx)));
        if(attempt==4) {
          REQUIRE(AdmissionUmdDrawRetire(&Composer_device,&Composer_tx,0)==HRESULT_FROM_WIN32(ERROR_TIMEOUT));
          for(i=0;i<8;++i) REQUIRE(Composer_device.ScreenBuffers[i].SubmissionHolds==1);
          SetEvent(Composer_device.ScreenFences[0].Event);
        }
        Composer_mode=0;
        REQUIRE(AdmissionUmdDrawRetire(&Composer_device,&Composer_tx,0)==S_OK);
        REQUIRE(FAILED(AdmissionUmdDrawRetire(&Composer_device,&Composer_tx,0)));
        if(attempt==0) {
          APPLE_AGX_WIN32_COMMAND_VIEW view;
          APPLE_AGX_WIN32_ALLOCATION_REFERENCE temp;
          AGX_WIN32_RELOC_ALLOCATION idTemp;
          memset(&Composer_tx,0,sizeof(Composer_tx));
          memcpy(refs,savedRefs,sizeof(refs)); memcpy(ids,savedIds,sizeof(ids));
          REQUIRE(FAILED(AdmissionUmdDrawSeal(&Composer_device,1,1,&request,ids,&Composer_tx)));
          temp=refs[0]; refs[0]=refs[1]; refs[1]=temp;
          idTemp=ids[0]; ids[0]=ids[1]; ids[1]=idTemp;
          request.Draw.DestinationReference=1; request.Draw.VertexReference=0;
          relocs[0].TargetReference=1; relocs[1].TargetReference=0;
          REQUIRE(AdmissionUmdDrawSeal(&Composer_device,2,1,&request,ids,&Composer_tx)==S_OK);
          REQUIRE(Composer_tx.Allocations[0].hAllocation==102 && Composer_tx.Allocations[0].Value==0);
          REQUIRE(Composer_tx.Allocations[1].hAllocation==101 && Composer_tx.Allocations[1].Value==1);
          REQUIRE(AppleAgxWin32CommandValidate(Composer_tx.Command,Composer_tx.CommandBytes,7,8,&view)==AppleAgxWin32AbiSuccess);
          REQUIRE(view.Draw->DestinationReference==1 && view.References[1].AllocationIndex==1);
          Composer_reordered=TRUE;
          REQUIRE(AdmissionUmdDrawDispatch(&Composer_device,&Composer_tx)==S_OK);
          REQUIRE(Composer_tx.Fence==2);
          REQUIRE(AdmissionUmdDrawRetire(&Composer_device,&Composer_tx,0)==S_OK);
          REQUIRE(Composer_calls==2 && Composer_signals==2);
        }
      }
    }
    for(i=0;i<8;++i) REQUIRE(Composer_device.ScreenBuffers[i].SubmissionHolds==0);
  }
  printf("UMD COMPOSER: Composer_failures=%u\n",Composer_failures);
  return Composer_failures;
}
