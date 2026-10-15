/* EXP1298: compare SV_IsFrontFace on the TGSI and direct NIR paths. */
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
    say("WATCHDOG face smoke"); TerminateProcess(GetCurrentProcess(),1);
  } }
}
static const char vs_hlsl[] =
  "float4 main(uint id : SV_VertexID) : SV_Position {\n"
  "  float2 p[6] = { float2(-0.9,-0.7), float2(-0.1,-0.7), float2(-0.5,0.7),\n"
  "                  float2(0.1,-0.7), float2(0.5,0.7), float2(0.9,-0.7) };\n"
  "  return float4(p[id],0,1); }\n";
static const char ps_hlsl[] =
  "float4 main(float4 p : SV_Position, bool f : SV_IsFrontFace) : SV_Target {\n"
  "  return f ? float4(0,1,0,1) : float4(1,0,0,1); }\n";
static ID3DBlob *compile(pD3DCompile fn,const char *src,const char *target) {
  ID3DBlob *code=NULL,*error=NULL; HRESULT hr=fn(src,strlen(src),NULL,NULL,NULL,"main",target,0,0,&code,&error);
  if(error) { say("COMPILE %s %.300s",target,(const char *)ID3D10Blob_GetBufferPointer(error)); REL(error); }
  if(FAILED(hr)) { REL(code); return NULL; } return code;
}
static UINT pixel(const D3D11_MAPPED_SUBRESOURCE *m,UINT x,UINT y) {
  const BYTE *p=(const BYTE *)m->pData+y*m->RowPitch+x*4;
  return (UINT)p[0] | ((UINT)p[1]<<8) | ((UINT)p[2]<<16) | ((UINT)p[3]<<24);
}
static int run(IDXGIAdapter1 *adapter,pD3DCompile fn,int nir) {
  const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_10_1,D3D_FEATURE_LEVEL_10_0};
  const float blue[4]={0,0,1,1}; const char *path=nir?"nir":"tgsi";
  ID3D11Device *dev=NULL; ID3D11DeviceContext *ctx=NULL; ID3D11VertexShader *vs=NULL;
  ID3D11PixelShader *ps=NULL; ID3D11Texture2D *rt=NULL,*readback=NULL;
  ID3D11RenderTargetView *rtv=NULL; ID3D11RasterizerState *rs=NULL;
  ID3D11Query *query=NULL; ID3DBlob *vscode=NULL,*pscode=NULL;
  D3D11_TEXTURE2D_DESC td; D3D11_RASTERIZER_DESC rd; D3D11_VIEWPORT vp;
  D3D11_QUERY_DESC qd; D3D11_MAPPED_SUBRESOURCE mapped; D3D_FEATURE_LEVEL got;
  HRESULT hr=S_OK; UINT front=0,back=0; int pass=0;
  if(!SetEnvironmentVariableA("APPLE_AGX_D3D_SHADER_NIR",nir?"1":NULL)) goto done;
  started=(LONG)GetTickCount();
  hr=D3D11CreateDevice((IDXGIAdapter *)adapter,D3D_DRIVER_TYPE_UNKNOWN,NULL,0,levels,2,
                       D3D11_SDK_VERSION,&dev,&got,&ctx); if(FAILED(hr)) goto done;
  vscode=compile(fn,vs_hlsl,"vs_4_0"); pscode=compile(fn,ps_hlsl,"ps_4_0");
  if(!vscode || !pscode) goto done;
  hr=ID3D11Device_CreateVertexShader(dev,ID3D10Blob_GetBufferPointer(vscode),ID3D10Blob_GetBufferSize(vscode),NULL,&vs);
  if(FAILED(hr)) goto done;
  hr=ID3D11Device_CreatePixelShader(dev,ID3D10Blob_GetBufferPointer(pscode),ID3D10Blob_GetBufferSize(pscode),NULL,&ps);
  if(FAILED(hr)) goto done;
  memset(&td,0,sizeof(td)); td.Width=td.Height=64; td.MipLevels=td.ArraySize=1;
  td.Format=DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count=1; td.Usage=D3D11_USAGE_DEFAULT;
  td.BindFlags=D3D11_BIND_RENDER_TARGET;
  hr=ID3D11Device_CreateTexture2D(dev,&td,NULL,&rt); if(FAILED(hr)) goto done;
  hr=ID3D11Device_CreateRenderTargetView(dev,(ID3D11Resource *)rt,NULL,&rtv); if(FAILED(hr)) goto done;
  td.BindFlags=0; td.Usage=D3D11_USAGE_STAGING; td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  hr=ID3D11Device_CreateTexture2D(dev,&td,NULL,&readback); if(FAILED(hr)) goto done;
  memset(&rd,0,sizeof(rd)); rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_NONE;
  rd.FrontCounterClockwise=FALSE; rd.DepthClipEnable=TRUE;
  hr=ID3D11Device_CreateRasterizerState(dev,&rd,&rs); if(FAILED(hr)) goto done;
  memset(&vp,0,sizeof(vp)); vp.Width=vp.Height=64; vp.MaxDepth=1;
  ID3D11DeviceContext_ClearRenderTargetView(ctx,rtv,blue);
  ID3D11DeviceContext_OMSetRenderTargets(ctx,1,&rtv,NULL);
  ID3D11DeviceContext_RSSetState(ctx,rs); ID3D11DeviceContext_RSSetViewports(ctx,1,&vp);
  ID3D11DeviceContext_VSSetShader(ctx,vs,NULL,0); ID3D11DeviceContext_PSSetShader(ctx,ps,NULL,0);
  ID3D11DeviceContext_IASetPrimitiveTopology(ctx,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ID3D11DeviceContext_Draw(ctx,6,0);
  memset(&qd,0,sizeof(qd)); qd.Query=D3D11_QUERY_EVENT;
  hr=ID3D11Device_CreateQuery(dev,&qd,&query); if(FAILED(hr)) goto done;
  ID3D11DeviceContext_End(ctx,(ID3D11Asynchronous *)query);
  ID3D11DeviceContext_Flush(ctx);
  for (;;) { BOOL complete=FALSE;
    hr=ID3D11DeviceContext_GetData(ctx,(ID3D11Asynchronous *)query,&complete,sizeof(complete),0);
    if(FAILED(hr)) goto done; if(hr==S_OK && complete) break;
    if((DWORD)(GetTickCount()-(DWORD)started)>19000) goto done; Sleep(1);
  }
  ID3D11DeviceContext_CopyResource(ctx,(ID3D11Resource *)readback,(ID3D11Resource *)rt);
  hr=ID3D11DeviceContext_Map(ctx,(ID3D11Resource *)readback,0,D3D11_MAP_READ,0,&mapped);
  if(FAILED(hr)) goto done;
  front=pixel(&mapped,16,32); back=pixel(&mapped,48,32);
  ID3D11DeviceContext_Unmap(ctx,(ID3D11Resource *)readback,0);
  pass=front==0xff00ff00u && back==0xff0000ffu;
done:
  say("FACE path=%s front=0x%08x back=0x%08x verdict=%s",path,front,back,pass?"PASS":"FAIL");
  if(FAILED(hr)) say("FACE path=%s HRESULT=0x%08lx",path,(unsigned long)hr);
  started=0; REL(query); REL(rs); REL(readback); REL(rtv); REL(rt);
  REL(ps); REL(vs); REL(pscode); REL(vscode); REL(ctx); REL(dev);
  return pass;
}
int main(int argc,char **argv) {
  IDXGIFactory1 *factory=NULL; IDXGIAdapter1 *adapter=NULL,*pick=NULL;
  HMODULE dll=NULL; pD3DCompile fn=NULL; HANDLE watch; int pass=0;
  if(argc<2) { printf("usage: agx_face_smoke.exe <out.txt>\n"); return 2; }
  output=CreateFileA(argv[1],GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,
                     FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,NULL);
  watch=CreateThread(NULL,0,watchdog,NULL,0,NULL); if(watch) CloseHandle(watch);
  dll=LoadLibraryA("d3dcompiler_47.dll");
  fn=dll?(pD3DCompile)(void *)GetProcAddress(dll,"D3DCompile"):NULL;
  if(!fn || FAILED(CreateDXGIFactory1(&IID_IDXGIFactory1,(void **)&factory))) goto done;
  for(UINT i=0;IDXGIFactory1_EnumAdapters1(factory,i,&adapter)!=DXGI_ERROR_NOT_FOUND;i++) {
    DXGI_ADAPTER_DESC1 desc;
    if(SUCCEEDED(IDXGIAdapter1_GetDesc1(adapter,&desc)) && desc.VendorId==0x4c505041u) {
      pick=adapter; adapter=NULL; break;
    }
    REL(adapter); adapter=NULL;
  }
  if(!pick) { say("NO_APPLE_AGX_ADAPTER"); goto done; }
  { int tgsi=run(pick,fn,0); int nir=run(pick,fn,1); pass=tgsi && nir; }
done:
  say("AGX_FACE_SMOKE: %s",pass?"PASS":"FAIL");
  REL(adapter); REL(pick); REL(factory); if(dll) FreeLibrary(dll);
  if(output!=INVALID_HANDLE_VALUE) CloseHandle(output);
  return pass?0:1;
}
