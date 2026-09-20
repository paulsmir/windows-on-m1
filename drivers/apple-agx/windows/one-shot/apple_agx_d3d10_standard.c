#define COBJMACROS
#include <windows.h>
#include <d3d10.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <stdio.h>

#define RELEASE_IF(p, T) do { if (p) { T##_Release(p); (p)=NULL; } } while (0)

static LRESULT CALLBACK StandardWindowProc(HWND window, UINT message,
                                            WPARAM wparam, LPARAM lparam) {
  if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
  return DefWindowProcW(window,message,wparam,lparam);
}

static HRESULT CompileShader(const char *source, const char *target,
                             ID3DBlob **bytecode) {
  ID3DBlob *errors=NULL;
  HRESULT result=D3DCompile(source,strlen(source),"AppleAgxStandard",NULL,NULL,
      "main",target,D3DCOMPILE_ENABLE_STRICTNESS,0,bytecode,&errors);
  if (FAILED(result) && errors)
    fprintf(stderr,"SHADER_COMPILE: %.*s\n",(int)ID3D10Blob_GetBufferSize(errors),
            (const char *)ID3D10Blob_GetBufferPointer(errors));
  RELEASE_IF(errors,ID3D10Blob);
  return result;
}

int wmain(void) {
  static const char vsSource[]=
      "float4 main(float4 p:POSITION):SV_POSITION{return p;}";
  static const char psSource[]=
      "float4 main():SV_Target{return float4(0.15,0.55,0.25,1.0);}";
  static const float vertices[12]={-0.75f,-0.75f,0.0f,1.0f,
                                    0.0f, 0.75f,0.0f,1.0f,
                                    0.75f,-0.75f,0.0f,1.0f};
  WNDCLASSW wc={0}; HWND window=NULL; IDXGISwapChain *swap=NULL;
  ID3D10Device *device=NULL; ID3D10Texture2D *back=NULL;
  ID3D10RenderTargetView *rtv=NULL; ID3DBlob *vsBytes=NULL,*psBytes=NULL;
  ID3D10VertexShader *vs=NULL; ID3D10PixelShader *ps=NULL;
  ID3D10InputLayout *layout=NULL; ID3D10Buffer *vb=NULL;
  IDXGIDevice *dxgiDevice=NULL; IDXGIAdapter *adapter=NULL;
  IDXGIFactory *factory=NULL;
  const char *stage="window";
  HRESULT result=E_FAIL; DXGI_ADAPTER_DESC adapterDesc={0};
  wc.lpfnWndProc=StandardWindowProc;wc.hInstance=GetModuleHandleW(NULL);
  wc.lpszClassName=L"AppleAgxStandardRuntimeQualification";
  if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) goto done;
  window=CreateWindowExW(0,wc.lpszClassName,L"Apple AGX Standard Runtime",
      WS_OVERLAPPEDWINDOW,0,0,2560,1600,NULL,NULL,wc.hInstance,NULL);
  if(!window) goto done;
  ShowWindow(window,SW_SHOW);UpdateWindow(window);
  DXGI_SWAP_CHAIN_DESC sd={0};sd.BufferDesc.Width=2560;sd.BufferDesc.Height=1600;
  sd.BufferDesc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
  sd.SampleDesc.Count=1;sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.BufferCount=1;sd.OutputWindow=window;sd.Windowed=TRUE;
  sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
  stage="create-device";
  result=D3D10CreateDevice(NULL,D3D10_DRIVER_TYPE_HARDWARE,NULL,0,
      D3D10_SDK_VERSION,&device);
  fprintf(stderr,"STANDARD_STAGE stage=%s hr=0x%08lx\n",stage,(ULONG)result);
  fflush(stderr);
  if(FAILED(result)) goto done;
  stage="device-adapter";
  result=ID3D10Device_QueryInterface(device,&IID_IDXGIDevice,(void **)&dxgiDevice);
  if(FAILED(result)) goto done;
  result=IDXGIDevice_GetAdapter(dxgiDevice,&adapter);if(FAILED(result)) goto done;
  result=IDXGIAdapter_GetDesc(adapter,&adapterDesc);if(FAILED(result)) goto done;
  wprintf(L"STANDARD_ADAPTER vendor=0x%04x device=0x%04x desc=%ls\n",
      adapterDesc.VendorId,adapterDesc.DeviceId,adapterDesc.Description);
  if(adapterDesc.VendorId!=0x106bu){result=DXGI_ERROR_UNSUPPORTED;goto done;}
  stage="adapter-factory";
  result=IDXGIAdapter_GetParent(adapter,&IID_IDXGIFactory,(void **)&factory);
  if(FAILED(result)) goto done;
  stage="create-swap-chain";
  result=IDXGIFactory_CreateSwapChain(factory,(IUnknown *)device,&sd,&swap);
  fprintf(stderr,"STANDARD_STAGE stage=%s hr=0x%08lx\n",stage,(ULONG)result);
  fflush(stderr);
  if(FAILED(result)) goto done;
  stage="back-buffer";
  result=IDXGISwapChain_GetBuffer(swap,0,&IID_ID3D10Texture2D,(void **)&back);
  if(FAILED(result)) goto done;
  result=ID3D10Device_CreateRenderTargetView(device,(ID3D10Resource *)back,NULL,&rtv);
  if(FAILED(result)) goto done;
  result=CompileShader(vsSource,"vs_4_0",&vsBytes);if(FAILED(result)) goto done;
  result=CompileShader(psSource,"ps_4_0",&psBytes);if(FAILED(result)) goto done;
  result=ID3D10Device_CreateVertexShader(device,ID3D10Blob_GetBufferPointer(vsBytes),
      ID3D10Blob_GetBufferSize(vsBytes),&vs);if(FAILED(result)) goto done;
  result=ID3D10Device_CreatePixelShader(device,ID3D10Blob_GetBufferPointer(psBytes),
      ID3D10Blob_GetBufferSize(psBytes),&ps);if(FAILED(result)) goto done;
  D3D10_INPUT_ELEMENT_DESC element={"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,
      0,0,D3D10_INPUT_PER_VERTEX_DATA,0};
  result=ID3D10Device_CreateInputLayout(device,&element,1,
      ID3D10Blob_GetBufferPointer(vsBytes),ID3D10Blob_GetBufferSize(vsBytes),&layout);
  if(FAILED(result)) goto done;
  D3D10_BUFFER_DESC bd={sizeof(vertices),D3D10_USAGE_DEFAULT,
      D3D10_BIND_VERTEX_BUFFER,0,0};D3D10_SUBRESOURCE_DATA init={vertices,0,0};
  result=ID3D10Device_CreateBuffer(device,&bd,&init,&vb);if(FAILED(result)) goto done;
  UINT stride=16,offset=0;D3D10_VIEWPORT viewport={0,0,2560,1600,0.0f,1.0f};
  ID3D10Device_OMSetRenderTargets(device,1,&rtv,NULL);
  ID3D10Device_RSSetViewports(device,1,&viewport);
  ID3D10Device_IASetInputLayout(device,layout);
  ID3D10Device_IASetVertexBuffers(device,0,1,&vb,&stride,&offset);
  ID3D10Device_IASetPrimitiveTopology(device,D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ID3D10Device_VSSetShader(device,vs);ID3D10Device_PSSetShader(device,ps);
  const float clear[4]={0.02f,0.02f,0.04f,1.0f};
  ID3D10Device_ClearRenderTargetView(device,rtv,clear);
  ID3D10Device_Draw(device,3,0);ID3D10Device_Flush(device);
  stage="present";
  result=IDXGISwapChain_Present(swap,0,0);if(FAILED(result)) goto done;
  puts("STANDARD_RUNTIME_PASS create=PASS draw=PASS present=PASS");
 done:
  if(FAILED(result)) fprintf(stderr,"STANDARD_RUNTIME_FAIL stage=%s hr=0x%08lx\n",stage,(ULONG)result);
  RELEASE_IF(vb,ID3D10Buffer);RELEASE_IF(layout,ID3D10InputLayout);
  RELEASE_IF(ps,ID3D10PixelShader);RELEASE_IF(vs,ID3D10VertexShader);
  RELEASE_IF(psBytes,ID3D10Blob);RELEASE_IF(vsBytes,ID3D10Blob);
  RELEASE_IF(rtv,ID3D10RenderTargetView);RELEASE_IF(back,ID3D10Texture2D);
  RELEASE_IF(factory,IDXGIFactory);
  RELEASE_IF(adapter,IDXGIAdapter);RELEASE_IF(dxgiDevice,IDXGIDevice);
  RELEASE_IF(device,ID3D10Device);RELEASE_IF(swap,IDXGISwapChain);
  if(window) DestroyWindow(window);
  return FAILED(result)?1:0;
}
