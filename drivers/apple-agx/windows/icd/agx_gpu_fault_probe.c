/* RC37: opt-in real GPU fault/recovery checkpoint, no benchmark required.
 * Build (WDK/SDK developer prompt):
 *   cl /nologo /W4 /O2 agx_gpu_fault_probe.c /link d3d11.lib dxgi.lib gdi32.lib dwmapi.lib
 * Run: agx_gpu_fault_probe.exe <log.txt>
 * Requires Wom1GpuFaultDrillAllowed=1 and Wom1GpuFaultInfoRead=1 on the devnode.
 * The KMD replaces this process's next validated compute CDM fetch with a
 * proven unmapped VA, consumes the arm before publication, and records the
 * matching hardware FAULT_INFO. This is a command-fetch READ, not a shader
 * data-load or a software-only device-removal test. Nothing runs by default
 * in the normal probe suite. Use only with the preregistered recovery artifact.
 */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <d3dkmthk.h>
#include <dwmapi.h>
#include <tlhelp32.h>
#include <wtsapi32.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "../../render-admission/include/render_gpu_fault.h"

static HANDLE log_file = INVALID_HANDLE_VALUE;
static void say(const char *format, ...) {
  char line[1024]; va_list ap; int n; DWORD written;
  va_start(ap, format); n = vsnprintf(line, sizeof(line)-3, format, ap); va_end(ap);
  if (n < 0) return;
  if (n > (int)sizeof(line)-3) n = (int)sizeof(line)-3;
  line[n++]='\r'; line[n++]='\n';
  fwrite(line, 1, (size_t)n, stdout); fflush(stdout);
  if (log_file != INVALID_HANDLE_VALUE) {
    WriteFile(log_file, line, (DWORD)n, &written, NULL); FlushFileBuffers(log_file);
  }
}

static HRESULT open_device(ID3D11Device **device, ID3D11DeviceContext **ctx, LUID *luid) {
  IDXGIFactory1 *factory=NULL; IDXGIAdapter1 *adapter=NULL;
  HRESULT hr=CreateDXGIFactory1(&IID_IDXGIFactory1,(void **)&factory);
  D3D_FEATURE_LEVEL requested=D3D_FEATURE_LEVEL_11_0, level;
  if (FAILED(hr)) return hr;
  for (UINT i=0; ;++i) {
    DXGI_ADAPTER_DESC1 desc;
    hr=IDXGIFactory1_EnumAdapters1(factory,i,&adapter);
    if (FAILED(hr)) break;
    hr=IDXGIAdapter1_GetDesc1(adapter,&desc);
    if (SUCCEEDED(hr) && (desc.VendorId==0x106b || desc.VendorId==0x4c505041) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
      *luid=desc.AdapterLuid;
      hr=D3D11CreateDevice((IDXGIAdapter *)adapter,D3D_DRIVER_TYPE_UNKNOWN,NULL,0,
          &requested,1,D3D11_SDK_VERSION,device,&level,ctx);
      IDXGIAdapter1_Release(adapter); break;
    }
    IDXGIAdapter1_Release(adapter);
  }
  IDXGIFactory1_Release(factory); return hr;
}

static HRESULT shader(const char *text, const char *target, ID3DBlob **blob) {
  HMODULE lib=LoadLibraryA("d3dcompiler_47.dll"); ID3DBlob *errors=NULL;
  pD3DCompile compile=lib ? (pD3DCompile)(void *)GetProcAddress(lib,"D3DCompile") : NULL;
  HRESULT hr=compile ? compile(text,strlen(text),"rc37",NULL,NULL,"main",target,
      D3DCOMPILE_ENABLE_STRICTNESS,0,blob,&errors) : E_FAIL;
  if (errors) { say("SHADER %s",(const char *)ID3D10Blob_GetBufferPointer(errors)); ID3D10Blob_Release(errors); }
  /* Blobs may retain code from the DLL; keep it loaded until process exit. */
  return hr;
}

static HRESULT wait_query(ID3D11Device *d, ID3D11DeviceContext *ctx, ID3D11Query *query) {
  ULONGLONG start=GetTickCount64();
  for (;;) {
    BOOL done=FALSE;
    HRESULT hr=ID3D11Device_GetDeviceRemovedReason(d);
    if (FAILED(hr)) return hr;
    hr=ID3D11DeviceContext_GetData(ctx,(ID3D11Asynchronous *)query,&done,sizeof(done),0);
    if (FAILED(hr) || (hr==S_OK && done)) return hr;
    if (GetTickCount64()-start>=15000) return HRESULT_FROM_WIN32(WAIT_TIMEOUT);
    Sleep(5);
  }
}

static NTSTATUS drill(LUID luid, ADMISSION_GPU_FAULT_DRILL *data, UINT operation) {
  D3DKMT_OPENADAPTERFROMLUID open={0}; D3DKMT_CLOSEADAPTER close={0};
  D3DKMT_ESCAPE escape={0}; NTSTATUS status;
  memset(data,0,sizeof(*data));
  open.AdapterLuid=luid; status=D3DKMTOpenAdapterFromLuid(&open);
  if (status<0) return status;
  data->Magic=ADMISSION_GPU_FAULT_DRILL_MAGIC;
  data->Version=1; data->Bytes=sizeof(*data); data->Operation=operation;
  escape.hAdapter=open.hAdapter; escape.Type=D3DKMT_ESCAPE_DRIVERPRIVATE;
  escape.pPrivateDriverData=data; escape.PrivateDriverDataSize=sizeof(*data);
  status=D3DKMTEscape(&escape);
  close.hAdapter=open.hAdapter; D3DKMTCloseAdapter(&close); return status;
}

static int fault_child(void) {
  ID3D11Device *d=NULL; ID3D11DeviceContext *ctx=NULL; LUID luid={0};
  ID3D11Buffer *buffer=NULL; ID3D11UnorderedAccessView *uav=NULL;
  ID3D11ComputeShader *cs=NULL; ID3DBlob *blob=NULL; ID3D11Query *query=NULL;
  D3D11_BUFFER_DESC bd={0}; D3D11_UNORDERED_ACCESS_VIEW_DESC ud={0};
  D3D11_QUERY_DESC qd={D3D11_QUERY_EVENT,0}; ADMISSION_GPU_FAULT_DRILL receipt;
  NTSTATUS status; HRESULT hr=open_device(&d,&ctx,&luid), removed;
  bd.ByteWidth=16; bd.Usage=D3D11_USAGE_DEFAULT; bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
  bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
  if (SUCCEEDED(hr)) hr=ID3D11Device_CreateBuffer(d,&bd,NULL,&buffer);
  ud.Format=DXGI_FORMAT_R32_TYPELESS; ud.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;
  ud.Buffer.NumElements=4; ud.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
  if (SUCCEEDED(hr)) hr=ID3D11Device_CreateUnorderedAccessView(d,(ID3D11Resource *)buffer,&ud,&uav);
  if (SUCCEEDED(hr)) hr=shader("RWByteAddressBuffer dst:register(u0); [numthreads(1,1,1)] "
      "void main(){dst.Store(0,0x12345678);}","cs_5_0",&blob);
  if (SUCCEEDED(hr)) hr=ID3D11Device_CreateComputeShader(d,ID3D10Blob_GetBufferPointer(blob),
      ID3D10Blob_GetBufferSize(blob),NULL,&cs);
  if (SUCCEEDED(hr)) hr=ID3D11Device_CreateQuery(d,&qd,&query);
  say("FAULT_CHILD pid=%lu setup=%08lx",GetCurrentProcessId(),(unsigned long)hr);
  if (FAILED(hr)) return 1;
  ID3D11DeviceContext_CSSetShader(ctx,cs,NULL,0);
  ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx,0,1,&uav,NULL);
  ID3D11DeviceContext_Dispatch(ctx,1,1,1);
  ID3D11DeviceContext_End(ctx,(ID3D11Asynchronous *)query); ID3D11DeviceContext_Flush(ctx);
  hr=wait_query(d,ctx,query); say("WARMUP %08lx",(unsigned long)hr);
  if (FAILED(hr)) return 1;
  status=drill(luid,&receipt,1); say("ARM status=%08lx armed=%u",(unsigned long)status,receipt.Armed);
  if (status<0 || receipt.Armed!=1) return 1;
  ID3D11DeviceContext_Dispatch(ctx,1,1,1);
  ID3D11DeviceContext_End(ctx,(ID3D11Asynchronous *)query); ID3D11DeviceContext_Flush(ctx);
  hr=wait_query(d,ctx,query); removed=ID3D11Device_GetDeviceRemovedReason(d);
  status=drill(luid,&receipt,0);
  say("FAULT_RESULT wait=%08lx removed=%08lx query=%08lx armed=%u fence=%x va=%llx fault=%llx",
      (unsigned long)hr,(unsigned long)removed,(unsigned long)status,receipt.Armed,
      receipt.Fence,receipt.GpuVa,receipt.FaultInfo);
  /* A software rejection, a null tile read or a timeout alone is NOT a pass. */
  if (status<0 || !FAILED(removed) || receipt.Armed || !receipt.Fence || !receipt.GpuVa ||
      !(receipt.FaultInfo&1) || !(receipt.FaultInfo&0x10) ||
      ((receipt.FaultInfo>>30)<<6)!=receipt.GpuVa) return 1;
  say("AGX_GPU_FAULT_INJECTED: PASS"); return 0;
}

static int render_child(void) {
  ID3D11Device *d=NULL; ID3D11DeviceContext *ctx=NULL; LUID luid={0};
  ID3D11Texture2D *target=NULL,*readback=NULL; ID3D11RenderTargetView *rtv=NULL;
  ID3D11VertexShader *vs=NULL; ID3D11PixelShader *ps=NULL;
  ID3D11RasterizerState *raster=NULL; ID3DBlob *vsb=NULL,*psb=NULL; ID3D11Query *query=NULL;
  D3D11_TEXTURE2D_DESC td={0}; D3D11_RASTERIZER_DESC rd={0};
  D3D11_VIEWPORT vp={0,0,64,64,0,1}; D3D11_QUERY_DESC qd={D3D11_QUERY_EVENT,0};
  D3D11_MAPPED_SUBRESOURCE map={0}; UINT pixel=0; BOOL composition=FALSE;
  DWM_TIMING_INFO timing={0}; HRESULT hr=open_device(&d,&ctx,&luid);
  td.Width=td.Height=64; td.MipLevels=td.ArraySize=1; td.SampleDesc.Count=1;
  td.Format=DXGI_FORMAT_R8G8B8A8_UNORM; td.Usage=D3D11_USAGE_DEFAULT; td.BindFlags=D3D11_BIND_RENDER_TARGET;
  if (SUCCEEDED(hr)) hr=ID3D11Device_CreateTexture2D(d,&td,NULL,&target);
  if (SUCCEEDED(hr)) hr=ID3D11Device_CreateRenderTargetView(d,(ID3D11Resource *)target,NULL,&rtv);
  td.Usage=D3D11_USAGE_STAGING; td.BindFlags=0; td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  if (SUCCEEDED(hr)) hr=ID3D11Device_CreateTexture2D(d,&td,NULL,&readback);
  if (SUCCEEDED(hr)) hr=shader("float4 main(uint id:SV_VertexID):SV_Position{"
      "float2 uv=float2((id<<1)&2,id&2);return float4(uv*float2(2,-2)+float2(-1,1),0,1);}","vs_5_0",&vsb);
  if (SUCCEEDED(hr)) hr=shader("float4 main():SV_Target{return float4(1,0,1,1);}","ps_5_0",&psb);
  if (SUCCEEDED(hr)) hr=ID3D11Device_CreateVertexShader(d,ID3D10Blob_GetBufferPointer(vsb),ID3D10Blob_GetBufferSize(vsb),NULL,&vs);
  if (SUCCEEDED(hr)) hr=ID3D11Device_CreatePixelShader(d,ID3D10Blob_GetBufferPointer(psb),ID3D10Blob_GetBufferSize(psb),NULL,&ps);
  rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_NONE; rd.DepthClipEnable=TRUE;
  if (SUCCEEDED(hr)) hr=ID3D11Device_CreateRasterizerState(d,&rd,&raster);
  if (SUCCEEDED(hr)) hr=ID3D11Device_CreateQuery(d,&qd,&query);
  if (FAILED(hr)) { say("RENDER_SETUP %08lx",(unsigned long)hr); return 1; }
  ID3D11DeviceContext_OMSetRenderTargets(ctx,1,&rtv,NULL);
  ID3D11DeviceContext_RSSetState(ctx,raster); ID3D11DeviceContext_RSSetViewports(ctx,1,&vp);
  ID3D11DeviceContext_IASetPrimitiveTopology(ctx,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ID3D11DeviceContext_VSSetShader(ctx,vs,NULL,0); ID3D11DeviceContext_PSSetShader(ctx,ps,NULL,0);
  ID3D11DeviceContext_Draw(ctx,3,0);
  ID3D11DeviceContext_CopyResource(ctx,(ID3D11Resource *)readback,(ID3D11Resource *)target);
  ID3D11DeviceContext_End(ctx,(ID3D11Asynchronous *)query); ID3D11DeviceContext_Flush(ctx);
  hr=wait_query(d,ctx,query);
  if (SUCCEEDED(hr)) hr=ID3D11DeviceContext_Map(ctx,(ID3D11Resource *)readback,0,D3D11_MAP_READ,0,&map);
  if (SUCCEEDED(hr)) {
    memcpy(&pixel,(const char *)map.pData+32*map.RowPitch+32*4,4);
    ID3D11DeviceContext_Unmap(ctx,(ID3D11Resource *)readback,0);
  }
  say("RENDER pid=%lu hr=%08lx pixel=%08x removed=%08lx",GetCurrentProcessId(),
      (unsigned long)hr,pixel,(unsigned long)ID3D11Device_GetDeviceRemovedReason(d));
  if (FAILED(hr) || pixel!=0xffff00ffu || FAILED(ID3D11Device_GetDeviceRemovedReason(d))) return 1;
  timing.cbSize=sizeof(timing);
  if (FAILED(DwmIsCompositionEnabled(&composition)) || !composition ||
      FAILED(DwmFlush()) || FAILED(DwmGetCompositionTimingInfo(NULL,&timing))) return 1;
  say("AGX_GPU_FAULT_FRESH_RENDER: PASS dwm_frame=%llu",timing.cFrame); return 0;
}

/* EXP2003: a non-elevated tool cannot open dwm.exe (Window Manager\DWM-n) to
 * query its session, so the session comes from the WTS process list, which
 * needs no process handle. */
static DWORD dwm_pid(void) {
  WTS_PROCESS_INFOW *list=NULL; DWORD count=0, session=0, result=0;
  if (!ProcessIdToSessionId(GetCurrentProcessId(),&session)) return 0;
  if (!WTSEnumerateProcessesW(WTS_CURRENT_SERVER_HANDLE,0,1,&list,&count)) return 0;
  for (DWORD i=0;i<count;++i)
    if (list[i].pProcessName && !_wcsicmp(list[i].pProcessName,L"dwm.exe") && list[i].SessionId==session) {
      result=list[i].ProcessId; break;
    }
  WTSFreeMemory(list); return result;
}

static int child(const char *log_path, const char *mode) {
  static unsigned sequence;
  char exe[MAX_PATH], command[3*MAX_PATH]; STARTUPINFOA si={0}; PROCESS_INFORMATION pi={0};
  DWORD code=1, length=GetModuleFileNameA(NULL,exe,MAX_PATH); int n;
  if (!length || length>=MAX_PATH || strchr(log_path,'"')) return 1;
  n=snprintf(command,sizeof(command),"\"%s\" \"%s.%u.%s.txt\" %s",exe,log_path,++sequence,mode,mode);
  if (n<0 || n>=(int)sizeof(command)) return 1;
  si.cb=sizeof(si);
  if (!CreateProcessA(exe,command,NULL,NULL,FALSE,0,NULL,NULL,&si,&pi)) return 1;
  CloseHandle(pi.hThread);
  if (WaitForSingleObject(pi.hProcess,25000)!=WAIT_OBJECT_0) {
    say("CHILD_TIMEOUT %s pid=%lu",mode,pi.dwProcessId);
    TerminateProcess(pi.hProcess,2); WaitForSingleObject(pi.hProcess,2000);
  } else GetExitCodeProcess(pi.hProcess,&code);
  CloseHandle(pi.hProcess); say("CHILD %s exit=%lu",mode,code); return code ? 1 : 0;
}

int main(int argc, char **argv) {
  DWORD before; HANDLE dwm; int result;
  if (argc<2 || argc>3) { puts("usage: agx_gpu_fault_probe.exe <log.txt> [fault|render]"); return 2; }
  log_file=CreateFileA(argv[1],GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,NULL);
  if (log_file==INVALID_HANDLE_VALUE) return 2;
  if (argc==3) {
    if (!strcmp(argv[2],"fault")) return fault_child();
    if (!strcmp(argv[2],"render")) return render_child();
    return 2;
  }
  before=dwm_pid(); dwm=before ? OpenProcess(SYNCHRONIZE,FALSE,before) : NULL;
  say("RC37_START pid=%lu dwm=%lu",GetCurrentProcessId(),before);
  /* Without SYNCHRONIZE access, DWM survival is judged by its PID alone. */
  if (!before) { say("DWM_PROCESS_UNAVAILABLE"); return 1; }
  if (!dwm) say("DWM_HANDLE_UNAVAILABLE err=%lu pid-only", GetLastError());
  result=child(argv[1],"render");
  if (!result) result=child(argv[1],"fault");
  if (!result) result=child(argv[1],"render");
  if ((dwm && WaitForSingleObject(dwm,0)!=WAIT_TIMEOUT) || dwm_pid()!=before) result=1;
  if (dwm) CloseHandle(dwm);
  say("AGX_GPU_FAULT_RECOVERY: %s",result ? "FAIL" : "PASS"); return result;
}
