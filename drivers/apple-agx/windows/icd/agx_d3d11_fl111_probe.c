/* D3D11 feature level 11_1 probe (investigation/analysis/D3D12-FL-program.md,
 * "RESOLVED: the levels GravityMark requests"). One fresh device per step;
 * each step prints AGX_D3D11_<STEP>: PASS|FAIL|SKIP.
 * D11LOGIC: D3D11.1 output-merger logic ops (ID3D11Device1::CreateBlendState1)
 * on an R8G8B8A8_UINT target cleared to 0x33: the constant source
 * 0x0f,0xf0,0x55,0xaa draws 0x3c,0xc3,0x66,0x99 with XOR and
 * 0xcf,0xfc,0xdd,0xee with OR_REVERSE (s | ~d), which catches a reordered
 * operation table.
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

int main(void) {
  int failures = 0;
  failures += step_logic();
  printf("AGX_D3D11_FL111_PROBE_DONE failures=%d\n", failures);
  return failures ? 1 : 0;
}
