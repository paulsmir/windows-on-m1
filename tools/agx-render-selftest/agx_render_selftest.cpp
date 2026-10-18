// Deterministic D3D11 (FL 10_0) render self-test for the AppleAgx UMD/KMD.
// Every check reads results back through a staging copy; no display needed.
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

static const char *kShader =
"Texture2D t:register(t0); SamplerState s:register(s0);\n"
"struct V{float4 p:SV_Position; float2 uv:TEXCOORD0;};\n"
"V vs(float2 pos:POSITION, float2 uv:TEXCOORD0){V o; o.uv=uv; o.p=float4(pos,0,1); return o;}\n"
"float4 ps(V i):SV_Target{return t.Sample(s,i.uv);}\n"
"float4 solid(V i):SV_Target{return float4(0,1,0,1);}\n"
"float4 uvc(V i):SV_Target{return float4(i.uv.x,i.uv.y,0,1);}\n";

static int failures;
static void report(const char *name, bool ok, const char *detail) {
  printf("%s %s %s\n", ok ? "PASS" : "FAIL", name, detail ? detail : "");
  if (!ok) ++failures;
}

static bool readback(ID3D11Device *d, ID3D11DeviceContext *c, ID3D11Texture2D *src,
                     UINT x, UINT y, uint32_t *pixel, UINT *nonzero) {
  D3D11_TEXTURE2D_DESC desc; src->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.MiscFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ID3D11Texture2D *staging = nullptr;
  if (FAILED(d->CreateTexture2D(&desc, nullptr, &staging))) return false;
  c->CopyResource(staging, src);
  D3D11_MAPPED_SUBRESOURCE m = {};
  HRESULT hr = c->Map(staging, 0, D3D11_MAP_READ, 0, &m);
  if (FAILED(hr)) { staging->Release(); return false; }
  *pixel = ((uint32_t *)((uint8_t *)m.pData + y * m.RowPitch))[x];
  *nonzero = 0;
  for (UINT r = 0; r < desc.Height; ++r)
    for (UINT q = 0; q < desc.Width; ++q)
      if (((uint32_t *)((uint8_t *)m.pData + r * m.RowPitch))[q]) ++*nonzero;
  c->Unmap(staging, 0); staging->Release();
  return true;
}

static ID3D11Texture2D *make_texture(ID3D11Device *d, UINT w, UINT h, UINT bind,
                                     UINT misc, const uint32_t *init) {
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = bind; desc.MiscFlags = misc;
  D3D11_SUBRESOURCE_DATA data = {init, w * 4u, 0};
  ID3D11Texture2D *t = nullptr;
  HRESULT hr = d->CreateTexture2D(&desc, init ? &data : nullptr, &t);
  if (FAILED(hr)) { printf("CreateTexture2D hr=0x%08lx\n", hr); return nullptr; }
  return t;
}

int main(int argc, char **argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  printf("start\n");
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_10_0};
  ID3D11Device *d = nullptr; ID3D11DeviceContext *c = nullptr; D3D_FEATURE_LEVEL got;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1,
                                 D3D11_SDK_VERSION, &d, &got, &c);
  if (FAILED(hr)) { printf("FAIL device hr=0x%08lx\n", hr); return 2; }
  IDXGIDevice *xd = nullptr; IDXGIAdapter *ad = nullptr; DXGI_ADAPTER_DESC adesc = {};
  if (SUCCEEDED(d->QueryInterface(__uuidof(IDXGIDevice), (void **)&xd)) &&
      SUCCEEDED(xd->GetAdapter(&ad)) && SUCCEEDED(ad->GetDesc(&adesc)))
    printf("adapter %ls fl=0x%x\n", adesc.Description, got);
  printf("device ok\n");
  const UINT W = argc > 1 ? (UINT)atoi(argv[1]) : 256u, H = argc > 2 ? (UINT)atoi(argv[2]) : W;
  printf("size %ux%u\n", W, H);
  char buf[160];
  uint32_t px; UINT nz;

  // 1. Clear a render target.
  ID3D11Texture2D *rt = make_texture(d, W, H, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, nullptr);
  ID3D11RenderTargetView *rtv = nullptr;
  if (!rt || FAILED(d->CreateRenderTargetView(rt, nullptr, &rtv))) { printf("FAIL rtv\n"); return 3; }
  const float red[4] = {1, 0, 0, 1};
  c->ClearRenderTargetView(rtv, red);
  bool ok = readback(d, c, rt, W / 2, H / 2, &px, &nz);
  sprintf_s(buf, "pixel=0x%08x nonzero=%u/%u", px, nz, W * H);
  report("clear", ok && px == 0xffff0000u && nz == W * H, buf);

  // 2. Draw a texture created from CPU data.
  static uint32_t init[4096 * 4096];
  for (UINT i = 0; i < W * H; ++i) init[i] = 0xff00ff00u; // green
  printf("step tex-init\n");
  ID3D11Texture2D *tex = make_texture(d, W, H, D3D11_BIND_SHADER_RESOURCE, 0, init);
  printf("step compile\n");
  ID3D11ShaderResourceView *srv = nullptr;
  ID3DBlob *vsb = nullptr, *psb = nullptr, *err = nullptr;
  if (FAILED(D3DCompile(kShader, strlen(kShader), nullptr, nullptr, nullptr, "vs", "vs_4_0", 0, 0, &vsb, &err)) ||
      FAILED(D3DCompile(kShader, strlen(kShader), nullptr, nullptr, nullptr, "ps", "ps_4_0", 0, 0, &psb, &err))) {
    printf("FAIL compile %s\n", err ? (char *)err->GetBufferPointer() : ""); return 4;
  }
  printf("step shaders\n");
  ID3DBlob *solidb = nullptr, *uvcb = nullptr;
  D3DCompile(kShader, strlen(kShader), nullptr, nullptr, nullptr, "solid", "ps_4_0", 0, 0, &solidb, &err);
  D3DCompile(kShader, strlen(kShader), nullptr, nullptr, nullptr, "uvc", "ps_4_0", 0, 0, &uvcb, &err);
  ID3D11PixelShader *pssolid = nullptr, *psuvc = nullptr;
  ID3D11VertexShader *vs = nullptr; ID3D11PixelShader *ps = nullptr; ID3D11SamplerState *ss = nullptr;
  d->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs);
  d->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps);
  d->CreatePixelShader(solidb->GetBufferPointer(), solidb->GetBufferSize(), nullptr, &pssolid);
  d->CreatePixelShader(uvcb->GetBufferPointer(), uvcb->GetBufferSize(), nullptr, &psuvc);
  D3D11_SAMPLER_DESC sd = {}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
  sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
  d->CreateSamplerState(&sd, &ss);
  if (!tex || FAILED(d->CreateShaderResourceView(tex, nullptr, &srv)) || !vs || !ps || !ss) { printf("FAIL setup\n"); return 5; }
  // Fullscreen triangle with an explicit vertex buffer and input layout.
  const float verts[] = {-1, 1, 0, 0,   3, 1, 2, 0,   -1, -3, 0, 2};
  D3D11_BUFFER_DESC bd = {sizeof(verts), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
  D3D11_SUBRESOURCE_DATA vdata = {verts, 0, 0};
  ID3D11Buffer *vb = nullptr; ID3D11InputLayout *il = nullptr;
  const D3D11_INPUT_ELEMENT_DESC elems[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0}};
  if (FAILED(d->CreateBuffer(&bd, &vdata, &vb)) ||
      FAILED(d->CreateInputLayout(elems, 2, vsb->GetBufferPointer(), vsb->GetBufferSize(), &il))) {
    printf("FAIL vb/il\n"); return 6;
  }
  const UINT stride = 16, voff = 0;
  printf("step draw\n");
  D3D11_VIEWPORT vp = {0, 0, (float)W, (float)H, 0, 1};
  auto draw = [&](ID3D11ShaderResourceView *view) {
    c->OMSetRenderTargets(1, &rtv, nullptr); c->RSSetViewports(1, &vp);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->IASetInputLayout(il); c->IASetVertexBuffers(0, 1, &vb, &stride, &voff);
    c->VSSetShader(vs, nullptr, 0); c->PSSetShader(ps, nullptr, 0);
    c->PSSetShaderResources(0, 1, &view); c->PSSetSamplers(0, 1, &ss);
    c->Draw(3, 0);
  };
  const float black[4] = {0, 0, 0, 0};
  // Explicit pipeline state (argv[3]=="explicit"): blend write-all, no cull,
  // depth disabled. Distinguishes default-state bugs from draw execution.
  const char *mode = argc > 3 ? argv[3] : "";
  bool want_blend = !strcmp(mode, "explicit") || !strcmp(mode, "blend");
  bool want_raster = !strcmp(mode, "explicit") || !strcmp(mode, "raster");
  bool want_depth = !strcmp(mode, "explicit") || !strcmp(mode, "depth");
  if (want_blend || want_raster || want_depth) {
    D3D11_BLEND_DESC bdsc = {}; bdsc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    bdsc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE; bdsc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
    bdsc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD; bdsc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bdsc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO; bdsc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    ID3D11BlendState *bs = nullptr; d->CreateBlendState(&bdsc, &bs);
    D3D11_RASTERIZER_DESC rd = {}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
    ID3D11RasterizerState *rs = nullptr; d->CreateRasterizerState(&rd, &rs);
    D3D11_DEPTH_STENCIL_DESC dd = {}; dd.DepthEnable = FALSE; dd.StencilEnable = FALSE;
    ID3D11DepthStencilState *ds = nullptr; d->CreateDepthStencilState(&dd, &ds);
    const float bf[4] = {0, 0, 0, 0};
    if (want_blend) c->OMSetBlendState(bs, bf, 0xffffffffu);
    if (want_raster) c->RSSetState(rs);
    if (want_depth) c->OMSetDepthStencilState(ds, 0);
    printf("state mode=%s blend=%d raster=%d depth=%d\n", mode, want_blend, want_raster, want_depth);
  }
  // 2a. Solid-colour pixel shader over a red clear (does the draw run at all?).
  {
    c->ClearRenderTargetView(rtv, red);
    c->OMSetRenderTargets(1, &rtv, nullptr); c->RSSetViewports(1, &vp);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->IASetInputLayout(il); c->IASetVertexBuffers(0, 1, &vb, &stride, &voff);
    c->VSSetShader(vs, nullptr, 0); c->PSSetShader(pssolid, nullptr, 0);
    c->Draw(3, 0);
    ok = readback(d, c, rt, W / 2, H / 2, &px, &nz);
    sprintf_s(buf, "pixel=0x%08x nonzero=%u/%u", px, nz, W * H);
    report("draw-solid-over-red", ok && px == 0xff00ff00u, buf);
    c->ClearRenderTargetView(rtv, red); c->PSSetShader(psuvc, nullptr, 0); c->Draw(3, 0);
    ok = readback(d, c, rt, W * 3 / 4, H / 4, &px, &nz);
    sprintf_s(buf, "pixel@200,50=0x%08x nonzero=%u/%u", px, nz, W * H);
    report("draw-uv-gradient", ok && px != 0xffff0000u && (px & 0xff000000u) == 0xff000000u, buf);
  }
  c->ClearRenderTargetView(rtv, black); draw(srv);
  printf("step readback\n");
  ok = readback(d, c, rt, W / 2, H / 2, &px, &nz);
  sprintf_s(buf, "pixel=0x%08x nonzero=%u/%u", px, nz, W * H);
  report("sample-initial-data", ok && px == 0xff00ff00u && nz == W * H, buf);

  // 3. UpdateSubresource into the texture, draw again.
  for (UINT i = 0; i < W * H; ++i) init[i] = 0xff0000ffu; // blue
  c->UpdateSubresource(tex, 0, nullptr, init, W * 4u, 0);
  c->ClearRenderTargetView(rtv, black); draw(srv);
  ok = readback(d, c, rt, W / 2, H / 2, &px, &nz);
  sprintf_s(buf, "pixel=0x%08x nonzero=%u/%u", px, nz, W * H);
  report("sample-update-subresource", ok && px == 0xff0000ffu && nz == W * H, buf);

  // 4. Render-to-texture then sample it (RT -> SRV on the same device).
  ID3D11Texture2D *rt2 = make_texture(d, W, H, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, nullptr);
  ID3D11RenderTargetView *rtv2 = nullptr; ID3D11ShaderResourceView *srv2 = nullptr;
  d->CreateRenderTargetView(rt2, nullptr, &rtv2); d->CreateShaderResourceView(rt2, nullptr, &srv2);
  const float yellow[4] = {1, 1, 0, 1};
  c->ClearRenderTargetView(rtv2, yellow);
  c->ClearRenderTargetView(rtv, black); draw(srv2);
  ok = readback(d, c, rt, W / 2, H / 2, &px, &nz);
  sprintf_s(buf, "pixel=0x%08x nonzero=%u/%u", px, nz, W * H);
  report("sample-render-target", ok && px == 0xffffff00u && nz == W * H, buf);

  // 5. Shared texture written on device A, sampled on device B.
  ID3D11Device *d2 = nullptr; ID3D11DeviceContext *c2 = nullptr;
  hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1, D3D11_SDK_VERSION, &d2, &got, &c2);
  ID3D11Texture2D *shared = make_texture(d, W, H, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
                                         D3D11_RESOURCE_MISC_SHARED, nullptr);
  ID3D11RenderTargetView *rtvs = nullptr;
  if (SUCCEEDED(hr) && shared && SUCCEEDED(d->CreateRenderTargetView(shared, nullptr, &rtvs))) {
    const float cyan[4] = {0, 1, 1, 1};
    c->ClearRenderTargetView(rtvs, cyan); c->Flush();
    IDXGIResource *res = nullptr; HANDLE h = nullptr;
    shared->QueryInterface(__uuidof(IDXGIResource), (void **)&res); res->GetSharedHandle(&h);
    ID3D11Texture2D *opened = nullptr;
    hr = d2->OpenSharedResource(h, __uuidof(ID3D11Texture2D), (void **)&opened);
    if (SUCCEEDED(hr)) {
      Sleep(100);
      ok = readback(d2, c2, opened, W / 2, H / 2, &px, &nz);
      sprintf_s(buf, "pixel=0x%08x nonzero=%u/%u", px, nz, W * H);
      report("shared-cross-device", ok && px == 0xff00ffffu && nz == W * H, buf);
    } else { sprintf_s(buf, "open hr=0x%08lx", hr); report("shared-cross-device", false, buf); }
  } else { sprintf_s(buf, "setup hr=0x%08lx", hr); report("shared-cross-device", false, buf); }
  printf("RESULT failures=%d\n", failures);
  return failures ? 1 : 0;
}
