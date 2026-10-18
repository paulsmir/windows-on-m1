/* D3D-G4 hardware checkpoint: D3D depth-stencil views.
 * A 1024x768 R8G8B8A8 target with a D24S8, D32 or D16 depth buffer (3 MiB for
 * the 32-bit formats, above the old 1 MiB D3D depth bound). Each iteration:
 * - clears colour and depth (odd D24S8 iterations clear depth only);
 * - draws a full-screen red quad at z 0.5 (depth test LESS, depth write on);
 * - draws a blue quad over one half at z 0.3 (visible) and a green quad
 *   over the other half at z 0.7 (hidden); the halves swap every iteration,
 *   so stale depth or a missing clear shows;
 * - with stencil (D24S8): writes stencil 1 under a centre square at z 0.2
 *   (colour yellow), then draws white at z 0.1 with stencil EQUAL 1, so only
 *   the square turns white.
 * The target is read back every iteration and checked at four points.
 * Mode "prepass" (D3D-G7) instead renders a depth-only pass first (no render
 * target bound) that writes z 0.3 over one half, then binds the colour
 * target and draws red at z 0.5 over everything: only the other half may
 * turn red, so the depth-only pass must have written depth.
 * Every line is written through to the output file before the next step.
 * Usage: agx_depth_smoke.exe <out.txt> [d24|d32|d16] [iterations] [prepass] */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH 1024
#define HEIGHT 768

static HANDLE out = INVALID_HANDLE_VALUE;

static void say(const char *format, ...) {
  char line[512];
  DWORD written;
  va_list args;
  int n;
  va_start(args, format);
  n = vsnprintf(line, sizeof(line) - 2, format, args);
  va_end(args);
  if (n < 0) return;
  if (n > (int)sizeof(line) - 3) n = (int)sizeof(line) - 3;
  line[n++] = '\r';
  line[n++] = '\n';
  fwrite(line, 1, (size_t)n, stdout);
  fflush(stdout);
  if (out != INVALID_HANDLE_VALUE) {
    WriteFile(out, line, (DWORD)n, &written, NULL);
    FlushFileBuffers(out);
  }
}

/* A rectangle (clip-space x0,y0,x1,y1) at depth z, filled with one colour. */
static const char vs_source[] =
    "cbuffer C : register(b0) { float4 rect; float4 zc; float4 colour; };\n"
    "float4 main(uint id : SV_VertexID) : SV_Position {\n"
    "  float2 c = float2((id == 1 || id == 3 || id == 4) ? rect.z : rect.x,\n"
    "                    (id == 2 || id == 4 || id == 5) ? rect.w : rect.y);\n"
    "  return float4(c, zc.x, 1);\n"
    "}\n";
static const char ps_source[] =
    "cbuffer C : register(b0) { float4 rect; float4 zc; float4 colour; };\n"
    "float4 main(float4 p : SV_Position) : SV_Target { return colour; }\n";

static ID3DBlob *compile(pD3DCompile fn, const char *src, size_t len, const char *target) {
  ID3DBlob *code = NULL, *errors = NULL;
  HRESULT hr = fn(src, len, NULL, NULL, NULL, "main", target, 0, 0, &code, &errors);
  say("D3DCompile %s 0x%08lx", target, (unsigned long)hr);
  if (errors) {
    say("%.300s", (const char *)ID3D10Blob_GetBufferPointer(errors));
    ID3D10Blob_Release(errors);
  }
  return SUCCEEDED(hr) ? code : NULL;
}

static ID3D11DeviceContext *ctx;
static ID3D11Buffer *cb;

static int quad(float x0, float y0, float x1, float y1, float z, unsigned rgba) {
  D3D11_MAPPED_SUBRESOURCE map;
  float *f;
  HRESULT hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)cb, 0,
                                       D3D11_MAP_WRITE_DISCARD, 0, &map);
  if (FAILED(hr)) { say("CB_MAP 0x%08lx", (unsigned long)hr); return 0; }
  f = (float *)map.pData;
  memset(f, 0, 48);
  f[0] = x0; f[1] = y0; f[2] = x1; f[3] = y1; f[4] = z;
  for (int c = 0; c < 4; ++c) f[8 + c] = (float)((rgba >> (8 * c)) & 0xffu) / 255.0f;
  ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)cb, 0);
  ID3D11DeviceContext_Draw(ctx, 6, 0);
  return 1;
}

int main(int argc, char **argv) {
  IDXGIFactory1 *factory = NULL;
  IDXGIAdapter1 *adapter = NULL, *pick = NULL;
  ID3D11Device *device = NULL;
  ID3D11Texture2D *target = NULL, *staging = NULL, *depth = NULL;
  ID3D11RenderTargetView *rtv = NULL;
  ID3D11DepthStencilView *dsv = NULL;
  ID3D11DepthStencilState *less = NULL, *mark = NULL, *equal = NULL;
  ID3D11RasterizerState *raster = NULL;
  ID3D11VertexShader *vs = NULL;
  ID3D11PixelShader *ps = NULL;
  ID3DBlob *vsb, *psb;
  D3D_FEATURE_LEVEL level = 0;
  const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                    D3D_FEATURE_LEVEL_10_0};
  HMODULE compiler;
  pD3DCompile compile_fn;
  HRESULT hr;
  DXGI_FORMAT format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  const char *mode = argc > 2 ? argv[2] : "d24";
  int iterations, bad = 0, stencil, prepass;

  if (argc < 2) {
    printf("usage: agx_depth_smoke.exe <out.txt> [d24|d32|d16] [iterations]\n");
    return 2;
  }
  if (!strcmp(mode, "d32")) format = DXGI_FORMAT_D32_FLOAT;
  else if (!strcmp(mode, "d16")) format = DXGI_FORMAT_D16_UNORM;
  stencil = format == DXGI_FORMAT_D24_UNORM_S8_UINT;
  iterations = argc > 3 ? atoi(argv[3]) : 30;
  if (iterations < 2) iterations = 2;
  prepass = argc > 4 && !strcmp(argv[4], "prepass");
  out = CreateFileA(argv[1], GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
  say("DEPTH %s %dx%d stencil %d iterations %d prepass %d", mode, WIDTH, HEIGHT, stencil,
      iterations, prepass);
  compiler = LoadLibraryA("d3dcompiler_47.dll");
  compile_fn = compiler ? (pD3DCompile)(void *)GetProcAddress(compiler, "D3DCompile") : NULL;
  if (!compile_fn) { say("NO_D3DCOMPILER %lu", GetLastError()); return 1; }
  hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory);
  if (FAILED(hr)) { say("CreateDXGIFactory1 0x%08lx", (unsigned long)hr); return 1; }
  for (UINT a = 0; IDXGIFactory1_EnumAdapters1(factory, a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
    DXGI_ADAPTER_DESC1 d;
    IDXGIAdapter1_GetDesc1(adapter, &d);
    say("ADAPTER %u vendor %04x flags %x %ls", a, d.VendorId, d.Flags, d.Description);
    if (!pick && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) { pick = adapter; continue; }
    IDXGIAdapter1_Release(adapter);
  }
  if (!pick) { say("NO_HARDWARE_ADAPTER"); return 1; }
  hr = D3D11CreateDevice((IDXGIAdapter *)pick, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, want, 3,
                         D3D11_SDK_VERSION, &device, &level, &ctx);
  say("D3D11CreateDevice 0x%08lx FEATURE_LEVEL 0x%x", (unsigned long)hr, level);
  if (FAILED(hr)) return 1;
  {
    D3D11_TEXTURE2D_DESC td;
    D3D11_BUFFER_DESC bd;
    D3D11_DEPTH_STENCIL_DESC dd;
    memset(&td, 0, sizeof(td));
    td.Width = WIDTH; td.Height = HEIGHT; td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = ID3D11Device_CreateTexture2D(device, &td, NULL, &target);
    if (SUCCEEDED(hr))
      hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)target, NULL, &rtv);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateTexture2D(device, &td, NULL, &staging);
    say("COLOUR 0x%08lx", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    td.Format = format; td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL; td.CPUAccessFlags = 0;
    hr = ID3D11Device_CreateTexture2D(device, &td, NULL, &depth);
    say("CreateTexture2D depth 0x%08lx", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    hr = ID3D11Device_CreateDepthStencilView(device, (ID3D11Resource *)depth, NULL, &dsv);
    say("CreateDepthStencilView 0x%08lx", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    memset(&dd, 0, sizeof(dd));
    dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc = D3D11_COMPARISON_LESS;
    dd.FrontFace.StencilFailOp = dd.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    dd.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    dd.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    dd.BackFace = dd.FrontFace;
    hr = ID3D11Device_CreateDepthStencilState(device, &dd, &less);
    dd.StencilEnable = TRUE; dd.StencilReadMask = dd.StencilWriteMask = 0xff;
    dd.FrontFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE; dd.BackFace = dd.FrontFace;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateDepthStencilState(device, &dd, &mark);
    dd.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    dd.FrontFace.StencilFunc = D3D11_COMPARISON_EQUAL; dd.BackFace = dd.FrontFace;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateDepthStencilState(device, &dd, &equal);
    memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = 48; bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateBuffer(device, &bd, NULL, &cb);
    {
      /* The quads' winding is not part of the check. */
      D3D11_RASTERIZER_DESC rd;
      memset(&rd, 0, sizeof(rd));
      rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
      if (SUCCEEDED(hr)) hr = ID3D11Device_CreateRasterizerState(device, &rd, &raster);
    }
    say("STATES 0x%08lx", (unsigned long)hr);
    if (FAILED(hr)) return 1;
  }
  vsb = compile(compile_fn, vs_source, sizeof(vs_source) - 1, "vs_4_0");
  psb = compile(compile_fn, ps_source, sizeof(ps_source) - 1, "ps_4_0");
  if (!vsb || !psb) return 1;
  hr = ID3D11Device_CreateVertexShader(device, ID3D10Blob_GetBufferPointer(vsb),
                                       ID3D10Blob_GetBufferSize(vsb), NULL, &vs);
  if (SUCCEEDED(hr)) hr = ID3D11Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(psb),
                                       ID3D10Blob_GetBufferSize(psb), NULL, &ps);
  say("SHADERS 0x%08lx", (unsigned long)hr);
  if (FAILED(hr)) return 1;
  {
    D3D11_VIEWPORT vp;
    memset(&vp, 0, sizeof(vp));
    vp.Width = (float)WIDTH; vp.Height = (float)HEIGHT; vp.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
  }
  ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, dsv);
  ID3D11DeviceContext_RSSetState(ctx, raster);
  ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0);
  ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
  ID3D11DeviceContext_VSSetConstantBuffers(ctx, 0, 1, &cb);
  ID3D11DeviceContext_PSSetConstantBuffers(ctx, 0, 1, &cb);
  for (int i = 0; i < iterations; ++i) {
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    /* Front half (blue) is the left one on even iterations. */
    const int left_front = (i & 1) == 0;
    const unsigned red = 0xff0000ffu, green = 0xff00ff00u, blue = 0xffff0000u;
    const unsigned white = 0xffffffffu;
    static const int points[4][2] = {{100, 384}, {924, 384}, {512, 384}, {512, 40}};
    unsigned want_px[4], got[4];
    D3D11_MAPPED_SUBRESOURCE map;
    UINT clear = D3D11_CLEAR_DEPTH | (stencil && !(i & 1) ? D3D11_CLEAR_STENCIL : 0u);
    if (prepass) {
      /* Depth-only pass over the front half, then colour over everything. */
      ID3D11DeviceContext_OMSetRenderTargets(ctx, 0, NULL, dsv);
      ID3D11DeviceContext_ClearDepthStencilView(ctx, dsv, clear, 1.0f, 0);
      ID3D11DeviceContext_OMSetDepthStencilState(ctx, less, 0);
      if (!quad(left_front ? -1.0f : 0.0f, -1.0f, left_front ? 0.0f : 1.0f, 1.0f, 0.3f, blue))
        return 1;
      ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, dsv);
      ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, black);
      if (!quad(-1.0f, -1.0f, 1.0f, 1.0f, 0.5f, red)) return 1;
      want_px[0] = left_front ? 0xff000000u : red;
      want_px[1] = left_front ? red : 0xff000000u;
      want_px[2] = left_front ? red : 0xff000000u;
      want_px[3] = want_px[2];
      goto readback;
    }
    ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, black);
    ID3D11DeviceContext_ClearDepthStencilView(ctx, dsv, clear, 1.0f, 0);
    ID3D11DeviceContext_OMSetDepthStencilState(ctx, less, 0);
    if (!quad(-1.0f, -1.0f, 1.0f, 1.0f, 0.5f, red)) return 1;
    if (!quad(left_front ? -1.0f : 0.0f, -1.0f, left_front ? 0.0f : 1.0f, 1.0f, 0.3f, blue))
      return 1;
    if (!quad(left_front ? 0.0f : -1.0f, -1.0f, left_front ? 1.0f : 0.0f, 1.0f, 0.7f, green))
      return 1;
    if (stencil) {
      ID3D11DeviceContext_OMSetDepthStencilState(ctx, mark, 1);
      if (!quad(-0.25f, -0.25f, 0.25f, 0.25f, 0.2f, 0xff00ffffu)) return 1;
      ID3D11DeviceContext_OMSetDepthStencilState(ctx, equal, 1);
      if (!quad(-1.0f, -1.0f, 1.0f, 1.0f, 0.1f, white)) return 1;
    }
    want_px[0] = left_front ? blue : red;
    want_px[1] = left_front ? red : blue;
    want_px[2] = stencil ? white : (left_front ? red : blue);
    want_px[3] = left_front ? red : blue;
  readback:
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging, (ID3D11Resource *)target);
    hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map);
    if (FAILED(hr)) {
      say("READBACK_MAP 0x%08lx at %d removed 0x%08lx", (unsigned long)hr, i,
          (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
      return 1;
    }
    for (int k = 0; k < 4; ++k)
      got[k] = *(const unsigned *)((const unsigned char *)map.pData +
          (size_t)points[k][1] * map.RowPitch + (size_t)points[k][0] * 4u);
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
    for (int k = 0; k < 4; ++k)
      if (got[k] != want_px[k]) {
        if (bad < 8)
          say("WRONG iteration %d point %d,%d got 0x%08x want 0x%08x", i, points[k][0],
              points[k][1], got[k], want_px[k]);
        ++bad;
      }
    if (i < 2 || i == iterations - 1)
      say("ITERATION %d bad %d pixels 0x%08x 0x%08x 0x%08x 0x%08x removed 0x%08lx", i, bad,
          got[0], got[1], got[2], got[3],
          (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
  }
  say("RESULT mode %s iterations %d bad %d removed 0x%08lx", mode, iterations, bad,
      (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
  say(bad ? "AGX_DEPTH_SMOKE: FAIL" : "AGX_DEPTH_SMOKE: PASS");
  return bad ? 1 : 0;
}
