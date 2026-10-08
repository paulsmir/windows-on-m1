// Narrow the text failure: sample one channel of a small texture created with
// format F and filled by method M, draw it full-screen, read back.
// Formats: A8_UNORM (alpha), R8_UNORM (red), B8G8R8A8 (alpha).
// Methods: init (initial data), upd (full UpdateSubresource), box (sub-box
// UpdateSubresource of the centre), dyn (dynamic texture Map WRITE_DISCARD).
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <string.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

static const char *kShader =
  "Texture2D t : register(t0); SamplerState s : register(s0);\n"
  "struct V { float4 p : SV_Position; float2 uv : TEXCOORD0; };\n"
  "V vs(float2 p : POSITION, float2 uv : TEXCOORD0) { V o; o.p = float4(p, 0, 1); o.uv = uv; return o; }\n"
  "float4 psa(V i) : SV_Target { float a = t.Sample(s, i.uv).a; return float4(1 - a, 1 - a, 1 - a, 1); }\n"
  "float4 psr(V i) : SV_Target { float a = t.Sample(s, i.uv).r; return float4(1 - a, 1 - a, 1 - a, 1); }\n";

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  printf("start\n");
  const UINT W = 64, H = 64, TW = 32, TH = 32;
  ID3D11Device *d = nullptr; ID3D11DeviceContext *c = nullptr; D3D_FEATURE_LEVEL got;
  D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_10_0};
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1,
                               D3D11_SDK_VERSION, &d, &got, &c))) { printf("FAIL device\n"); return 2; }
  ID3DBlob *vsb = nullptr, *pab = nullptr, *prb = nullptr, *err = nullptr;
  D3DCompile(kShader, strlen(kShader), 0, 0, 0, "vs", "vs_4_0", 0, 0, &vsb, &err);
  D3DCompile(kShader, strlen(kShader), 0, 0, 0, "psa", "ps_4_0", 0, 0, &pab, &err);
  D3DCompile(kShader, strlen(kShader), 0, 0, 0, "psr", "ps_4_0", 0, 0, &prb, &err);
  ID3D11VertexShader *vs; ID3D11PixelShader *psa, *psr;
  d->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), 0, &vs);
  d->CreatePixelShader(pab->GetBufferPointer(), pab->GetBufferSize(), 0, &psa);
  d->CreatePixelShader(prb->GetBufferPointer(), prb->GetBufferSize(), 0, &psr);
  const float verts[] = {-1, 1, 0, 0, 3, 1, 2, 0, -1, -3, 0, 2};
  D3D11_BUFFER_DESC bd = {sizeof(verts), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
  D3D11_SUBRESOURCE_DATA vdata = {verts, 0, 0}; ID3D11Buffer *vb; d->CreateBuffer(&bd, &vdata, &vb);
  const D3D11_INPUT_ELEMENT_DESC el[] = {{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0}};
  ID3D11InputLayout *il; d->CreateInputLayout(el, 2, vsb->GetBufferPointer(), vsb->GetBufferSize(), &il);
  D3D11_SAMPLER_DESC sd = {}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
  sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
  ID3D11SamplerState *ss; d->CreateSamplerState(&sd, &ss);
  D3D11_TEXTURE2D_DESC rd = {W, H, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
  ID3D11Texture2D *rt; d->CreateTexture2D(&rd, 0, &rt); ID3D11RenderTargetView *rtv; d->CreateRenderTargetView(rt, 0, &rtv);
  rd.Usage = D3D11_USAGE_STAGING; rd.BindFlags = 0; rd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ID3D11Texture2D *st; d->CreateTexture2D(&rd, 0, &st);
  const DXGI_FORMAT fmts[] = {DXGI_FORMAT_A8_UNORM, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM};
  const char *fn[] = {"A8", "R8", "BGRA8"}; const char *mn[] = {"init", "upd", "box", "dyn"};
  static unsigned char data[TW * TH * 4];
  int fails = 0;
  for (int fi = 0; fi < 3; ++fi) for (int m = 0; m < 4; ++m) {
    UINT bpp = fi == 2 ? 4 : 1;
    memset(data, 0xff, sizeof(data));  // every channel 1.0
    D3D11_TEXTURE2D_DESC td = {TW, TH, 1, 1, fmts[fi], {1, 0}, m == 3 ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT,
                               D3D11_BIND_SHADER_RESOURCE, m == 3 ? D3D11_CPU_ACCESS_WRITE : 0u, 0};
    D3D11_SUBRESOURCE_DATA init = {data, TW * bpp, 0};
    ID3D11Texture2D *t = nullptr;
    HRESULT hr = d->CreateTexture2D(&td, m == 0 ? &init : nullptr, &t);
    if (FAILED(hr)) { printf("FAIL %s-%s create 0x%08lx\n", fn[fi], mn[m], hr); ++fails; continue; }
    if (m == 1) c->UpdateSubresource(t, 0, nullptr, data, TW * bpp, 0);
    if (m == 2) { D3D11_BOX b = {8, 8, 0, 24, 24, 1}; c->UpdateSubresource(t, 0, &b, data, 16 * bpp, 0); }
    if (m == 3) { D3D11_MAPPED_SUBRESOURCE ms; if (SUCCEEDED(c->Map(t, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
        for (UINT y = 0; y < TH; ++y) memset((char *)ms.pData + y * ms.RowPitch, 0xff, TW * bpp); c->Unmap(t, 0); } }
    ID3D11ShaderResourceView *srv = nullptr; d->CreateShaderResourceView(t, 0, &srv);
    const float white[4] = {1, 1, 1, 1}; c->ClearRenderTargetView(rtv, white);
    D3D11_VIEWPORT vp = {0, 0, (float)W, (float)H, 0, 1}; UINT stride = 16, off = 0;
    c->OMSetRenderTargets(1, &rtv, 0); c->RSSetViewports(1, &vp); c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c->IASetInputLayout(il); c->IASetVertexBuffers(0, 1, &vb, &stride, &off); c->VSSetShader(vs, 0, 0);
    c->PSSetShader(fi == 1 ? psr : psa, 0, 0); c->PSSetShaderResources(0, 1, &srv); c->PSSetSamplers(0, 1, &ss);
    c->Draw(3, 0);
    c->CopyResource(st, rt); D3D11_MAPPED_SUBRESOURCE mr; int dark = 0;
    if (SUCCEEDED(c->Map(st, 0, D3D11_MAP_READ, 0, &mr))) {
      for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x)
        if ((((unsigned *)((char *)mr.pData + y * mr.RowPitch))[x] & 0xff) < 0x80) ++dark;
      c->Unmap(st, 0);
    }
    int expect = m == 2 ? (W * H / 4) : (W * H);  // box covers the centre quarter
    bool ok = dark >= expect * 9 / 10 && dark <= expect * 11 / 10;
    printf("%s %s-%s dark=%d expect=%d\n", ok ? "PASS" : "FAIL", fn[fi], mn[m], dark, expect);
    if (!ok) ++fails;
    srv->Release(); t->Release();
  }
  printf("RESULT failures=%d\n", fails);
  return fails;
}
