/* D3D11 feature level 11_1 probe (investigation/analysis/D3D12-FL-program.md,
 * "RESOLVED: the levels GravityMark requests"). One fresh device per step;
 * each step prints AGX_D3D11_<STEP>: PASS|FAIL|SKIP.
 * D11LOGIC: D3D11.1 output-merger logic ops (ID3D11Device1::CreateBlendState1)
 * on an R8G8B8A8_UINT target cleared to 0x33: the constant source
 * 0x0f,0xf0,0x55,0xaa draws 0x3c,0xc3,0x66,0x99 with XOR and
 * 0xcf,0xfc,0xdd,0xee with OR_REVERSE (s | ~d), which catches a reordered
 * operation table.
 * D11UAVCOMPACT: a cs_5_0 dispatch with raw u1, append u3, typed u5 and raw
 * u7, which the D3D11.1 translator binds through the shader's UAV map
 * (compacted shader buffers 0..3, image 5): every view must hold its
 * thread's value, the append buffer 0..63 in any order, the counter 64.
 * D11TIR (needs FL 11_1: APPLE_AGX_D3D11_FL111=1): UAV-only rendering with
 * ForcedSampleCount 16 and 8 of thin, sub-pixel and edge-sharing triangles;
 * the pixel shader adds countbits(SV_Coverage) per pixel, which must equal
 * the reference coverage of the D3D standard patterns and the top-left rule
 * (dxbc_tir.h, the contract the expansion GS and pixel shader implement);
 * then the same draw as TIR into a single-sample R32_FLOAT render target with
 * ONE + ONE blending, which must hold the same counts.
 * Usage: agx_d3d11_fl111_probe.exe */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <string.h>

#include "agx_d3d11_fl111_vs.h"
#include "agx_d3d11_fl111_ps_logic.h"
#include "agx_d3d11_fl111_cs_compact.h"
#include "agx_d3d11_fl111_vs_tir.h"
#include "agx_d3d11_fl111_ps_tir.h"
#include "agx_d3d11_fl111_ps_tirrt.h"
#include "dxbc_tir.h"

static IDXGIAdapter1 *hardware_adapter(void) {
  IDXGIFactory1 *factory = NULL;
  IDXGIAdapter1 *adapter = NULL, *pick = NULL;
  if (FAILED(CreateDXGIFactory1(&IID_IDXGIFactory1, (void **)&factory))) return NULL;
  for (UINT i = 0; IDXGIFactory1_EnumAdapters1(factory, i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 d;
    IDXGIAdapter1_GetDesc1(adapter, &d);
    if (!pick && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) { pick = adapter; continue; }
    IDXGIAdapter1_Release(adapter);
  }
  IDXGIFactory1_Release(factory);
  return pick;
}

/* The highest of 11_1, 11_0 the driver accepts. */
static HRESULT create_device(ID3D11Device1 **device1, ID3D11DeviceContext **ctx, D3D_FEATURE_LEVEL *level) {
  const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  IDXGIAdapter1 *adapter = hardware_adapter();
  ID3D11Device *device = NULL;
  HRESULT hr;
  if (!adapter) return DXGI_ERROR_NOT_FOUND;
  hr = D3D11CreateDevice((IDXGIAdapter *)adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, want, 2,
                         D3D11_SDK_VERSION, &device, level, ctx);
  IDXGIAdapter1_Release(adapter);
  if (FAILED(hr)) return hr;
  hr = ID3D11Device_QueryInterface(device, &IID_ID3D11Device1, (void **)device1);
  ID3D11Device_Release(device);
  return hr;
}

/* Draws the constant source through one logic op onto a target cleared to
 * 0x33 and counts the texels that differ from want; op -1 only clears. */
static unsigned logic_draw(ID3D11Device1 *device, ID3D11DeviceContext *ctx, int op,
                           const unsigned char want[4], HRESULT *status) {
  D3D11_TEXTURE2D_DESC td;
  D3D11_BLEND_DESC1 bd;
  D3D11_MAPPED_SUBRESOURCE map;
  ID3D11Texture2D *target = NULL, *staging = NULL;
  ID3D11RenderTargetView *rtv = NULL;
  ID3D11BlendState1 *blend = NULL;
  ID3D11VertexShader *vs = NULL;
  ID3D11PixelShader *ps = NULL;
  D3D11_VIEWPORT vp = {0, 0, 64, 64, 0, 1};
  const FLOAT clear_f[4] = {51.0f, 51.0f, 51.0f, 51.0f};
  unsigned bad = 4096;
  HRESULT hr;
  memset(&td, 0, sizeof(td));
  td.Width = 64; td.Height = 64; td.MipLevels = 1; td.ArraySize = 1;
  td.Format = DXGI_FORMAT_R8G8B8A8_UINT; td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
  hr = ID3D11Device1_CreateTexture2D(device, &td, NULL, &target);
  if (SUCCEEDED(hr)) {
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = ID3D11Device1_CreateTexture2D(device, &td, NULL, &staging);
  }
  if (SUCCEEDED(hr)) hr = ID3D11Device1_CreateRenderTargetView(device, (ID3D11Resource *)target, NULL, &rtv);
  if (SUCCEEDED(hr) && op >= 0) {
    memset(&bd, 0, sizeof(bd));
    bd.RenderTarget[0].LogicOpEnable = TRUE;
    bd.RenderTarget[0].LogicOp = (D3D11_LOGIC_OP)op;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = ID3D11Device1_CreateBlendState1(device, &bd, &blend);
    printf("D11LOGIC op %d CreateBlendState1 0x%08lx\n", op, (unsigned long)hr);
  }
  if (SUCCEEDED(hr) && op >= 0) hr = ID3D11Device1_CreateVertexShader(device, g_agx_d3d11_fl111_vs,
                                                           sizeof(g_agx_d3d11_fl111_vs), NULL, &vs);
  if (SUCCEEDED(hr) && op >= 0) hr = ID3D11Device1_CreatePixelShader(device, g_agx_d3d11_fl111_ps_logic,
                                                          sizeof(g_agx_d3d11_fl111_ps_logic), NULL, &ps);
  if (SUCCEEDED(hr)) {
    /* an integer target clears with the float values converted (D3D11) */
    ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, clear_f);
    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL);
    ID3D11DeviceContext_OMSetBlendState(ctx, (ID3D11BlendState *)blend, NULL, 0xffffffffu);
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
    if (op >= 0) ID3D11DeviceContext_Draw(ctx, 3, 0);
    ID3D11DeviceContext_CopyResource(ctx, (ID3D11Resource *)staging, (ID3D11Resource *)target);
    hr = ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map);
  }
  if (SUCCEEDED(hr)) {
    const unsigned char *row = (const unsigned char *)map.pData;
    bad = 0;
    for (UINT y = 0; y < 64; ++y)
      for (UINT x = 0; x < 64; ++x)
        if (memcmp(row + y * map.RowPitch + x * 4, want, 4)) ++bad;
    printf("D11LOGIC op %d texel0 %u,%u,%u,%u want %u,%u,%u,%u bad %u\n", op, row[0], row[1], row[2],
           row[3], want[0], want[1], want[2], want[3], bad);
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
  }
  ID3D11DeviceContext_ClearState(ctx);
  if (ps) ID3D11PixelShader_Release(ps);
  if (vs) ID3D11VertexShader_Release(vs);
  if (blend) ID3D11BlendState1_Release(blend);
  if (rtv) ID3D11RenderTargetView_Release(rtv);
  if (staging) ID3D11Texture2D_Release(staging);
  if (target) ID3D11Texture2D_Release(target);
  *status = hr;
  return bad;
}

static int step_logic(void) {
  static const unsigned char clear_want[4] = {0x33, 0x33, 0x33, 0x33};
  static const unsigned char xor_want[4] = {0x3c, 0xc3, 0x66, 0x99};
  static const unsigned char or_reverse_want[4] = {0xcf, 0xfc, 0xdd, 0xee};
  ID3D11Device1 *device = NULL;
  ID3D11DeviceContext *ctx = NULL;
  D3D_FEATURE_LEVEL level = 0;
  D3D11_FEATURE_DATA_D3D11_OPTIONS options;
  unsigned bad = 1;
  HRESULT hr = create_device(&device, &ctx, &level);
  printf("D11LOGIC device 0x%08lx level 0x%x\n", (unsigned long)hr, (unsigned)level);
  memset(&options, 0, sizeof(options));
  if (SUCCEEDED(hr)) hr = ID3D11Device1_CheckFeatureSupport(device, D3D11_FEATURE_D3D11_OPTIONS, &options,
                                                            sizeof(options));
  printf("D11LOGIC OutputMergerLogicOp %d hr 0x%08lx\n", options.OutputMergerLogicOp, (unsigned long)hr);
  if (SUCCEEDED(hr) && !options.OutputMergerLogicOp) {
    printf("AGX_D3D11_D11LOGIC: SKIP (cap absent)\n");
    goto out;
  }
  /* the clear alone first: a wrong integer clear is not a logic-op failure */
  if (SUCCEEDED(hr)) bad = logic_draw(device, ctx, -1, clear_want, &hr);
  if (SUCCEEDED(hr) && !bad) bad = logic_draw(device, ctx, D3D11_LOGIC_OP_XOR, xor_want, &hr);
  if (SUCCEEDED(hr) && !bad) bad = logic_draw(device, ctx, D3D11_LOGIC_OP_OR_REVERSE, or_reverse_want, &hr);
  if (SUCCEEDED(hr)) hr = ID3D11Device1_GetDeviceRemovedReason(device);
  printf("AGX_D3D11_D11LOGIC: %s hr=0x%08lx bad=%u\n", SUCCEEDED(hr) && !bad ? "PASS" : "FAIL",
         (unsigned long)hr, bad);
out:
  if (ctx) ID3D11DeviceContext_Release(ctx);
  if (device) ID3D11Device1_Release(device);
  return SUCCEEDED(hr) && !bad ? 0 : 1;
}

static ID3D11Buffer *buffer(ID3D11Device1 *device, UINT bytes, UINT bind, UINT misc, UINT stride,
                            D3D11_USAGE usage, UINT cpu) {
  D3D11_BUFFER_DESC bd;
  ID3D11Buffer *b = NULL;
  memset(&bd, 0, sizeof(bd));
  bd.ByteWidth = bytes; bd.Usage = usage; bd.BindFlags = bind; bd.CPUAccessFlags = cpu;
  bd.MiscFlags = misc; bd.StructureByteStride = stride;
  return SUCCEEDED(ID3D11Device1_CreateBuffer(device, &bd, NULL, &b)) ? b : NULL;
}

/* Copies a resource to its staging twin and maps it (NULL on failure). */
static const unsigned *read_back(ID3D11DeviceContext *ctx, ID3D11Resource *src, ID3D11Resource *staging,
                                 UINT *pitch) {
  D3D11_MAPPED_SUBRESOURCE map;
  ID3D11DeviceContext_CopyResource(ctx, staging, src);
  if (FAILED(ID3D11DeviceContext_Map(ctx, staging, 0, D3D11_MAP_READ, 0, &map))) return NULL;
  if (pitch) *pitch = map.RowPitch;
  return (const unsigned *)map.pData;
}

static int step_uav_compact(void) {
  ID3D11Device1 *device = NULL;
  ID3D11DeviceContext *ctx = NULL;
  D3D_FEATURE_LEVEL level = 0;
  ID3D11Buffer *raw1 = NULL, *app3 = NULL, *raw7 = NULL, *count = NULL;
  ID3D11Buffer *s_raw1 = NULL, *s_app3 = NULL, *s_raw7 = NULL, *s_count = NULL;
  ID3D11Texture2D *tex5 = NULL, *s_tex5 = NULL;
  ID3D11UnorderedAccessView *uavs[8] = {0};
  ID3D11ComputeShader *cs = NULL;
  unsigned bad = 1, counter = 0, sum = 0, mask_lo = 0, mask_hi = 0;
  HRESULT hr = create_device(&device, &ctx, &level);
  printf("D11UAVCOMPACT device 0x%08lx level 0x%x\n", (unsigned long)hr, (unsigned)level);
  if (SUCCEEDED(hr)) {
    raw1 = buffer(device, 256, D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0,
                  D3D11_USAGE_DEFAULT, 0);
    raw7 = buffer(device, 16, D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0,
                  D3D11_USAGE_DEFAULT, 0);
    app3 = buffer(device, 256, D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4,
                  D3D11_USAGE_DEFAULT, 0);
    count = buffer(device, 16, 0, 0, 0, D3D11_USAGE_DEFAULT, 0);
    s_raw1 = buffer(device, 256, 0, 0, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ);
    s_raw7 = buffer(device, 16, 0, 0, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ);
    s_app3 = buffer(device, 256, 0, 0, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ);
    s_count = buffer(device, 16, 0, 0, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ);
    if (!raw1 || !raw7 || !app3 || !count || !s_raw1 || !s_raw7 || !s_app3 || !s_count) hr = E_OUTOFMEMORY;
  }
  if (SUCCEEDED(hr)) {
    D3D11_TEXTURE2D_DESC td;
    memset(&td, 0, sizeof(td));
    td.Width = 8; td.Height = 8; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R32_UINT;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    hr = ID3D11Device1_CreateTexture2D(device, &td, NULL, &tex5);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (SUCCEEDED(hr)) hr = ID3D11Device1_CreateTexture2D(device, &td, NULL, &s_tex5);
  }
  if (SUCCEEDED(hr)) {
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud;
    memset(&ud, 0, sizeof(ud));
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Format = DXGI_FORMAT_R32_TYPELESS;
    ud.Buffer.NumElements = 64; ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    hr = ID3D11Device1_CreateUnorderedAccessView(device, (ID3D11Resource *)raw1, &ud, &uavs[1]);
    ud.Buffer.NumElements = 4;
    if (SUCCEEDED(hr)) hr = ID3D11Device1_CreateUnorderedAccessView(device, (ID3D11Resource *)raw7, &ud, &uavs[7]);
    ud.Format = DXGI_FORMAT_UNKNOWN; ud.Buffer.NumElements = 64; ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
    if (SUCCEEDED(hr)) hr = ID3D11Device1_CreateUnorderedAccessView(device, (ID3D11Resource *)app3, &ud, &uavs[3]);
    if (SUCCEEDED(hr)) hr = ID3D11Device1_CreateUnorderedAccessView(device, (ID3D11Resource *)tex5, NULL, &uavs[5]);
    if (SUCCEEDED(hr)) hr = ID3D11Device1_CreateComputeShader(device, g_agx_d3d11_fl111_cs_compact,
                                                              sizeof(g_agx_d3d11_fl111_cs_compact), NULL, &cs);
    if (SUCCEEDED(hr)) {
      const UINT clear[4] = {0, 0, 0, 0};
      UINT initial[8];
      for (UINT i = 0; i < 8; ++i) initial[i] = ~0u;
      initial[3] = 0;
      ID3D11DeviceContext_ClearUnorderedAccessViewUint(ctx, uavs[7], clear);
      ID3D11DeviceContext_CSSetShader(ctx, cs, NULL, 0);
      ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 8, uavs, initial);
      ID3D11DeviceContext_Dispatch(ctx, 1, 1, 1);
      ID3D11DeviceContext_CopyStructureCount(ctx, count, 0, uavs[3]);
    }
    printf("D11UAVCOMPACT views+dispatch 0x%08lx\n", (unsigned long)hr);
  }
  if (SUCCEEDED(hr)) {
    const unsigned *v;
    UINT pitch = 0;
    bad = 0;
    if ((v = read_back(ctx, (ID3D11Resource *)raw1, (ID3D11Resource *)s_raw1, NULL))) {
      for (unsigned i = 0; i < 64; ++i) if (v[i] != i + 100) ++bad;
      printf("D11UAVCOMPACT raw1[0,63] %u,%u\n", v[0], v[63]);
      ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)s_raw1, 0);
    } else ++bad;
    if ((v = read_back(ctx, (ID3D11Resource *)tex5, (ID3D11Resource *)s_tex5, &pitch))) {
      for (unsigned y = 0; y < 8; ++y)
        for (unsigned x = 0; x < 8; ++x)
          if (((const unsigned *)((const char *)v + y * pitch))[x] != (y * 8 + x) * 3) ++bad;
      printf("D11UAVCOMPACT tex5[7,7] %u\n", ((const unsigned *)((const char *)v + 7 * pitch))[7]);
      ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)s_tex5, 0);
    } else ++bad;
    if ((v = read_back(ctx, (ID3D11Resource *)raw7, (ID3D11Resource *)s_raw7, NULL))) {
      if (v[0] != 64) ++bad;
      printf("D11UAVCOMPACT raw7[0] %u\n", v[0]);
      ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)s_raw7, 0);
    } else ++bad;
    if ((v = read_back(ctx, (ID3D11Resource *)count, (ID3D11Resource *)s_count, NULL))) {
      counter = v[0];
      ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)s_count, 0);
    }
    if (counter != 64) ++bad;
    /* the appended values: each of 0..63 exactly once, in any order */
    if ((v = read_back(ctx, (ID3D11Resource *)app3, (ID3D11Resource *)s_app3, NULL))) {
      for (unsigned i = 0; i < 64; ++i) {
        sum += v[i];
        if (v[i] < 32) mask_lo |= 1u << v[i];
        else if (v[i] < 64) mask_hi |= 1u << (v[i] - 32);
      }
      ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)s_app3, 0);
    }
    if (mask_lo != ~0u || mask_hi != ~0u) ++bad;
    printf("D11UAVCOMPACT counter %u append-sum %u masks %08x,%08x (want 64, 2016, all)\n", counter, sum,
           mask_lo, mask_hi);
    hr = ID3D11Device1_GetDeviceRemovedReason(device);
  }
  printf("AGX_D3D11_D11UAVCOMPACT: %s hr=0x%08lx bad=%u\n", SUCCEEDED(hr) && !bad ? "PASS" : "FAIL",
         (unsigned long)hr, bad);
  for (UINT i = 0; i < 8; ++i) if (uavs[i]) ID3D11UnorderedAccessView_Release(uavs[i]);
  if (cs) ID3D11ComputeShader_Release(cs);
  if (tex5) ID3D11Texture2D_Release(tex5);
  if (s_tex5) ID3D11Texture2D_Release(s_tex5);
  if (raw1) ID3D11Buffer_Release(raw1);
  if (raw7) ID3D11Buffer_Release(raw7);
  if (app3) ID3D11Buffer_Release(app3);
  if (count) ID3D11Buffer_Release(count);
  if (s_raw1) ID3D11Buffer_Release(s_raw1);
  if (s_raw7) ID3D11Buffer_Release(s_raw7);
  if (s_app3) ID3D11Buffer_Release(s_app3);
  if (s_count) ID3D11Buffer_Release(s_count);
  if (ctx) ID3D11DeviceContext_Release(ctx);
  if (device) ID3D11Device1_Release(device);
  return SUCCEEDED(hr) && !bad ? 0 : 1;
}

/* D11TIR triangles in pixels of the 64x64 viewport (multiples of 1/256): a
 * quad split along a diagonal (the shared edge covers each sample once),
 * a thin sliver, a sub-pixel triangle and one with sample-aligned edges. */
static const float tir_tris[][6] = {
  {4.0f, 4.0f, 28.0f, 4.0f, 28.0f, 20.0f}, {4.0f, 4.0f, 28.0f, 20.0f, 4.0f, 20.0f},
  {32.25f, 2.5f, 60.0f, 3.125f, 33.0f, 4.0f},
  {40.3125f, 30.6875f, 40.875f, 30.5f, 40.5f, 31.25f},
  {8.5625f, 40.6875f, 30.4375f, 40.6875f, 8.5625f, 60.5f},
  {50.0f, 40.0f, 44.0f, 60.0f, 62.0f, 52.0f}};
#define TIR_TRIS (sizeof(tir_tris) / sizeof(tir_tris[0]))

static unsigned tir_draw(ID3D11Device1 *device, ID3D11DeviceContext *ctx, UINT samples, BOOL rt, HRESULT *status) {
  ID3D11Buffer *vb = NULL, *counts = NULL, *staging = NULL;
  ID3D11UnorderedAccessView *uav = NULL;
  ID3D11VertexShader *vs = NULL;
  ID3D11PixelShader *ps = NULL;
  ID3D11InputLayout *layout = NULL;
  ID3D11RasterizerState1 *rs = NULL;
  ID3D11Texture2D *target = NULL, *target_staging = NULL;
  ID3D11RenderTargetView *rtv = NULL;
  ID3D11BlendState *blend = NULL;
  float verts[TIR_TRIS * 6];
  unsigned want[64 * 64], bad = 4096, total = 0;
  D3D11_VIEWPORT vp = {0, 0, 64, 64, 0, 1};
  HRESULT hr = S_OK;
  memset(want, 0, sizeof(want));
  for (unsigned i = 0; i < TIR_TRIS; ++i) {
    int32_t x[3], y[3];
    struct dxbc_tir_edges e;
    for (unsigned k = 0; k < 3; ++k) {
      verts[i * 6 + k * 2] = tir_tris[i][k * 2] / 32.0f - 1.0f;
      verts[i * 6 + k * 2 + 1] = 1.0f - tir_tris[i][k * 2 + 1] / 32.0f;
      x[k] = (int32_t)(tir_tris[i][k * 2] * 256.0f);
      y[k] = (int32_t)(tir_tris[i][k * 2 + 1] * 256.0f);
    }
    if (!dxbc_tir_setup(x, y, &e)) continue;
    for (int py = 0; py < 64; ++py)
      for (int px = 0; px < 64; ++px) {
        unsigned m = dxbc_tir_mask(&e, px, py, samples), n = 0;
        for (; m; m &= m - 1) ++n;
        want[py * 64 + px] += n;
        total += n;
      }
  }
  {
    D3D11_SUBRESOURCE_DATA init = {verts, 0, 0};
    D3D11_BUFFER_DESC bd;
    memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = sizeof(verts); bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    hr = ID3D11Device1_CreateBuffer(device, &bd, &init, &vb);
  }
  if (SUCCEEDED(hr)) {
    counts = buffer(device, sizeof(want), D3D11_BIND_UNORDERED_ACCESS, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0,
                    D3D11_USAGE_DEFAULT, 0);
    staging = buffer(device, sizeof(want), 0, 0, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ);
    if (!counts || !staging) hr = E_OUTOFMEMORY;
  }
  if (SUCCEEDED(hr)) {
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud;
    memset(&ud, 0, sizeof(ud));
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; ud.Format = DXGI_FORMAT_R32_TYPELESS;
    ud.Buffer.NumElements = 64 * 64; ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    hr = ID3D11Device1_CreateUnorderedAccessView(device, (ID3D11Resource *)counts, &ud, &uav);
  }
  if (SUCCEEDED(hr)) hr = ID3D11Device1_CreateVertexShader(device, g_agx_d3d11_fl111_vs_tir, sizeof(g_agx_d3d11_fl111_vs_tir),
                                                           NULL, &vs);
  if (SUCCEEDED(hr))
    hr = rt ? ID3D11Device1_CreatePixelShader(device, g_agx_d3d11_fl111_ps_tirrt, sizeof(g_agx_d3d11_fl111_ps_tirrt), NULL, &ps)
            : ID3D11Device1_CreatePixelShader(device, g_agx_d3d11_fl111_ps_tir, sizeof(g_agx_d3d11_fl111_ps_tir), NULL, &ps);
  if (SUCCEEDED(hr) && rt) {
    D3D11_TEXTURE2D_DESC td;
    D3D11_BLEND_DESC bd;
    memset(&td, 0, sizeof(td));
    td.Width = 64; td.Height = 64; td.MipLevels = 1; td.ArraySize = 1; td.Format = DXGI_FORMAT_R32_FLOAT;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = ID3D11Device1_CreateTexture2D(device, &td, NULL, &target);
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (SUCCEEDED(hr)) hr = ID3D11Device1_CreateTexture2D(device, &td, NULL, &target_staging);
    if (SUCCEEDED(hr)) hr = ID3D11Device1_CreateRenderTargetView(device, (ID3D11Resource *)target, NULL, &rtv);
    memset(&bd, 0, sizeof(bd));
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (SUCCEEDED(hr)) hr = ID3D11Device1_CreateBlendState(device, &bd, &blend);
  }
  if (SUCCEEDED(hr)) {
    const D3D11_INPUT_ELEMENT_DESC el = {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0};
    hr = ID3D11Device1_CreateInputLayout(device, &el, 1, g_agx_d3d11_fl111_vs_tir, sizeof(g_agx_d3d11_fl111_vs_tir),
                                         &layout);
  }
  if (SUCCEEDED(hr)) {
    D3D11_RASTERIZER_DESC1 rd;
    memset(&rd, 0, sizeof(rd));
    rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
    rd.ForcedSampleCount = samples;
    hr = ID3D11Device1_CreateRasterizerState1(device, &rd, &rs);
    printf("D11TIR %u CreateRasterizerState1 0x%08lx\n", samples, (unsigned long)hr);
  }
  if (SUCCEEDED(hr)) {
    const UINT clear[4] = {0, 0, 0, 0}, stride = 8, offset = 0;
    const FLOAT zero[4] = {0, 0, 0, 0};
    ID3D11DeviceContext_ClearUnorderedAccessViewUint(ctx, uav, clear);
    if (rt) {
      ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, zero);
      ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL);
      ID3D11DeviceContext_OMSetBlendState(ctx, blend, NULL, 0xffffffffu);
    } else {
      ID3D11DeviceContext_OMSetRenderTargetsAndUnorderedAccessViews(ctx, 0, NULL, NULL, 0, 1, &uav, NULL);
    }
    ID3D11DeviceContext_RSSetState(ctx, (ID3D11RasterizerState *)rs);
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
    ID3D11DeviceContext_IASetInputLayout(ctx, layout);
    ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vb, &stride, &offset);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
    ID3D11DeviceContext_Draw(ctx, (UINT)(TIR_TRIS * 3), 0);
    {
      UINT pitch = 0;
      ID3D11Resource *src = rt ? (ID3D11Resource *)target : (ID3D11Resource *)counts;
      ID3D11Resource *dst = rt ? (ID3D11Resource *)target_staging : (ID3D11Resource *)staging;
      const unsigned *v = read_back(ctx, src, dst, &pitch);
      if (v) {
        unsigned got = 0, shown = 0;
        bad = 0;
        for (unsigned i = 0; i < 64 * 64; ++i) {
          const unsigned value = rt ? (unsigned)((const float *)((const char *)v + (i / 64) * pitch))[i % 64] : v[i];
          got += value;
          if (value != want[i]) {
            if (shown++ < 6)
              printf("D11TIR %u%s pixel %u,%u got %u want %u\n", samples, rt ? " rt" : "", i % 64, i / 64, value, want[i]);
            ++bad;
          }
        }
        printf("D11TIR %u%s covered samples got %u want %u\n", samples, rt ? " rt" : "", got, total);
        ID3D11DeviceContext_Unmap(ctx, dst, 0);
      } else hr = E_FAIL;
    }
  }
  ID3D11DeviceContext_ClearState(ctx);
  if (blend) ID3D11BlendState_Release(blend);
  if (rtv) ID3D11RenderTargetView_Release(rtv);
  if (target_staging) ID3D11Texture2D_Release(target_staging);
  if (target) ID3D11Texture2D_Release(target);
  if (rs) ID3D11RasterizerState1_Release(rs);
  if (layout) ID3D11InputLayout_Release(layout);
  if (ps) ID3D11PixelShader_Release(ps);
  if (vs) ID3D11VertexShader_Release(vs);
  if (uav) ID3D11UnorderedAccessView_Release(uav);
  if (staging) ID3D11Buffer_Release(staging);
  if (counts) ID3D11Buffer_Release(counts);
  if (vb) ID3D11Buffer_Release(vb);
  *status = hr;
  return bad;
}

static int step_tir(void) {
  ID3D11Device1 *device = NULL;
  ID3D11DeviceContext *ctx = NULL;
  D3D_FEATURE_LEVEL level = 0;
  unsigned bad = 1;
  HRESULT hr = create_device(&device, &ctx, &level);
  printf("D11TIR device 0x%08lx level 0x%x\n", (unsigned long)hr, (unsigned)level);
  if (SUCCEEDED(hr) && level < D3D_FEATURE_LEVEL_11_1) {
    printf("AGX_D3D11_D11TIR: SKIP (feature level 0x%x)\n", (unsigned)level);
    ID3D11DeviceContext_Release(ctx);
    ID3D11Device1_Release(device);
    return 0;
  }
  if (SUCCEEDED(hr)) bad = tir_draw(device, ctx, 16, FALSE, &hr);
  if (SUCCEEDED(hr)) bad += tir_draw(device, ctx, 8, FALSE, &hr);
  if (SUCCEEDED(hr)) bad += tir_draw(device, ctx, 16, TRUE, &hr);
  if (SUCCEEDED(hr)) hr = ID3D11Device1_GetDeviceRemovedReason(device);
  printf("AGX_D3D11_D11TIR: %s hr=0x%08lx bad=%u\n", SUCCEEDED(hr) && !bad ? "PASS" : "FAIL", (unsigned long)hr, bad);
  if (ctx) ID3D11DeviceContext_Release(ctx);
  if (device) ID3D11Device1_Release(device);
  return SUCCEEDED(hr) && !bad ? 0 : 1;
}

int main(void) {
  int failures = 0;
  failures += step_logic();
  failures += step_uav_compact();
  failures += step_tir();
  printf("AGX_D3D11_FL111_PROBE_DONE failures=%d\n", failures);
  return failures ? 1 : 0;
}
