#include <windows.h>
#include <wingdi.h>
typedef _Return_type_success_(return >= 0) LONG NTSTATUS;
#pragma warning(push)
#pragma warning(disable : 4201)
#include <d3d10umddi.h>
#pragma warning(pop)
#include "../src/umd_internal.h"
#include "apple_agx_dynamic_job.h"
#include "agx_win32_reloc_capture.h"
#include "umd_asahi_batch_adapter.h"
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
  ADMISSION_WIN32_ALLOCATION_FACT facts[APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES];
  unsigned i;
  (void)h; ++Composer_calls;
  REQUIRE(r->hContext == Composer_device.KernelContext && r->CommandOffset == 0);
  REQUIRE(r->NumAllocations == 8 && r->NumPatchLocations == 0);
  REQUIRE(r->RenderCBSequence == Composer_calls);
  REQUIRE(Composer_device.DrawSubmission &&
      Composer_device.DrawSubmission->ResidencyHeld);
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
      Composer_lookup,&Composer_device,facts,ARRAYSIZE(facts))==AdmissionWin32TransportSuccess);
  for(i=0; i<2; ++i) {
    Composer_placement=0x1100000000ULL + i*0x100000;
    REQUIRE(AppleAgxDynamicJobMaterialize(&view, facts, ARRAYSIZE(facts), 0x1100000000ULL,
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
  REQUIRE(FAILED(AdmissionUmdDrawRetire(&Composer_device,&Composer_tx,0,FALSE)));
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
  Composer_device.RuntimeDevice.handle=(VOID *)(UINT_PTR)8;
  Composer_device.PagingQueue=0x601u;
  Composer_callbacks.pfnRenderCb=Composer_render;
  Composer_callbacks.pfnMakeResidentCb=TestMakeResident;
  Composer_callbacks.pfnEvictCb=TestEvict;
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
      {
        REQUIRE(FAILED(AdmissionUmdDrawAbort(&Composer_device,&Composer_tx)));
        if(attempt==0) {
          HANDLE event=Composer_device.ScreenFences[0].Event;
          Composer_device.ScreenFences[0].Event=NULL;
          Composer_device.LastScreenError=S_OK;
          REQUIRE(FAILED(AdmissionUmdDrawRetire(&Composer_device,&Composer_tx,0,FALSE)));
          REQUIRE(Composer_tx.Phase==AdmissionDrawAccepted);
          for(i=0;i<8;++i) REQUIRE(Composer_device.ScreenBuffers[i].SubmissionHolds==1);
          Composer_device.ScreenFences[0].Event=event;
        }
        if(attempt==4) {
          REQUIRE(AdmissionUmdDrawRetire(&Composer_device,&Composer_tx,0,FALSE)==HRESULT_FROM_WIN32(ERROR_TIMEOUT));
          for(i=0;i<8;++i) REQUIRE(Composer_device.ScreenBuffers[i].SubmissionHolds==1);
          SetEvent(Composer_device.ScreenFences[0].Event);
        }
        Composer_mode=0;
        REQUIRE(AdmissionUmdDrawRetire(&Composer_device,&Composer_tx,0,FALSE)==S_OK);
        REQUIRE(FAILED(AdmissionUmdDrawRetire(&Composer_device,&Composer_tx,0,FALSE)));
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
          REQUIRE(AdmissionUmdDrawRetire(&Composer_device,&Composer_tx,0,FALSE)==S_OK);
          REQUIRE(Composer_calls==2 && Composer_signals==2);
        }
      }
    }
    for(i=0;i<8;++i) REQUIRE(Composer_device.ScreenBuffers[i].SubmissionHolds==0);
  }
  printf("UMD COMPOSER: Composer_failures=%u\n",Composer_failures);
  return Composer_failures;
}

/* The adapter is deliberately tested against the public typed-capture contract,
 * rather than any Linux batch or sync object.  Native Asahi capture embeds this
 * exact object, so the same source-hold rules apply when the real hook lands. */
typedef struct _COMPOSER_CAPTURE_OWNER {
  unsigned Holds, Releases;
} COMPOSER_CAPTURE_OWNER;
static int Composer_capture_query(void *context, APPLE_AGX_U64 token,
                                  AGX_WIN32_RELOC_ALLOCATION *out) {
  COMPOSER_CAPTURE_OWNER *owner=(COMPOSER_CAPTURE_OWNER *)context;
  unsigned slot;
  (void)owner;
  if(!out || token<10 || token>17) return 0;
  slot=(unsigned)(token-10);
  if(!Composer_device.ScreenBuffers[slot].Active) return 0;
  *out=(AGX_WIN32_RELOC_ALLOCATION){Composer_device.OwnerCookie,token,
    20+slot,0x8000,Composer_device.Win32Generation,slot,
    slot==2 ? (AppleAgxWin32AccessRead|AppleAgxWin32AccessExecute) :
              (AppleAgxWin32AccessRead|AppleAgxWin32AccessWrite)};
  return 1;
}
static int Composer_capture_retain(void *context, APPLE_AGX_U64 token,
                                   APPLE_AGX_U64 serial) {
  COMPOSER_CAPTURE_OWNER *owner=(COMPOSER_CAPTURE_OWNER *)context;
  AGX_WIN32_RELOC_ALLOCATION current;
  if(!Composer_capture_query(context,token,&current) || current.Serial!=serial)
    return 0;
  ++owner->Holds;
  return 1;
}
static int Composer_capture_retain_exact(void *context,
    const AGX_WIN32_RELOC_ALLOCATION *expected) {
  AGX_WIN32_RELOC_ALLOCATION current;
  if(!expected || !Composer_capture_query(context,expected->Token,&current) ||
     memcmp(&current,expected,sizeof(current))) return 0;
  return Composer_capture_retain(context,expected->Token,expected->Serial);
}
static void Composer_capture_release(void *context, APPLE_AGX_U64 token,
                                     APPLE_AGX_U64 serial) {
  COMPOSER_CAPTURE_OWNER *owner=(COMPOSER_CAPTURE_OWNER *)context;
  AGX_WIN32_RELOC_ALLOCATION current;
  if(!Composer_capture_query(context,token,&current) || current.Serial!=serial ||
     !owner->Holds) { ++Composer_failures; return; }
  --owner->Holds; ++owner->Releases;
}

unsigned AdmissionUmdAsahiBatchAdapterTests(void) {
  COMPOSER_CAPTURE_OWNER owner={0};
  AGX_WIN32_RELOC_CAPTURE capture={0};
  const AGX_WIN32_RELOC_OPERATIONS ops={Composer_capture_query,
    Composer_capture_retain,Composer_capture_release,Composer_capture_retain_exact};
  APPLE_AGX_WIN32_DRAW_PAYLOAD draw={0};
  APPLE_AGX_U32 refs[10];
  ADMISSION_UMD_ASAHI_BATCH *batch=(ADMISSION_UMD_ASAHI_BATCH *)HeapAlloc(
      GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*batch));
  unsigned failuresBefore=Composer_failures;
  REQUIRE(batch!=NULL);
  if(!batch) return 1;
  /* Real invariant: after entry into Render, enqueue failure and timeout must
   * preserve native and Windows holds, permit only retirement, and never replay. */
  for(unsigned attempt=0;attempt<5;++attempt) {
  Composer_setup();
  memset(batch,0,sizeof(*batch)); memset(&capture,0,sizeof(capture));
  memset(&owner,0,sizeof(owner));
  REQUIRE(AgxWin32RelocBegin(&capture,Composer_device.OwnerCookie,
      Composer_device.Win32Generation,77,&ops,&owner)==AgxRelocOk);
  for(unsigned i=0;i<10;++i) {
    static const unsigned roles[]={1,2,6,6,9,7,11,12,10,9};
    static const unsigned slots[]={0,1,2,2,3,4,5,6,7,7};
    unsigned access=i==0?AppleAgxWin32AccessWrite:
      ((i==2||i==3)?(AppleAgxWin32AccessRead|AppleAgxWin32AccessExecute):AppleAgxWin32AccessRead);
    REQUIRE(AgxWin32RelocReference(&capture,10+slots[i],roles[i],access,
      (i==3 || i==9)?0x4000:0,0x4000,&refs[i])==AgxRelocOk);
  }
  REQUIRE(AgxWin32RelocField(&capture,AppleAgxWin32RelocationEncoderAddress,
      refs[8],0,refs[0],0)==AgxRelocOk);
  REQUIRE(AgxWin32RelocField(&capture,AppleAgxWin32RelocationEncoderAddress,
      refs[8],8,refs[1],0)==AgxRelocOk);
  REQUIRE(AgxWin32RelocField(&capture,AppleAgxWin32RelocationVdmPipelineOffset32,
      refs[8],16,refs[4],0)==AgxRelocOk);
  REQUIRE(AgxWin32RelocField(&capture,AppleAgxWin32RelocationUscShaderOffset32,
      refs[4],0,refs[2],0)==AgxRelocOk);
  REQUIRE(AgxWin32RelocField(&capture,AppleAgxWin32RelocationUscShaderOffset32,
      refs[4],8,refs[3],0)==AgxRelocOk);
  REQUIRE(AgxWin32RelocField(&capture,AppleAgxWin32RelocationUscBufferAddress40,
      refs[4],16,refs[5],0)==AgxRelocOk);
  draw.StructBytes=sizeof(draw); draw.Format=AppleAgxWin32FormatBgra8Unorm;
  draw.SurfaceWidth=16; draw.SurfaceHeight=16; draw.SurfacePitch=64;
  draw.Topology=AppleAgxWin32TopologyTriangleList; draw.VertexCount=3; draw.InstanceCount=1;
  draw.DestinationReference=refs[0]; draw.VertexReference=refs[1];
  draw.VertexShaderReference=refs[2]; draw.FragmentShaderReference=refs[3];
  draw.UscPipelineReference=refs[4]; draw.DescriptorReference=refs[5];
  draw.ScissorReference=refs[6]; draw.DepthBiasReference=refs[7]; draw.EncoderReference=refs[8];
  draw.Reserved[APPLE_AGX_WIN32_DRAW_V2_FRAGMENT_USC_PIPELINE_RESERVED_INDEX]=refs[9];
  draw.IndexReference=draw.ConstantReference=draw.TextureReference=
    draw.VertexRodataReference=draw.FragmentRodataReference=APPLE_AGX_WIN32_OPTIONAL_REFERENCE;
  {
    AGX_WIN32_DRAW_REQUEST preflight={0}; APPLE_AGX_U64 command[512]; APPLE_AGX_U32 bytes=0;
    preflight.Generation=capture.Generation; preflight.AllocationCount=8;
    preflight.ReferenceCount=capture.ReferenceCount; preflight.RelocationCount=capture.RelocationCount;
    preflight.References=capture.References; preflight.Relocations=capture.Relocations; preflight.Draw=draw;
    APPLE_AGX_WIN32_ABI_RESULT abi=AgxWin32TransportBuildDrawVersion(&preflight,
        APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_USC,command,sizeof(command),&bytes);
    if(abi!=AppleAgxWin32AbiSuccess) fprintf(stderr,"ASAHI_BATCH preflight_abi=%u\n",(unsigned)abi);
    REQUIRE(abi==AppleAgxWin32AbiSuccess);
  }
  HRESULT seal=AdmissionUmdAsahiBatchSeal(&Composer_device,&capture,77,
      APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_USC,&draw,batch);
  REQUIRE(seal==S_OK);
  REQUIRE(owner.Holds==10 && owner.Releases==0);
  Composer_mode=attempt;
  HRESULT dispatched=AdmissionUmdAsahiBatchDispatch(&Composer_device,batch);
  REQUIRE(attempt>0 && attempt<4 ? FAILED(dispatched) : dispatched==S_OK);
  REQUIRE(batch->Phase==AdmissionAsahiBatchSubmitted);
  REQUIRE(Composer_calls==1 && Composer_tx.Phase==AdmissionDrawEmpty);
  REQUIRE(AdmissionUmdAsahiBatchDispatch(&Composer_device,batch)==HRESULT_FROM_WIN32(ERROR_BUSY));
  REQUIRE(FAILED(AdmissionUmdAsahiBatchAbort(&Composer_device,batch)));
  REQUIRE(Composer_calls==1 && owner.Holds==10 && owner.Releases==0);
  for(unsigned i=0;i<8;++i) REQUIRE(Composer_device.ScreenBuffers[i].SubmissionHolds==1);
  if(attempt==0) {
    HANDLE live=Composer_device.KernelContext;
    APPLE_AGX_U64 savedOwner=batch->Owner,savedRequest=batch->RequestId;
    APPLE_AGX_U32 savedGeneration=batch->Generation;
    unsigned signals=Composer_signals;
    Composer_device.QuiescedKernelContext=live;
    Composer_device.KernelContextQuiesced=TRUE;
    Composer_device.KernelContext=NULL;
    Composer_device.NativeBatchTransaction=batch;
    Composer_device.LastNativeRequest=batch->RequestId;
    REQUIRE(FAILED(AdmissionUmdDrawDispatch(&Composer_device,&batch->Submission)));
    batch->Submission.Context=(HANDLE)(UINT_PTR)10;
    REQUIRE(AdmissionUmdAsahiBatchRetire(&Composer_device,batch,0)==E_INVALIDARG);
    batch->Submission.Context=live;
    batch->Owner++;
    REQUIRE(AdmissionUmdAsahiBatchRetire(&Composer_device,batch,0)==E_INVALIDARG);
    batch->Owner=savedOwner;
    batch->Generation++;
    REQUIRE(AdmissionUmdAsahiBatchRetire(&Composer_device,batch,0)==E_INVALIDARG);
    batch->Generation=savedGeneration;
    batch->RequestId++;
    REQUIRE(AdmissionUmdAsahiBatchRetire(&Composer_device,batch,0)==E_INVALIDARG);
    batch->RequestId=savedRequest;
    Composer_device.NativeBatchTransaction=&Composer_tx;
    REQUIRE(AdmissionUmdAsahiBatchRetire(&Composer_device,batch,0)==E_INVALIDARG);
    Composer_device.NativeBatchTransaction=batch;
    {
      APPLE_AGX_U32 fence=batch->Submission.Fence;
      ADMISSION_UMD_DRAW_PHASE phase=batch->Submission.Phase;
      batch->Submission.Fence=0;
      REQUIRE(AdmissionUmdDrawRetire(&Composer_device,&batch->Submission,0,TRUE)==E_INVALIDARG);
      REQUIRE(Composer_signals==signals && batch->Submission.Phase==phase);
      batch->Submission.Fence=fence;
      batch->Submission.Phase=AdmissionDrawSealed;
      REQUIRE(AdmissionUmdDrawRetire(&Composer_device,&batch->Submission,0,TRUE)==E_INVALIDARG);
      batch->Submission.Phase=phase;
    }
    Composer_device.KernelContext=live;
    REQUIRE(AdmissionUmdAsahiBatchRetire(&Composer_device,batch,0)==E_INVALIDARG);
    Composer_device.KernelContext=NULL;
  }
  if(attempt==3) {
    REQUIRE(batch->Submission.Fence==0 && capture.Fence==0);
    REQUIRE(FAILED(AdmissionUmdAsahiBatchRetire(&Composer_device,batch,0)));
    REQUIRE(batch->Phase==AdmissionAsahiBatchSubmitted);
    REQUIRE(Composer_calls==1 && owner.Holds==10 && owner.Releases==0);
    for(unsigned i=0;i<8;++i) REQUIRE(Composer_device.ScreenBuffers[i].SubmissionHolds==1);
    /* Retry enqueues an unsignalled event: acquiring a fence alone is not
     * completion and must not release either ownership set. */
    Composer_mode=4;
  }
  if(attempt==3 || attempt==4) {
    REQUIRE(AdmissionUmdAsahiBatchRetire(&Composer_device,batch,0)==HRESULT_FROM_WIN32(ERROR_TIMEOUT));
    REQUIRE(batch->Phase==AdmissionAsahiBatchSubmitted);
    REQUIRE(batch->Submission.Fence!=0 && capture.Fence==batch->Submission.Fence);
    REQUIRE(Composer_calls==1 && owner.Holds==10 && owner.Releases==0);
    for(unsigned i=0;i<8;++i) REQUIRE(Composer_device.ScreenBuffers[i].SubmissionHolds==1);
    SetEvent(Composer_device.ScreenFences[0].Event);
  }
  Composer_mode=0;
  REQUIRE(AdmissionUmdAsahiBatchRetire(&Composer_device,batch,0)==S_OK);
  REQUIRE(owner.Holds==0 && owner.Releases==10 && Composer_calls==1);
  for(unsigned i=0;i<8;++i) REQUIRE(Composer_device.ScreenBuffers[i].SubmissionHolds==0);
  REQUIRE(AdmissionUmdAsahiBatchRetire(&Composer_device,batch,0)==E_INVALIDARG);
  }
  HeapFree(GetProcessHeap(),0,batch);
  printf("UMD ASAHI BATCH ADAPTER: failures=%u\n",Composer_failures-failuresBefore);
  return Composer_failures-failuresBefore;
}
