#include "agx_kmt_native_qualification.h"
#include "agx_kmt_native_bridge.h"
#include "agx_win32_asahi_scene.h"
#include "render_dynamic_overlay.h"
#include <stdio.h>

#define NATIVE_WAIT_MS 15000u

typedef struct {
  UINT ExpectedCandidateBuild;
  ULONGLONG PriorSnapshotGeneration;
  AGX_KMT_NATIVE_BRIDGE *Bridge;
  AGX_KMT_NATIVE_BINDING Binding;
  struct pipe_screen *Screen;
  struct pipe_context *Context;
  AGX_WIN32_ASAHI_SCENE Scene;
  AGX_KMT_NATIVE_RECEIPT Callback;
  ADMISSION_NATIVE_GRAPH_RECEIPT Graph;
} NATIVE_QUALIFICATION;
/* One-shot lifetime storage remains reachable on the existing preserve path.
 * Allocation identities continue to live solely in the existing ScreenBuffers. */
static NATIVE_QUALIFICATION *NativePending;

static ULONGLONG native_hash(const unsigned char *data,UINT bytes) {
  ULONGLONG hash=14695981039346656037ULL;
  for(UINT i=0;i<bytes;++i) {hash^=data[i];hash*=1099511628211ULL;}
  return hash;
}
static int dump_receipt(const NATIVE_QUALIFICATION *q) {
  wchar_t name[96];DWORD written=0;
  if(swprintf_s(name,ARRAYSIZE(name),L"native-output-%016llx.bin",q->Graph.CommandHash)<0) return 0;
  HANDLE file=CreateFileW(name,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
  if(file==INVALID_HANDLE_VALUE) return 0;
  BOOL ok=WriteFile(file,&q->Graph,sizeof(q->Graph),&written,NULL);
  CloseHandle(file);return ok && written==sizeof(q->Graph);
}
static UINT native_pixel(const unsigned char *bytes,UINT x,UINT y) {
  /* AGX tiled64: interleave pixel coordinates within the existing64x64 tile.
   * The qualified16x16 image occupies one tile; no inferred linear pitch. */
  UINT index=0;
  for(UINT bit=0;bit<6;++bit) index|=((x>>bit)&1u)<<(2*bit) | ((y>>bit)&1u)<<(2*bit+1);
  const unsigned char *p=bytes+4*index;
  return p[0]|((UINT)p[1]<<8)|((UINT)p[2]<<16)|((UINT)p[3]<<24);
}
static int pixel_near(UINT actual,UINT expected) {
  for(UINT channel=0;channel<4;++channel) {
    int delta=(int)((actual>>(8*channel))&255u)-(int)((expected>>(8*channel))&255u);
    if(delta < -1 || delta > 1) return 0;
  }
  return 1;
}
static int verify_output(const NATIVE_QUALIFICATION *q) {
  const AGX_WIN32_ASAHI_SCENE_RECEIPT *scene=&q->Scene.Receipt;
  const ADMISSION_NATIVE_GRAPH_RECEIPT *g=&q->Graph;
  if(scene->Width!=16 || scene->Height!=16 || scene->Tiling!=1 ||
      scene->TargetBytes!=0x4000 || g->ReadbackBytes!=scene->TargetBytes ||
      g->ReadbackBytes>sizeof(g->ReadbackData) ||
      native_hash(g->ReadbackData,g->ReadbackBytes)!=g->ReadbackFnv1a) return 0;
  UINT mismatches=0,foreground=0;
  for(UINT y=0;y<16;++y) for(UINT x=0;x<16;++x) {
    /* The actual scene vertices/viewport map to (0,0),(16,0),(8,16).
     * Test pixel centres; these edges never pass exactly through a centre. */
    int inside=(4*x+2>=2*y+1) && (4*x+2<=63-2*y);
    UINT expected=inside?scene->ExpectedForegroundBgra8:scene->ExpectedBackgroundBgra8;
    if(inside) ++foreground;
    if(!pixel_near(native_pixel(g->ReadbackData,x,y),expected)) ++mismatches;
  }
  fprintf(stderr,"NATIVE_KMT_PIXELS: mismatches=%u foreground=%u background=0x%08x foreground_color=0x%08x hash=%016llx\n",
      mismatches,foreground,scene->ExpectedBackgroundBgra8,scene->ExpectedForegroundBgra8,g->ReadbackFnv1a);
  return mismatches==0 && foreground==128;
}
static int snapshot_is_new(const ADMISSION_NATIVE_GRAPH_RECEIPT *g,
    UINT build,UINT boot,ULONGLONG prior) {
  return g && build && boot && g->CandidateBuild==build &&
      g->BootGeneration==boot && g->SnapshotGeneration>prior;
}
static int capture_output_baseline(NATIVE_QUALIFICATION *q) {
  DWORD bytes=sizeof(q->Graph),type=0;
  if(!q->Binding.DeviceInfo || !q->Binding.DeviceInfo->BootGeneration || !q->ExpectedCandidateBuild) return 0;
  ZeroMemory(&q->Graph,sizeof(q->Graph));
  q->PriorSnapshotGeneration=0;
  LSTATUS status=RegGetValueW(HKEY_LOCAL_MACHINE,
      L"SYSTEM\\CurrentControlSet\\Services\\AppleAgxAdmission",
      L"Wom1NativeGraphReceipt",RRF_RT_REG_BINARY,&type,&q->Graph,&bytes);
  if(status==ERROR_FILE_NOT_FOUND || status==ERROR_PATH_NOT_FOUND) {
    fprintf(stderr,"NATIVE_KMT_BASELINE: absent registry=%ld prior=0\n",status);
    return 1;
  }
  if(status!=ERROR_SUCCESS || type!=REG_BINARY || bytes!=sizeof(q->Graph) ||
      q->Graph.Version!=1 || q->Graph.Bytes!=sizeof(q->Graph) || q->Graph.Valid!=1 ||
      !q->Graph.CandidateBuild || !q->Graph.BootGeneration || !q->Graph.Generation ||
      !q->Graph.Fence || !q->Graph.CommandHash || q->Graph.ReadbackAvailable>1 ||
      q->Graph.ReadbackBytes>sizeof(q->Graph.ReadbackData)) {
    fprintf(stderr,"NATIVE_KMT_BASELINE_REJECT: registry=%ld type=%lu bytes=%lu version=%u valid=%u\n",
        status,type,bytes,q->Graph.Version,q->Graph.Valid);
    return 0;
  }
  /* Generation is an adapter output-sequence value. Do not filter by command
   * hash: process ID reuse can regenerate the same Win32 generation/hash. */
  if(q->Graph.CandidateBuild==q->ExpectedCandidateBuild &&
      q->Graph.BootGeneration==q->Binding.DeviceInfo->BootGeneration)
    q->PriorSnapshotGeneration=q->Graph.SnapshotGeneration;
  fprintf(stderr,"NATIVE_KMT_BASELINE: build=%u boot=%u prior=%llu recorded_build=%u recorded_boot=%u\n",
      q->ExpectedCandidateBuild,q->Binding.DeviceInfo->BootGeneration,q->PriorSnapshotGeneration,
      q->Graph.CandidateBuild,q->Graph.BootGeneration);
  return 1;
}
#if defined(AGX_KMT_NATIVE_QUALIFICATION_TEST)
unsigned AgxKmtNativeQualificationFreshnessContractTest(void) {
  ADMISSION_NATIVE_GRAPH_RECEIPT g={0};
  unsigned failures=0;
  g.CandidateBuild=42;g.BootGeneration=7;g.CommandHash=0x1234;
  g.SnapshotGeneration=19;
  if(snapshot_is_new(&g,42,7,19)) ++failures; /* identical old matching receipt */
  g.SnapshotGeneration=18;
  if(snapshot_is_new(&g,42,7,19)) ++failures;
  g.SnapshotGeneration=20;
  if(!snapshot_is_new(&g,42,7,19)) ++failures;
  if(snapshot_is_new(&g,43,7,19) || snapshot_is_new(&g,42,8,19)) ++failures;
  g.SnapshotGeneration=0;
  if(snapshot_is_new(&g,42,7,0)) ++failures;
  g.SnapshotGeneration=1;
  if(!snapshot_is_new(&g,42,7,0)) ++failures;
  if(snapshot_is_new(NULL,42,7,0)) ++failures;
  printf("NATIVE_KMT_FRESHNESS: failures=%u (same-command stale/equal snapshots rejected)\n",failures);
  return failures;
}
#endif
static int wait_output(NATIVE_QUALIFICATION *q) {
  ULONGLONG start=GetTickCount64();
  for(;;) {
    DWORD bytes=sizeof(q->Graph),type=0;
    ZeroMemory(&q->Graph,sizeof(q->Graph));
    LSTATUS status=RegGetValueW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Services\\AppleAgxAdmission",
        L"Wom1NativeGraphReceipt",RRF_RT_REG_BINARY,&type,&q->Graph,&bytes);
    if(status==ERROR_SUCCESS && type==REG_BINARY && bytes==sizeof(q->Graph) &&
        q->Graph.Version==1 && q->Graph.Bytes==sizeof(q->Graph) && q->Graph.Valid==1 &&
        q->Graph.CommandHash==q->Callback.CommandHash &&
        q->Graph.Generation==q->Scene.Receipt.TargetIdentity.Generation &&
        snapshot_is_new(&q->Graph,q->ExpectedCandidateBuild,q->Scene.Receipt.BootGeneration,
            q->PriorSnapshotGeneration) &&
        q->Graph.ReadbackAvailable==1 && q->Graph.Fence!=0) {
      const APPLE_AGX_WIN32_NATIVE_BATCH_METADATA *roots=&q->Scene.Receipt.NativeRoots;
      if(q->Graph.GraphObjectCount+1!=q->Scene.Receipt.References ||
          q->Graph.GraphEdgeCount!=q->Scene.Receipt.Relocations ||
          q->Graph.RenderTargetBytes!=q->Scene.Receipt.TargetBytes ||
          q->Graph.BackgroundReference!=roots->Background.UscReference ||
          q->Graph.PartialBackgroundReference!=roots->PartialBackground.UscReference ||
          q->Graph.EndOfTileReference!=roots->EndOfTile.UscReference ||
          q->Graph.BackgroundCounts!=roots->Background.PackedCounts ||
          q->Graph.PartialBackgroundCounts!=roots->PartialBackground.PackedCounts ||
          q->Graph.EndOfTileCounts!=roots->EndOfTile.PackedCounts ||
          q->Graph.BackgroundFlags!=roots->Background.UscFlags ||
          q->Graph.PartialBackgroundFlags!=roots->PartialBackground.UscFlags ||
          q->Graph.EndOfTileFlags!=roots->EndOfTile.UscFlags) return 0;
      fprintf(stderr,"NATIVE_KMT_OUTPUT: build=%u boot=%u generation=%u kernel_fence=%u snapshot=%llu command=%016llx bytes=%u\n",
          q->Graph.CandidateBuild,q->Graph.BootGeneration,q->Graph.Generation,q->Graph.Fence,
          q->Graph.SnapshotGeneration,q->Graph.CommandHash,q->Graph.ReadbackBytes);
      if(!dump_receipt(q)) return 0;
      return verify_output(q)?1:-1;
    }
    if(GetTickCount64()-start>=NATIVE_WAIT_MS) {
      fprintf(stderr,"NATIVE_KMT_OUTPUT_PENDING: registry=%ld command=%016llx\n",status,q->Callback.CommandHash);
      return 0;
    }
    Sleep(10);
  }
}
int AgxKmtNativeQualificationRun(D3DKMT_HANDLE adapter,D3DKMT_HANDLE device,
    D3DKMT_HANDLE paging,volatile const UINT64 *pagingFence,UINT expectedBuild) {
  if(NativePending || !expectedBuild) {
    fprintf(stderr,"NATIVE_KMT_REJECT: exact preregistered candidate build and one request required\n");
    return NativePending?2:1;
  }
  NATIVE_QUALIFICATION *q=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*q));
  if(!q) return 1;
  q->ExpectedCandidateBuild=expectedBuild;
  NativePending=q;
  int result=1;
  HRESULT hr=AgxKmtNativeBridgeCreate(adapter,device,paging,pagingFence,NATIVE_WAIT_MS,&q->Bridge);
  if(FAILED(hr) || FAILED(AgxKmtNativeBridgeGetBinding(q->Bridge,&q->Binding))) goto cleanup;
  if(!capture_output_baseline(q)) goto cleanup;
  q->Screen=AgxWin32AsahiScreenCreateForWindows(q->Binding.Backend,q->Binding.Windows,
      q->Binding.OwnerOperations,q->Binding.Owner,q->Binding.BatchOperations);
  if(!q->Screen) goto cleanup;
  q->Context=AgxWin32AsahiContextCreate(q->Screen,q->Binding.Owner);
  if(!q->Context || !AgxWin32AsahiSceneInit(&q->Scene,q->Screen,q->Context) ||
      !AgxWin32AsahiSceneDraw(&q->Scene)) goto cleanup;
  int submitted=AgxWin32AsahiSceneSubmit(&q->Scene);
  (void)AgxKmtNativeBridgeGetReceipt(q->Bridge,&q->Callback);
  fprintf(stderr,"NATIVE_KMT_SCENE: submitted=%u status=0x%08x request=%llu refs=%u edges=%u encoder=%llu target=%llu command=%016llx\n",
      submitted,(unsigned)q->Scene.SubmitStatus,q->Scene.Receipt.Request,
      q->Scene.Receipt.References,q->Scene.Receipt.Relocations,q->Scene.Receipt.EncoderBytes,
      q->Scene.Receipt.TargetBytes,q->Callback.CommandHash);
  if(q->Scene.Phase==AgxAsahiSceneSubmitted && !AgxWin32AsahiSceneRetire(&q->Scene,NATIVE_WAIT_MS)) goto retain;
  if(!submitted || q->Scene.Phase!=AgxAsahiSceneRetired ||
      q->Callback.Renders!=1 || q->Callback.Signals!=1) goto cleanup;
  int output=wait_output(q);
  if(output==0) goto retain;
  if(output<0) goto cleanup;
  result=0;
cleanup:
  if(q->Scene.Phase!=AgxAsahiSceneEmpty && q->Scene.Phase!=AgxAsahiSceneReleased &&
      !AgxWin32AsahiSceneCleanup(&q->Scene,NATIVE_WAIT_MS)) goto retain;
  if(q->Context) {
    if(!AgxWin32AsahiContextDestroy(q->Context)) goto retain;
    q->Context=NULL;
  }
  if(q->Screen) {
    ULONGLONG start=GetTickCount64();
    while(!AgxWin32AsahiScreenDestroy(q->Screen)) {
      if(GetTickCount64()-start>=NATIVE_WAIT_MS) goto retain;
      Sleep(10);
    }
    q->Screen=NULL;
  }
  if(q->Bridge) {
    (void)AgxKmtNativeBridgeGetReceipt(q->Bridge,&q->Callback);
    if(q->Callback.Allocations!=q->Callback.Deallocations || q->Callback.Locks!=q->Callback.Unlocks) result=1;
    if(FAILED(AgxKmtNativeBridgeClose(&q->Bridge))) goto retain;
  }
  fprintf(stderr,"NATIVE_KMT_RESULT: result=%u allocations=%u deallocations=%u locks=%u unlocks=%u\n",
      result,q->Callback.Allocations,q->Callback.Deallocations,q->Callback.Locks,q->Callback.Unlocks);
  HeapFree(GetProcessHeap(),0,q);NativePending=NULL;return result;
retain:
  fprintf(stderr,"NATIVE_KMT_PRESERVE: phase=%u submit=0x%08x retire=0x%08x resources retained for evidence/recovery\n",
      (unsigned)q->Scene.Phase,(unsigned)q->Scene.SubmitStatus,(unsigned)q->Scene.RetireStatus);
  return 2;
}
