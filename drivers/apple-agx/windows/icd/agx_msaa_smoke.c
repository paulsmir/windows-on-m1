/* D3D-G5 hardware checkpoint: multisampled render targets and resolve.
 * A 256x256 R8G8B8A8 target and a D24S8 depth buffer, both 2x or 4x. Each
 * iteration clears, draws a green triangle over the lower-left half at z 0.5
 * and then a red full-screen quad at z 0.6 (depth LESS, so it fills only the
 * upper-right half), resolves the target into a single-sample texture with
 * ResolveSubresource and reads it back:
 * - a lower-left pixel is green and an upper-right pixel red;
 * - pixels along the diagonal edge are mixed (green and red both strictly
 *   between 0 and 255), which only averaged samples produce.
 * Every line is written through to the output file before the next step.
 * EXP1289: an optional fourth argument "bgra" uses B8G8R8A8_UNORM targets
 * (readback normalized to R8G8B8A8 order).
 * EXP1294 diagnostic: a fifth argument "samples" also reads every sample of
 * the multisampled target through Texture2DMS.Load (first and last
 * iteration): triangle colours there and the clear colour after the resolve
 * place the interior-pixel defect in the resolve, the clear colour there in
 * rendering or storing the samples.
 * Usage: agx_msaa_smoke.exe <out.txt> [2|4] [iterations] [rgba|bgra] [samples] */
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

#define SIZE 256

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

/* zc.y > 0.5: the lower-left triangle; otherwise the rectangle. */
static const char vs_source[] =
    "cbuffer C : register(b0) { float4 rect; float4 zc; float4 colour; };\n"
    "float4 main(uint id : SV_VertexID) : SV_Position {\n"
    "  float2 c;\n"
    "  if (zc.y > 0.5) c = id == 0 ? float2(-1, 1) : id == 1 ? float2(1, -1) : float2(-1, -1);\n"
    "  else c = float2((id == 1 || id == 3 || id == 4) ? rect.z : rect.x,\n"
    "                  (id == 2 || id == 4 || id == 5) ? rect.w : rect.y);\n"
    "  return float4(c, zc.x, 1);\n"
    "}\n";
static const char ps_source[] =
    "cbuffer C : register(b0) { float4 rect; float4 zc; float4 colour; };\n"
    "float4 main(float4 p : SV_Position) : SV_Target { return colour; }\n";
static const char vs_full[] =
    "float4 main(uint id : SV_VertexID) : SV_Position {\n"
    "  return float4(id == 1 ? 3.0 : -1.0, id == 2 ? -3.0 : 1.0, 0, 1);\n"
    "}\n";
static const char ps_probe[] =
    "Texture2DMS<float4> m : register(t0);\n"
    "cbuffer P : register(b1) { uint4 s; };\n"
    "float4 main(float4 p : SV_Position) : SV_Target { return m.Load(int2(p.xy), s.x); }\n";

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

static int draw(int triangle, float z, unsigned rgba) {
  D3D11_MAPPED_SUBRESOURCE map;
  float *f;
  HRESULT hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)cb, 0,
                                       D3D11_MAP_WRITE_DISCARD, 0, &map);
  if (FAILED(hr)) { say("CB_MAP 0x%08lx", (unsigned long)hr); return 0; }
  f = (float *)map.pData;
  memset(f, 0, 48);
  f[0] = -1.0f; f[1] = -1.0f; f[2] = 1.0f; f[3] = 1.0f;
  f[4] = z; f[5] = triangle ? 1.0f : 0.0f;
  for (int c = 0; c < 4; ++c) f[8 + c] = (float)((rgba >> (8 * c)) & 0xffu) / 255.0f;
  ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)cb, 0);
  ID3D11DeviceContext_Draw(ctx, triangle ? 3 : 6, 0);
  return 1;
}

static DXGI_FORMAT target_format = DXGI_FORMAT_R8G8B8A8_UNORM;

/* B8G8R8A8 memory read as a 32-bit value, in R8G8B8A8 order. */
static unsigned rgba(unsigned px) {
  return target_format == DXGI_FORMAT_B8G8R8A8_UNORM ?
      (px & 0xff00ff00u) | ((px & 0xffu) << 16) | ((px >> 16) & 0xffu) : px;
}

int main(int argc, char **argv) {
  IDXGIFactory1 *factory = NULL;
  IDXGIAdapter1 *adapter = NULL, *pick = NULL;
  ID3D11Device *device = NULL;
  ID3D11Texture2D *msaa = NULL, *depth = NULL, *resolved = NULL, *staging = NULL;
  ID3D11RenderTargetView *rtv = NULL;
  ID3D11DepthStencilView *dsv = NULL;
  ID3D11DepthStencilState *less = NULL;
  ID3D11RasterizerState *raster = NULL;
  ID3D11VertexShader *vs = NULL;
  ID3D11PixelShader *ps = NULL;
  ID3DBlob *vsb, *psb;
  /* the "samples" diagnostic */
  ID3D11Texture2D *probe_tex = NULL;
  ID3D11RenderTargetView *probe_rtv = NULL;
  ID3D11ShaderResourceView *msaa_srv = NULL;
  ID3D11Buffer *probe_cb = NULL;
  ID3D11VertexShader *vs_probe = NULL;
  ID3D11PixelShader *ps_probe_shader = NULL;
  int probe;
  D3D_FEATURE_LEVEL level = 0;
  const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                    D3D_FEATURE_LEVEL_10_0};
  HMODULE compiler;
  pD3DCompile compile_fn;
  HRESULT hr;
  UINT samples, quality = 0, depth_quality = 0;
  int iterations, bad = 0;

  if (argc < 2) {
    printf("usage: agx_msaa_smoke.exe <out.txt> [2|4] [iterations] [rgba|bgra] [samples]\n");
    return 2;
  }
  samples = argc > 2 && atoi(argv[2]) == 2 ? 2u : 4u;
  iterations = argc > 3 ? atoi(argv[3]) : 30;
  if (argc > 4 && !strcmp(argv[4], "bgra")) target_format = DXGI_FORMAT_B8G8R8A8_UNORM;
  probe = argc > 5 && !strcmp(argv[5], "samples");
  if (iterations < 2) iterations = 2;
  out = CreateFileA(argv[1], GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
  say("MSAA samples %u %dx%d iterations %d format %u", samples, SIZE, SIZE, iterations,
      (unsigned)target_format);
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
  ID3D11Device_CheckMultisampleQualityLevels(device, target_format, samples, &quality);
  ID3D11Device_CheckMultisampleQualityLevels(device, DXGI_FORMAT_D24_UNORM_S8_UINT, samples,
                                             &depth_quality);
  say("QUALITY colour %u depth %u", quality, depth_quality);
  if (!quality || !depth_quality) { say("AGX_MSAA_SMOKE: FAIL (no multisample support)"); return 1; }
  {
    D3D11_TEXTURE2D_DESC td;
    D3D11_BUFFER_DESC bd;
    D3D11_DEPTH_STENCIL_DESC dd;
    D3D11_RASTERIZER_DESC rd;
    D3D11_RENDER_TARGET_VIEW_DESC rv;
    D3D11_DEPTH_STENCIL_VIEW_DESC dv;
    memset(&td, 0, sizeof(td));
    td.Width = td.Height = SIZE; td.MipLevels = td.ArraySize = 1;
    td.Format = target_format; td.SampleDesc.Count = samples;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | (probe ? D3D11_BIND_SHADER_RESOURCE : 0);
    hr = ID3D11Device_CreateTexture2D(device, &td, NULL, &msaa);
    say("CreateTexture2D msaa 0x%08lx", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    memset(&rv, 0, sizeof(rv));
    rv.Format = td.Format; rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
    hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)msaa, &rv, &rtv);
    td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateTexture2D(device, &td, NULL, &depth);
    memset(&dv, 0, sizeof(dv));
    dv.Format = td.Format; dv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateDepthStencilView(device, (ID3D11Resource *)depth, &dv, &dsv);
    say("VIEWS 0x%08lx", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    td.Format = target_format; td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    hr = ID3D11Device_CreateTexture2D(device, &td, NULL, &resolved);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateTexture2D(device, &td, NULL, &staging);
    memset(&dd, 0, sizeof(dd));
    dd.DepthEnable = TRUE; dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc = D3D11_COMPARISON_LESS;
    dd.FrontFace.StencilFailOp = dd.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    dd.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
    dd.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS; dd.BackFace = dd.FrontFace;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateDepthStencilState(device, &dd, &less);
    memset(&rd, 0, sizeof(rd));
    rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
    rd.MultisampleEnable = TRUE;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateRasterizerState(device, &rd, &raster);
    memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = 48; bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateBuffer(device, &bd, NULL, &cb);
    say("RESOURCES 0x%08lx", (unsigned long)hr);
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
  if (probe) {
    D3D11_TEXTURE2D_DESC td;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd;
    D3D11_BUFFER_DESC bd;
    ID3DBlob *a = compile(compile_fn, vs_full, sizeof(vs_full) - 1, "vs_4_0");
    ID3DBlob *b = compile(compile_fn, ps_probe, sizeof(ps_probe) - 1, "ps_4_0");
    if (!a || !b) return 1;
    hr = ID3D11Device_CreateVertexShader(device, ID3D10Blob_GetBufferPointer(a),
                                         ID3D10Blob_GetBufferSize(a), NULL, &vs_probe);
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(b),
                                         ID3D10Blob_GetBufferSize(b), NULL, &ps_probe_shader);
    memset(&td, 0, sizeof(td));
    td.Width = td.Height = SIZE; td.MipLevels = td.ArraySize = 1;
    td.Format = target_format; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateTexture2D(device, &td, NULL, &probe_tex);
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)probe_tex,
                                                               NULL, &probe_rtv);
    memset(&sd, 0, sizeof(sd));
    sd.Format = target_format; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DMS;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateShaderResourceView(device, (ID3D11Resource *)msaa,
                                                                 &sd, &msaa_srv);
    memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = 16; bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (SUCCEEDED(hr)) hr = ID3D11Device_CreateBuffer(device, &bd, NULL, &probe_cb);
    say("PROBE 0x%08lx", (unsigned long)hr);
    if (FAILED(hr)) return 1;
  }
  {
    D3D11_VIEWPORT vp;
    memset(&vp, 0, sizeof(vp));
    vp.Width = vp.Height = (float)SIZE; vp.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
  }
  ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, dsv);
  ID3D11DeviceContext_OMSetDepthStencilState(ctx, less, 0);
  ID3D11DeviceContext_RSSetState(ctx, raster);
  ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0);
  ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
  ID3D11DeviceContext_VSSetConstantBuffers(ctx, 0, 1, &cb);
  ID3D11DeviceContext_PSSetConstantBuffers(ctx, 0, 1, &cb);
  for (int i = 0; i < iterations; ++i) {
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    D3D11_MAPPED_SUBRESOURCE map;
    unsigned lower, upper;
    int mixed = 0;
    ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, black);
    ID3D11DeviceContext_ClearDepthStencilView(ctx, dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                              1.0f, 0);
    if (!draw(1, 0.5f, 0xff00ff00u) || !draw(0, 0.6f, 0xff0000ffu)) return 1;
    ID3D11DeviceContext_ResolveSubresource(ctx, (ID3D11Resource *)resolved, 0,
                                           (ID3D11Resource *)msaa, 0,
                                           target_format);
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging, (ID3D11Resource *)resolved);
    hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map);
    if (FAILED(hr)) {
      say("READBACK_MAP 0x%08lx at %d removed 0x%08lx", (unsigned long)hr, i,
          (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
      return 1;
    }
    lower = rgba(*(const unsigned *)((const unsigned char *)map.pData + 224u * map.RowPitch + 32u * 4u));
    upper = rgba(*(const unsigned *)((const unsigned char *)map.pData + 32u * map.RowPitch + 224u * 4u));
    /* The edge runs from the top-left to the bottom-right corner. */
    for (int y = 8; y < SIZE - 8; ++y) {
      for (int dx = -1; dx <= 1; ++dx) {
        unsigned px = rgba(*(const unsigned *)((const unsigned char *)map.pData +
            (size_t)y * map.RowPitch + (size_t)(y + dx) * 4u));
        unsigned r = px & 0xffu, g = (px >> 8) & 0xffu;
        if (r > 8u && r < 247u && g > 8u && g < 247u) { ++mixed; break; }
      }
    }
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
    if (probe && (i == 0 || i == iterations - 1)) {
      ID3D11ShaderResourceView *none = NULL;
      for (UINT s = 0; s < samples; ++s) {
        D3D11_MAPPED_SUBRESOURCE m;
        hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)probe_cb, 0, D3D11_MAP_WRITE_DISCARD,
                                     0, &m);
        if (FAILED(hr)) { say("PROBE_CB_MAP 0x%08lx", (unsigned long)hr); return 1; }
        memset(m.pData, 0, 16);
        *(UINT *)m.pData = s;
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)probe_cb, 0);
        ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &probe_rtv, NULL);
        ID3D11DeviceContext_VSSetShader(ctx, vs_probe, NULL, 0);
        ID3D11DeviceContext_PSSetShader(ctx, ps_probe_shader, NULL, 0);
        ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 1, &msaa_srv);
        ID3D11DeviceContext_PSSetConstantBuffers(ctx, 1, 1, &probe_cb);
        ID3D11DeviceContext_Draw(ctx, 3, 0);
        ID3D11DeviceContext_PSSetShaderResources(ctx, 0, 1, &none);
        ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging, (ID3D11Resource *)probe_tex);
        hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &m);
        if (FAILED(hr)) { say("PROBE_MAP 0x%08lx", (unsigned long)hr); return 1; }
        say("SAMPLE %u iteration %d lower 0x%08x upper 0x%08x diagonal 0x%08x", s, i,
            rgba(*(const unsigned *)((const unsigned char *)m.pData + 224u * m.RowPitch + 32u * 4u)),
            rgba(*(const unsigned *)((const unsigned char *)m.pData + 32u * m.RowPitch + 224u * 4u)),
            rgba(*(const unsigned *)((const unsigned char *)m.pData + 128u * m.RowPitch + 128u * 4u)));
        ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
      }
      ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, dsv);
      ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0);
      ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
    }
    if (lower != 0xff00ff00u || upper != 0xff0000ffu || mixed < (SIZE - 16) / 2) {
      if (bad < 8)
        say("WRONG iteration %d lower 0x%08x upper 0x%08x mixed-edge rows %d", i, lower, upper,
            mixed);
      ++bad;
    }
    if (i < 2 || i == iterations - 1)
      say("ITERATION %d bad %d lower 0x%08x upper 0x%08x mixed-edge rows %d removed 0x%08lx", i,
          bad, lower, upper, mixed, (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
  }
  say("RESULT samples %u iterations %d bad %d removed 0x%08lx", samples, iterations, bad,
      (unsigned long)ID3D11Device_GetDeviceRemovedReason(device));
  say(bad ? "AGX_MSAA_SMOKE: FAIL" : "AGX_MSAA_SMOKE: PASS");
  return bad ? 1 : 0;
}
