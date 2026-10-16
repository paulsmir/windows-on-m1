/* GPU timestamps checkpoint (docs/superpowers/plans/2026-10-16-gpu-timestamps.md):
 * D3D11 TIMESTAMP queries around GPU work of known relative length.
 * Each case: TIMESTAMP_DISJOINT begin, TIMESTAMP t0, the work, TIMESTAMP
 * t1, disjoint end, flush, then GetData on all three (5 s bound).
 * - render: a 512x512 full-screen triangle whose pixel shader loops N times;
 * - compute: 256 groups of 64 threads, each looping N times.
 * N = 1, 16384, 65536 (EXP1340: N <= 4096 took less GPU time than a job's
 * round trip, so t1 - t0 did not order). With GPU timestamps the disjoint
 * query reports 1 GHz and not disjoint, t1 > t0, and t1 - t0 of the
 * heaviest case exceeds the lightest by > 2 ms (Mesa's TIMESTAMP is the
 * batch's firmware end when it is later than the CPU clock at End); each
 * line also gives the CPU time from End(t0) to the data. With the CPU
 * emulation it reports the counter frequency and disjoint (verdict CPU).
 * Usage: agx_timestamp_smoke.exe <out.txt> */
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
  for (;;) { Sleep(1000); if (started && (DWORD)(GetTickCount()-(DWORD)started)>30000) {
    say("WATCHDOG timestamp smoke"); TerminateProcess(GetCurrentProcess(),1);
  } }
}
static const char vs_hlsl[] =
  "float4 main(uint id : SV_VertexID) : SV_Position {\n"
  "  float2 p = float2((id & 1) ? 3.0 : -1.0, (id & 2) ? -3.0 : 1.0);\n"
  "  return float4(p, 0, 1); }\n";
static const char ps_hlsl[] =
  "cbuffer c : register(b0) { uint4 n; };\n"
  "float4 main(float4 p : SV_Position) : SV_Target {\n"
  "  float a = p.x * 0.001;\n"
  "  [loop] for (uint k = 0; k < n.x; ++k) a = frac(a * 1.618 + 0.25);\n"
  "  return float4(a, 0.5, 0, 1); }\n";
static const char cs_hlsl[] =
  "cbuffer c : register(b0) { uint4 n; };\n"
  "RWByteAddressBuffer dst : register(u0);\n"
  "[numthreads(64, 1, 1)]\n"
  "void main(uint3 t : SV_DispatchThreadID) {\n"
  "  uint a = t.x;\n"
  "  [loop] for (uint k = 0; k < n.x; ++k) a = a * 1664525u + 1013904223u;\n"
  "  dst.Store(t.x * 4, a); }\n";
static ID3DBlob *compile(pD3DCompile fn,const char *src,const char *target) {
  ID3DBlob *code=NULL,*error=NULL; HRESULT hr=fn(src,strlen(src),NULL,NULL,NULL,"main",target,0,0,&code,&error);
  if(error) { say("COMPILE %s %.300s",target,(const char *)ID3D10Blob_GetBufferPointer(error)); REL(error); }
  if(FAILED(hr)) { REL(code); return NULL; } return code;
}
/* Polls one query until ready (or 5 s). */
static HRESULT wait_data(ID3D11DeviceContext *ctx,ID3D11Query *q,void *data,UINT bytes) {
  DWORD t0=GetTickCount(); HRESULT hr;
  for (;;) {
    hr=ID3D11DeviceContext_GetData(ctx,(ID3D11Asynchronous *)q,data,bytes,0);
    if(FAILED(hr) || hr==S_OK) return hr;
    if((DWORD)(GetTickCount()-t0)>5000) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    Sleep(1);
  }
}
typedef struct { ID3D11Device *dev; ID3D11DeviceContext *ctx; ID3D11VertexShader *vs;
  ID3D11PixelShader *ps; ID3D11ComputeShader *cs; ID3D11Buffer *cb, *dst;
  ID3D11UnorderedAccessView *uav; ID3D11RenderTargetView *rtv; ID3D11Texture2D *rt;
  ID3D11RasterizerState *rs; } SETUP;
/* Returns 1 GPU (not disjoint, 1 GHz, t1 > t0), 0 CPU emulation, -1 failure. */
static int measure(SETUP *s,int compute,UINT n,unsigned long long *delta) {
  const UINT counts[4]={n,0,0,0};
  D3D11_QUERY_DESC qd; ID3D11Query *disjoint=NULL,*t0=NULL,*t1=NULL;
  D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj; UINT64 v0=0,v1=0; HRESULT hr; int result=-1;
  LARGE_INTEGER cpu0={0},cpu1={0},freq={0}; unsigned long long cpu_us=0;
  memset(&dj,0,sizeof(dj));
  memset(&qd,0,sizeof(qd)); qd.Query=D3D11_QUERY_TIMESTAMP_DISJOINT;
  hr=ID3D11Device_CreateQuery(s->dev,&qd,&disjoint); if(FAILED(hr)) goto done;
  qd.Query=D3D11_QUERY_TIMESTAMP;
  hr=ID3D11Device_CreateQuery(s->dev,&qd,&t0); if(FAILED(hr)) goto done;
  hr=ID3D11Device_CreateQuery(s->dev,&qd,&t1); if(FAILED(hr)) goto done;
  ID3D11DeviceContext_UpdateSubresource(s->ctx,(ID3D11Resource *)s->cb,0,NULL,counts,0,0);
  ID3D11DeviceContext_Begin(s->ctx,(ID3D11Asynchronous *)disjoint);
  QueryPerformanceCounter(&cpu0);
  ID3D11DeviceContext_End(s->ctx,(ID3D11Asynchronous *)t0);
  if (compute) {
    ID3D11DeviceContext_CSSetShader(s->ctx,s->cs,NULL,0);
    ID3D11DeviceContext_CSSetConstantBuffers(s->ctx,0,1,&s->cb);
    ID3D11DeviceContext_CSSetUnorderedAccessViews(s->ctx,0,1,&s->uav,NULL);
    ID3D11DeviceContext_Dispatch(s->ctx,256,1,1);
  } else {
    D3D11_VIEWPORT vp; const float clear[4]={0,0,1,1};
    memset(&vp,0,sizeof(vp)); vp.Width=512; vp.Height=512; vp.MaxDepth=1;
    ID3D11DeviceContext_ClearRenderTargetView(s->ctx,s->rtv,clear);
    ID3D11DeviceContext_OMSetRenderTargets(s->ctx,1,&s->rtv,NULL);
    ID3D11DeviceContext_RSSetState(s->ctx,s->rs); ID3D11DeviceContext_RSSetViewports(s->ctx,1,&vp);
    ID3D11DeviceContext_VSSetShader(s->ctx,s->vs,NULL,0); ID3D11DeviceContext_PSSetShader(s->ctx,s->ps,NULL,0);
    ID3D11DeviceContext_PSSetConstantBuffers(s->ctx,0,1,&s->cb);
    ID3D11DeviceContext_IASetPrimitiveTopology(s->ctx,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_Draw(s->ctx,3,0);
  }
  ID3D11DeviceContext_End(s->ctx,(ID3D11Asynchronous *)t1);
  ID3D11DeviceContext_End(s->ctx,(ID3D11Asynchronous *)disjoint);
  ID3D11DeviceContext_Flush(s->ctx);
  hr=wait_data(s->ctx,disjoint,&dj,sizeof(dj)); if(FAILED(hr) || hr!=S_OK) goto done;
  hr=wait_data(s->ctx,t0,&v0,sizeof(v0)); if(FAILED(hr) || hr!=S_OK) goto done;
  hr=wait_data(s->ctx,t1,&v1,sizeof(v1)); if(FAILED(hr) || hr!=S_OK) goto done;
  QueryPerformanceCounter(&cpu1); QueryPerformanceFrequency(&freq);
  cpu_us=(unsigned long long)((cpu1.QuadPart-cpu0.QuadPart)*1000000LL/freq.QuadPart);
  *delta=v1>=v0 ? v1-v0 : 0;
  result=(!dj.Disjoint && dj.Frequency==1000000000ULL && v1>v0) ? 1 : 0;
done:
  say("TS %s n=%u t0=%llu t1=%llu delta=%llu cpu_us=%llu frequency=%llu disjoint=%d hr=0x%08lx removed=0x%08lx verdict=%s",
      compute?"compute":"render",n,(unsigned long long)v0,(unsigned long long)v1,
      (unsigned long long)(v1>=v0?v1-v0:0),cpu_us,(unsigned long long)dj.Frequency,(int)dj.Disjoint,(unsigned long)hr,
      (unsigned long)ID3D11Device_GetDeviceRemovedReason(s->dev),
      result>0?"GPU":result==0?"CPU":"FAIL");
  REL(t1); REL(t0); REL(disjoint);
  return result;
}
int main(int argc,char **argv) {
  IDXGIFactory1 *factory=NULL; IDXGIAdapter1 *adapter=NULL,*pick=NULL;
  HMODULE dll=NULL; pD3DCompile fn=NULL; HANDLE watch; int pass=1;
  const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0,D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};
  static const UINT ns[]={1,16384,65536};
  SETUP s; ID3DBlob *vsc=NULL,*psc=NULL,*csc=NULL; D3D_FEATURE_LEVEL got; HRESULT hr;
  D3D11_BUFFER_DESC bd; D3D11_TEXTURE2D_DESC td; D3D11_RASTERIZER_DESC rd; D3D11_UNORDERED_ACCESS_VIEW_DESC ud;
  memset(&s,0,sizeof(s));
  if(argc<2) { printf("usage: agx_timestamp_smoke.exe <out.txt>\n"); return 2; }
  output=CreateFileA(argv[1],GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,
                     FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,NULL);
  watch=CreateThread(NULL,0,watchdog,NULL,0,NULL); if(watch) CloseHandle(watch);
  started=(LONG)GetTickCount();
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
  hr=D3D11CreateDevice((IDXGIAdapter *)pick,D3D_DRIVER_TYPE_UNKNOWN,NULL,0,levels,3,
                       D3D11_SDK_VERSION,&s.dev,&got,&s.ctx);
  say("D3D11CreateDevice 0x%08lx FEATURE_LEVEL 0x%x",(unsigned long)hr,(unsigned)got);
  if(FAILED(hr)) { pass=0; goto done; }
  vsc=compile(fn,vs_hlsl,"vs_4_0"); psc=compile(fn,ps_hlsl,"ps_4_0"); csc=compile(fn,cs_hlsl,"cs_4_0");
  if(!vsc || !psc || !csc ||
     FAILED(ID3D11Device_CreateVertexShader(s.dev,ID3D10Blob_GetBufferPointer(vsc),ID3D10Blob_GetBufferSize(vsc),NULL,&s.vs)) ||
     FAILED(ID3D11Device_CreatePixelShader(s.dev,ID3D10Blob_GetBufferPointer(psc),ID3D10Blob_GetBufferSize(psc),NULL,&s.ps)) ||
     FAILED(ID3D11Device_CreateComputeShader(s.dev,ID3D10Blob_GetBufferPointer(csc),ID3D10Blob_GetBufferSize(csc),NULL,&s.cs))) {
    say("SETUP shaders failed"); pass=0; goto done;
  }
  memset(&bd,0,sizeof(bd)); bd.ByteWidth=16; bd.Usage=D3D11_USAGE_DEFAULT; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
  hr=ID3D11Device_CreateBuffer(s.dev,&bd,NULL,&s.cb);
  memset(&bd,0,sizeof(bd)); bd.ByteWidth=256*64*4; bd.Usage=D3D11_USAGE_DEFAULT;
  bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS; bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
  if(SUCCEEDED(hr)) hr=ID3D11Device_CreateBuffer(s.dev,&bd,NULL,&s.dst);
  memset(&ud,0,sizeof(ud)); ud.Format=DXGI_FORMAT_R32_TYPELESS; ud.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;
  ud.Buffer.NumElements=256*64; ud.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
  if(SUCCEEDED(hr)) hr=ID3D11Device_CreateUnorderedAccessView(s.dev,(ID3D11Resource *)s.dst,&ud,&s.uav);
  memset(&td,0,sizeof(td)); td.Width=512; td.Height=512; td.MipLevels=td.ArraySize=1;
  td.Format=DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_DEFAULT;
  td.BindFlags=D3D11_BIND_RENDER_TARGET;
  if(SUCCEEDED(hr)) hr=ID3D11Device_CreateTexture2D(s.dev,&td,NULL,&s.rt);
  if(SUCCEEDED(hr)) hr=ID3D11Device_CreateRenderTargetView(s.dev,(ID3D11Resource *)s.rt,NULL,&s.rtv);
  memset(&rd,0,sizeof(rd)); rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_NONE; rd.DepthClipEnable=TRUE;
  if(SUCCEEDED(hr)) hr=ID3D11Device_CreateRasterizerState(s.dev,&rd,&s.rs);
  if(FAILED(hr)) { say("SETUP resources 0x%08lx",(unsigned long)hr); pass=0; goto done; }
  for (int compute=0;compute<2;++compute) {
    unsigned long long previous=0;
    for (UINT i=0;i<sizeof(ns)/sizeof(ns[0]);++i) {
      unsigned long long delta=0;
      int r=measure(&s,compute,ns[i],&delta);
      if (r<=0) pass=0;
      if (r<0) break;
      /* the longest loop must take > 2 ms longer than the shortest */
      if (i==2 && delta<=previous+2000000ULL) { say("TS %s ORDER delta(n=%u)=%llu <= %llu + 2 ms",
          compute?"compute":"render",ns[i],delta,previous); pass=0; }
      if (i==0) previous=delta;
    }
  }
done:
  say("AGX_TIMESTAMP_SMOKE: %s",pass?"PASS":"FAIL");
  started=0;
  REL(s.rs); REL(s.rtv); REL(s.rt); REL(s.uav); REL(s.dst); REL(s.cb); REL(s.cs); REL(s.ps); REL(s.vs);
  REL(csc); REL(psc); REL(vsc); REL(s.ctx); REL(s.dev);
  REL(adapter); REL(pick); REL(factory); if(dll) FreeLibrary(dll);
  if(output!=INVALID_HANDLE_VALUE) CloseHandle(output);
  return pass?0:1;
}
