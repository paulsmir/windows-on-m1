/* EXP1317 discriminator: does a pixel shader that spills its indexable
 * temporary to scratch run at all? dwmcore.dll_003d8800 (x0[96]) timed out
 * the GPU on both shader paths with and without the SGX core topology. A
 * dynamically indexed array of N float4 is a dcl_indexableTemp; Asahi keeps
 * arrays up to 256 bytes in registers (csel) and lowers larger ones to
 * scratch (agx_compile.c nir_lower_vars_to_scratch). Each case fills the
 * array in a loop and returns element (x + sel) % N; red holds the index.
 * Cases run smallest first so a hang names the first failing size.
 * EXP1338 "vs": the array lives in the vertex shader instead (vertex-stage
 * scratch, DRM_ASAHI_RENDER_VERTEX_SCRATCH); the provoking vertex (id 0)
 * passes element sel % N flat to the pixel shader, so every pixel holds it.
 * EXP1339 "cs": the array lives in a compute shader (cs_4_0, compute
 * helper scratch, drm_asahi_cmd_compute.helper); two groups of 64 threads
 * store element (id + sel) % N of a uint4 array {k, 7} as k | 7 << 12 to a
 * raw UAV that is copied back and compared word by word.
 * Usage: agx_scratch_smoke.exe <out.txt> [nir] [vs] [cs] */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define REL(p) do { if (p) IUnknown_Release((IUnknown *)(p)); } while (0)
static HANDLE output = INVALID_HANDLE_VALUE;
static volatile LONG started;
static void say(const char *fmt, ...) {
  char line[512]; va_list args; DWORD written; int n;
  va_start(args, fmt); n = vsnprintf(line, sizeof(line)-3, fmt, args); va_end(args);
  if (n < 0) return; if (n > (int)sizeof(line)-3) n = sizeof(line)-3;
  line[n++]='\r'; line[n++]='\n';
  fwrite(line,1,n,stdout); fflush(stdout);
  if (output != INVALID_HANDLE_VALUE) { WriteFile(output,line,n,&written,NULL); FlushFileBuffers(output); }
}
static DWORD WINAPI watchdog(void *unused) {
  (void)unused;
  for (;;) { Sleep(1000); if (started && (DWORD)(GetTickCount()-(DWORD)started)>20000) {
    say("WATCHDOG scratch smoke"); TerminateProcess(GetCurrentProcess(),1);
  } }
}
static const char vs_hlsl[] =
  "float4 main(uint id : SV_VertexID) : SV_Position {\n"
  "  float2 p = float2((id & 1) ? 3.0 : -1.0, (id & 2) ? -3.0 : 1.0);\n"
  "  return float4(p, 0, 1); }\n";
static int vertex_mode, compute_mode;
/* N is substituted per case. */
static const char cs_template[] =
  "cbuffer c : register(b0) { uint4 sel; };\n"
  "RWByteAddressBuffer dst : register(u0);\n"
  "[numthreads(64, 1, 1)]\n"
  "void main(uint3 t : SV_DispatchThreadID) {\n"
  "  uint4 a[%u];\n"
  "  [loop] for (uint k = 0; k < %uu; ++k) a[k] = uint4(k, 7, 0, 0);\n"
  "  uint4 v = a[(t.x + sel.x) %% %uu]; dst.Store(t.x * 4, v.x | (v.y << 12)); }\n";
/* N is substituted per case. */
static const char vs_template[] =
  "cbuffer c : register(b0) { uint4 sel; };\n"
  "struct O { float4 p : SV_Position; nointerpolation float4 c : COLOR; };\n"
  "O main(uint id : SV_VertexID) {\n"
  "  O o; float2 p = float2((id & 1) ? 3.0 : -1.0, (id & 2) ? -3.0 : 1.0);\n"
  "  o.p = float4(p, 0, 1); float4 a[%u];\n"
  "  [loop] for (uint k = 0; k < %uu; ++k) a[k] = float4(k / 255.0, 0.5, 0, 1);\n"
  "  o.c = a[(id + sel.x) %% %uu]; return o; }\n";
static const char ps_flat[] =
  "struct O { float4 p : SV_Position; nointerpolation float4 c : COLOR; };\n"
  "float4 main(O i) : SV_Target { return i.c; }\n";
/* N is substituted per case. */
static const char ps_template[] =
  "cbuffer c : register(b0) { uint4 sel; };\n"
  "float4 main(float4 p : SV_Position) : SV_Target {\n"
  "  float4 a[%u];\n"
  "  [loop] for (uint k = 0; k < %uu; ++k) a[k] = float4(k / 255.0, 0.5, 0, 1);\n"
  "  return a[(uint(p.x) + sel.x) %% %uu]; }\n";
static ID3DBlob *compile(pD3DCompile fn,const char *src,const char *target) {
  ID3DBlob *code=NULL,*error=NULL; HRESULT hr=fn(src,strlen(src),NULL,NULL,NULL,"main",target,0,0,&code,&error);
  if(error) { say("COMPILE %s %.300s",target,(const char *)ID3D10Blob_GetBufferPointer(error)); REL(error); }
  if(FAILED(hr)) { REL(code); return NULL; } return code;
}
/* The dcl_indexableTemp register count of the program (0: the compiler
 * folded the array away and the case measures nothing). */
static UINT indexable_temp_entries(ID3DBlob *code) {
  const BYTE *b=(const BYTE *)ID3D10Blob_GetBufferPointer(code);
  SIZE_T bytes=ID3D10Blob_GetBufferSize(code); UINT chunks,c;
  if(bytes<32) return 0; memcpy(&chunks,b+28,4);
  for(c=0;c<chunks && 32+4*(SIZE_T)c+4<=bytes;++c) {
    UINT off,len,i; memcpy(&off,b+32+4*c,4);
    if(off+16>bytes || (memcmp(b+off,"SHDR",4) && memcmp(b+off,"SHEX",4))) continue;
    memcpy(&len,b+off+12,4);
    for(i=2;i<len && off+8+4*(SIZE_T)(i+3)<=bytes;) {
      UINT t,n; memcpy(&t,b+off+8+4*i,4); n=(t>>24)&0x7f;
      if((t&0x7ff)==0x35) memcpy(&n,b+off+8+4*(i+1),4);
      if((t&0x7ff)==0x69) { UINT e; memcpy(&e,b+off+8+4*(i+2),4); return e; }
      if(!n) break; i+=n;
    }
  }
  return 0;
}
static UINT pixel(const D3D11_MAPPED_SUBRESOURCE *m,UINT x,UINT y) {
  const BYTE *p=(const BYTE *)m->pData+y*m->RowPitch+x*4;
  return (UINT)p[0] | ((UINT)p[1]<<8) | ((UINT)p[2]<<16) | ((UINT)p[3]<<24);
}
/* EXP1339: the compute case. Returns 1 pass, 0 wrong words, -1 device
 * lost or timeout. */
static int run_cs(IDXGIAdapter1 *adapter,pD3DCompile fn,UINT n) {
  const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};
  const UINT sel[4]={3,0,0,0}; enum { threads=128 };
  char cs_hlsl[640];
  ID3D11Device *dev=NULL; ID3D11DeviceContext *ctx=NULL; ID3D11ComputeShader *cs=NULL;
  ID3D11Buffer *cb=NULL,*dst=NULL,*readback=NULL; ID3D11UnorderedAccessView *uav=NULL;
  ID3D11Query *query=NULL; ID3DBlob *code=NULL;
  D3D11_BUFFER_DESC bd; D3D11_SUBRESOURCE_DATA init; D3D11_UNORDERED_ACCESS_VIEW_DESC ud;
  D3D11_QUERY_DESC qd; D3D11_MAPPED_SUBRESOURCE mapped; D3D_FEATURE_LEVEL got;
  HRESULT hr=S_OK; UINT bad=0,first_x=0,first=0,expect_first=0,itemp=0; int result=-1; DWORD t0;
  snprintf(cs_hlsl,sizeof(cs_hlsl),cs_template,n,n,n);
  started=(LONG)GetTickCount();
  hr=D3D11CreateDevice((IDXGIAdapter *)adapter,D3D_DRIVER_TYPE_UNKNOWN,NULL,0,levels,3,
                       D3D11_SDK_VERSION,&dev,&got,&ctx); if(FAILED(hr)) goto done;
  code=compile(fn,cs_hlsl,"cs_4_0"); if(!code) goto done;
  itemp=indexable_temp_entries(code);
  hr=ID3D11Device_CreateComputeShader(dev,ID3D10Blob_GetBufferPointer(code),ID3D10Blob_GetBufferSize(code),NULL,&cs);
  if(FAILED(hr)) goto done;
  memset(&bd,0,sizeof(bd)); bd.ByteWidth=16; bd.Usage=D3D11_USAGE_DEFAULT; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
  memset(&init,0,sizeof(init)); init.pSysMem=sel;
  hr=ID3D11Device_CreateBuffer(dev,&bd,&init,&cb); if(FAILED(hr)) goto done;
  memset(&bd,0,sizeof(bd)); bd.ByteWidth=threads*4; bd.Usage=D3D11_USAGE_DEFAULT;
  bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS; bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
  hr=ID3D11Device_CreateBuffer(dev,&bd,NULL,&dst); if(FAILED(hr)) goto done;
  memset(&bd,0,sizeof(bd)); bd.ByteWidth=threads*4; bd.Usage=D3D11_USAGE_STAGING;
  bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  hr=ID3D11Device_CreateBuffer(dev,&bd,NULL,&readback); if(FAILED(hr)) goto done;
  memset(&ud,0,sizeof(ud)); ud.Format=DXGI_FORMAT_R32_TYPELESS; ud.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;
  ud.Buffer.NumElements=threads; ud.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
  hr=ID3D11Device_CreateUnorderedAccessView(dev,(ID3D11Resource *)dst,&ud,&uav); if(FAILED(hr)) goto done;
  ID3D11DeviceContext_CSSetShader(ctx,cs,NULL,0);
  ID3D11DeviceContext_CSSetConstantBuffers(ctx,0,1,&cb);
  ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx,0,1,&uav,NULL);
  ID3D11DeviceContext_Dispatch(ctx,threads/64,1,1);
  memset(&qd,0,sizeof(qd)); qd.Query=D3D11_QUERY_EVENT;
  hr=ID3D11Device_CreateQuery(dev,&qd,&query); if(FAILED(hr)) goto done;
  ID3D11DeviceContext_End(ctx,(ID3D11Asynchronous *)query);
  ID3D11DeviceContext_Flush(ctx);
  t0=GetTickCount();
  for (;;) { BOOL complete=FALSE;
    hr=ID3D11DeviceContext_GetData(ctx,(ID3D11Asynchronous *)query,&complete,sizeof(complete),0);
    if(FAILED(hr)) goto done; if(hr==S_OK && complete) break;
    if((DWORD)(GetTickCount()-t0)>8000) { hr=HRESULT_FROM_WIN32(ERROR_TIMEOUT); goto done; } Sleep(1);
  }
  ID3D11DeviceContext_CopyResource(ctx,(ID3D11Resource *)readback,(ID3D11Resource *)dst);
  hr=ID3D11DeviceContext_Map(ctx,(ID3D11Resource *)readback,0,D3D11_MAP_READ,0,&mapped);
  if(FAILED(hr)) goto done;
  for (UINT x=0;x<threads;++x) {
    UINT expect=((x+sel[0])%n) | (7u<<12), got_w=((const UINT *)mapped.pData)[x];
    if (x==0) { first=got_w; expect_first=expect; }
    if (got_w!=expect && !bad++) first_x=x;
  }
  ID3D11DeviceContext_Unmap(ctx,(ID3D11Resource *)readback,0);
  result = bad ? 0 : 1;
done:
  say("SCRATCH-CS n=%u bytes=%u itemp=%u w0=0x%08x expect0=0x%08x bad=%u first_bad_x=%u removed=0x%08lx hr=0x%08lx verdict=%s",
      n,n*16u,itemp,first,expect_first,bad,first_x,
      dev?(unsigned long)ID3D11Device_GetDeviceRemovedReason(dev):0ul,(unsigned long)hr,
      result>0?"PASS":result==0?"WRONG":"LOST");
  started=0; REL(query); REL(uav); REL(readback); REL(dst); REL(cb); REL(cs); REL(code);
  REL(ctx); REL(dev);
  return result;
}
/* Returns 1 pass, 0 wrong pixels, -1 device lost or timeout. */
static int run(IDXGIAdapter1 *adapter,pD3DCompile fn,UINT n) {
  const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};
  const float blue[4]={0,0,1,1}; const UINT sel[4]={3,0,0,0};
  char ps_hlsl[512], vs_hlsl_n[640];
  ID3D11Device *dev=NULL; ID3D11DeviceContext *ctx=NULL; ID3D11VertexShader *vs=NULL;
  ID3D11PixelShader *ps=NULL; ID3D11Texture2D *rt=NULL,*readback=NULL; ID3D11Buffer *cb=NULL;
  ID3D11RenderTargetView *rtv=NULL; ID3D11RasterizerState *rs=NULL;
  ID3D11Query *query=NULL; ID3DBlob *vscode=NULL,*pscode=NULL;
  D3D11_TEXTURE2D_DESC td; D3D11_RASTERIZER_DESC rd; D3D11_VIEWPORT vp; D3D11_BUFFER_DESC bd;
  D3D11_SUBRESOURCE_DATA init; D3D11_QUERY_DESC qd; D3D11_MAPPED_SUBRESOURCE mapped;
  D3D_FEATURE_LEVEL got; HRESULT hr=S_OK; UINT bad=0,first_x=0,first_px=0,expect_first=0,itemp=0;
  int result=-1; DWORD t0;
  snprintf(ps_hlsl,sizeof(ps_hlsl),ps_template,n,n,n);
  snprintf(vs_hlsl_n,sizeof(vs_hlsl_n),vs_template,n,n,n);
  started=(LONG)GetTickCount();
  hr=D3D11CreateDevice((IDXGIAdapter *)adapter,D3D_DRIVER_TYPE_UNKNOWN,NULL,0,levels,2,
                       D3D11_SDK_VERSION,&dev,&got,&ctx); if(FAILED(hr)) goto done;
  vscode=compile(fn,vertex_mode?vs_hlsl_n:vs_hlsl,"vs_4_0");
  pscode=compile(fn,vertex_mode?ps_flat:ps_hlsl,"ps_4_0");
  if(!vscode || !pscode) goto done;
  itemp=indexable_temp_entries(vertex_mode?vscode:pscode);
  hr=ID3D11Device_CreateVertexShader(dev,ID3D10Blob_GetBufferPointer(vscode),ID3D10Blob_GetBufferSize(vscode),NULL,&vs);
  if(FAILED(hr)) goto done;
  hr=ID3D11Device_CreatePixelShader(dev,ID3D10Blob_GetBufferPointer(pscode),ID3D10Blob_GetBufferSize(pscode),NULL,&ps);
  if(FAILED(hr)) goto done;
  memset(&bd,0,sizeof(bd)); bd.ByteWidth=16; bd.Usage=D3D11_USAGE_DEFAULT; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
  memset(&init,0,sizeof(init)); init.pSysMem=sel;
  hr=ID3D11Device_CreateBuffer(dev,&bd,&init,&cb); if(FAILED(hr)) goto done;
  memset(&td,0,sizeof(td)); td.Width=128; td.Height=8; td.MipLevels=td.ArraySize=1;
  td.Format=DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_DEFAULT;
  td.BindFlags=D3D11_BIND_RENDER_TARGET;
  hr=ID3D11Device_CreateTexture2D(dev,&td,NULL,&rt); if(FAILED(hr)) goto done;
  hr=ID3D11Device_CreateRenderTargetView(dev,(ID3D11Resource *)rt,NULL,&rtv); if(FAILED(hr)) goto done;
  td.BindFlags=0; td.Usage=D3D11_USAGE_STAGING; td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  hr=ID3D11Device_CreateTexture2D(dev,&td,NULL,&readback); if(FAILED(hr)) goto done;
  memset(&rd,0,sizeof(rd)); rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_NONE; rd.DepthClipEnable=TRUE;
  hr=ID3D11Device_CreateRasterizerState(dev,&rd,&rs); if(FAILED(hr)) goto done;
  memset(&vp,0,sizeof(vp)); vp.Width=128; vp.Height=8; vp.MaxDepth=1;
  ID3D11DeviceContext_ClearRenderTargetView(ctx,rtv,blue);
  ID3D11DeviceContext_OMSetRenderTargets(ctx,1,&rtv,NULL);
  ID3D11DeviceContext_RSSetState(ctx,rs); ID3D11DeviceContext_RSSetViewports(ctx,1,&vp);
  ID3D11DeviceContext_VSSetShader(ctx,vs,NULL,0); ID3D11DeviceContext_PSSetShader(ctx,ps,NULL,0);
  ID3D11DeviceContext_PSSetConstantBuffers(ctx,0,1,&cb);
  ID3D11DeviceContext_VSSetConstantBuffers(ctx,0,1,&cb);
  ID3D11DeviceContext_IASetPrimitiveTopology(ctx,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ID3D11DeviceContext_Draw(ctx,3,0);
  memset(&qd,0,sizeof(qd)); qd.Query=D3D11_QUERY_EVENT;
  hr=ID3D11Device_CreateQuery(dev,&qd,&query); if(FAILED(hr)) goto done;
  ID3D11DeviceContext_End(ctx,(ID3D11Asynchronous *)query);
  ID3D11DeviceContext_Flush(ctx);
  t0=GetTickCount();
  for (;;) { BOOL complete=FALSE;
    hr=ID3D11DeviceContext_GetData(ctx,(ID3D11Asynchronous *)query,&complete,sizeof(complete),0);
    if(FAILED(hr)) goto done; if(hr==S_OK && complete) break;
    if((DWORD)(GetTickCount()-t0)>8000) { hr=HRESULT_FROM_WIN32(ERROR_TIMEOUT); goto done; } Sleep(1);
  }
  ID3D11DeviceContext_CopyResource(ctx,(ID3D11Resource *)readback,(ID3D11Resource *)rt);
  hr=ID3D11DeviceContext_Map(ctx,(ID3D11Resource *)readback,0,D3D11_MAP_READ,0,&mapped);
  if(FAILED(hr)) goto done;
  for (UINT x=0;x<128;++x) {
    UINT idx=(vertex_mode?sel[0]:x+sel[0])%n, expect=0xff008000u | idx, got_px=pixel(&mapped,x,4);
    if (x==0) { first_px=got_px; expect_first=expect; }
    /* green 0.5 rounds to 0x7f or 0x80 */
    if ((got_px & 0xffff00ffu) != (expect & 0xffff00ffu) ||
        ((got_px >> 8) & 0xffu) < 0x7fu || ((got_px >> 8) & 0xffu) > 0x80u) {
      if (!bad++) first_x=x;
    }
  }
  ID3D11DeviceContext_Unmap(ctx,(ID3D11Resource *)readback,0);
  result = bad ? 0 : 1;
done:
  say("SCRATCH%s n=%u bytes=%u itemp=%u x0=0x%08x expect0=0x%08x bad=%u first_bad_x=%u removed=0x%08lx hr=0x%08lx verdict=%s",
      vertex_mode?"-VS":"",n,n*16u,itemp,first_px,expect_first,bad,first_x,
      dev?(unsigned long)ID3D11Device_GetDeviceRemovedReason(dev):0ul,(unsigned long)hr,
      result>0?"PASS":result==0?"WRONG":"LOST");
  started=0; REL(query); REL(rs); REL(readback); REL(rtv); REL(rt); REL(cb);
  REL(ps); REL(vs); REL(pscode); REL(vscode); REL(ctx); REL(dev);
  return result;
}
int main(int argc,char **argv) {
  IDXGIFactory1 *factory=NULL; IDXGIAdapter1 *adapter=NULL,*pick=NULL;
  HMODULE dll=NULL; pD3DCompile fn=NULL; HANDLE watch; int pass=1;
  static const UINT sizes[]={8,16,24,64,96};
  if(argc<2) { printf("usage: agx_scratch_smoke.exe <out.txt> [nir] [vs] [cs]\n"); return 2; }
  output=CreateFileA(argv[1],GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,
                     FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,NULL);
  for(int a=2;a<argc;++a) {
    if(!strcmp(argv[a],"nir")) SetEnvironmentVariableA("APPLE_AGX_D3D_SHADER_NIR","1");
    if(!strcmp(argv[a],"vs")) vertex_mode=1;
    if(!strcmp(argv[a],"cs")) compute_mode=1;
  }
  watch=CreateThread(NULL,0,watchdog,NULL,0,NULL); if(watch) CloseHandle(watch);
  dll=LoadLibraryA("d3dcompiler_47.dll");
  fn=dll?(pD3DCompile)(void *)GetProcAddress(dll,"D3DCompile"):NULL;
  if(!fn || FAILED(CreateDXGIFactory1(&IID_IDXGIFactory1,(void **)&factory))) { pass=0; goto done; }
  for(UINT i=0;IDXGIFactory1_EnumAdapters1(factory,i,&adapter)!=DXGI_ERROR_NOT_FOUND;i++) {
    DXGI_ADAPTER_DESC1 desc;
    if(SUCCEEDED(IDXGIAdapter1_GetDesc1(adapter,&desc)) && desc.VendorId==0x4c505041u) {
      pick=adapter; adapter=NULL; break;
    }
    REL(adapter); adapter=NULL;
  }
  if(!pick) { say("NO_APPLE_AGX_ADAPTER"); pass=0; goto done; }
  for (UINT i=0;i<sizeof(sizes)/sizeof(sizes[0]);++i) {
    int r=compute_mode?run_cs(pick,fn,sizes[i]):run(pick,fn,sizes[i]);
    if (r<=0) pass=0;
    if (r<0) break; /* a lost device: later sizes would only repeat the hang */
  }
done:
  say("AGX_SCRATCH_SMOKE: %s",pass?"PASS":"FAIL");
  REL(adapter); REL(pick); REL(factory); if(dll) FreeLibrary(dll);
  if(output!=INVALID_HANDLE_VALUE) CloseHandle(output);
  return pass?0:1;
}
