// Glyph-atlas upload check (EXP1065). D2D/XAML keep text in DEFAULT shader-
// resource textures updated by many small UpdateSubresource boxes between
// draws that sample them. Model that pattern, then read the atlas back twice:
// CopyResource into a staging texture (the copy path) and a 1:1 Load() draw
// into a render target (the sampler path). A copy mismatch is an upload
// defect; a copy match with a draw mismatch is a layout/sampling defect.
// EXP1067: the sampler path runs three times: an SV_VertexID-only vertex
// shader with no input layout (vid), a POSITION vertex shader with a vertex
// buffer and layout (vb), and the SV_VertexID shader with that layout and
// buffer still bound (vidvb), separating a vertex-input-state defect from an
// SV_VertexID defect.
// Usage: agx_atlas_selftest [rounds] [updates]
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

static const char *kShader =
  "Texture2D t : register(t0);\n"
  "float4 vs(uint id : SV_VertexID) : SV_Position {\n"
  "  float2 p = float2((id << 1) & 2, id & 2); return float4(p * float2(2, -2) + float2(-1, 1), 0, 1); }\n"
  "float4 vsb(float2 p : POSITION) : SV_Position { return float4(p, 0, 1); }\n"
  "float4 psc(float4 p : SV_Position) : SV_Target { return t.Load(int3(p.xy, 0)); }\n"
  "float4 psa(float4 p : SV_Position) : SV_Target { float a = t.Load(int3(p.xy, 0)).a; return float4(a, a, a, a); }\n";

static unsigned rng_state = 12345u;
static unsigned rng() { rng_state = rng_state * 1103515245u + 12345u; return rng_state >> 8; }

struct Box { UINT x0, y0, x1, y1; };
static void grow(Box &b, UINT x, UINT y) {
  if (x < b.x0) b.x0 = x; if (y < b.y0) b.y0 = y; if (x + 1 > b.x1) b.x1 = x + 1; if (y + 1 > b.y1) b.y1 = y + 1;
}

int main(int argc, char **argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  const int rounds = argc > 1 ? atoi(argv[1]) : 4, updates = argc > 2 ? atoi(argv[2]) : 300;
  const UINT W = 512, H = 512;
  ID3D11Device *d = nullptr; ID3D11DeviceContext *c = nullptr; D3D_FEATURE_LEVEL got;
  D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_10_0};
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1,
                               D3D11_SDK_VERSION, &d, &got, &c))) { printf("FAIL device\n"); return 2; }
  ID3DBlob *vsb = nullptr, *vbb = nullptr, *pcb = nullptr, *pab = nullptr, *err = nullptr;
  D3DCompile(kShader, strlen(kShader), 0, 0, 0, "vs", "vs_4_0", 0, 0, &vsb, &err);
  D3DCompile(kShader, strlen(kShader), 0, 0, 0, "vsb", "vs_4_0", 0, 0, &vbb, &err);
  D3DCompile(kShader, strlen(kShader), 0, 0, 0, "psc", "ps_4_0", 0, 0, &pcb, &err);
  D3DCompile(kShader, strlen(kShader), 0, 0, 0, "psa", "ps_4_0", 0, 0, &pab, &err);
  if (!vsb || !vbb || !pcb || !pab) { printf("FAIL compile\n"); return 2; }
  ID3D11VertexShader *vs, *vsv; ID3D11PixelShader *psc, *psa;
  d->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), 0, &vs);
  d->CreateVertexShader(vbb->GetBufferPointer(), vbb->GetBufferSize(), 0, &vsv);
  const float tri[] = {-1, 1, 3, 1, -1, -3};
  D3D11_BUFFER_DESC bd = {sizeof(tri), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
  D3D11_SUBRESOURCE_DATA vdata = {tri, 0, 0}; ID3D11Buffer *vb; d->CreateBuffer(&bd, &vdata, &vb);
  const D3D11_INPUT_ELEMENT_DESC el[] = {{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}};
  ID3D11InputLayout *il; d->CreateInputLayout(el, 1, vbb->GetBufferPointer(), vbb->GetBufferSize(), &il);
  d->CreatePixelShader(pcb->GetBufferPointer(), pcb->GetBufferSize(), 0, &psc);
  d->CreatePixelShader(pab->GetBufferPointer(), pab->GetBufferSize(), 0, &psa);
  D3D11_TEXTURE2D_DESC rd = {W, H, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT,
                             D3D11_BIND_RENDER_TARGET, 0, 0};
  ID3D11Texture2D *rt; d->CreateTexture2D(&rd, 0, &rt);
  ID3D11RenderTargetView *rtv; d->CreateRenderTargetView(rt, 0, &rtv);
  rd.Usage = D3D11_USAGE_STAGING; rd.BindFlags = 0; rd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ID3D11Texture2D *rtst; d->CreateTexture2D(&rd, 0, &rtst);
  // The scratch target receives the interleaved draws (GPU use between updates).
  rd.Usage = D3D11_USAGE_DEFAULT; rd.BindFlags = D3D11_BIND_RENDER_TARGET; rd.CPUAccessFlags = 0;
  ID3D11Texture2D *scratch; d->CreateTexture2D(&rd, 0, &scratch);
  ID3D11RenderTargetView *srtv; d->CreateRenderTargetView(scratch, 0, &srtv);
  const DXGI_FORMAT fmts[] = {DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_A8_UNORM};
  const char *fn[] = {"BGRA8", "A8"};
  unsigned char *model = (unsigned char *)malloc(W * H * 4), *patch = (unsigned char *)malloc(64 * 64 * 4);
  int failures = 0;
  D3D11_VIEWPORT vp = {0, 0, (float)W, (float)H, 0, 1};
  for (int fi = 0; fi < 2; ++fi) for (int round = 0; round < rounds; ++round) {
    const UINT bpp = fi == 0 ? 4 : 1;
    D3D11_TEXTURE2D_DESC td = {W, H, 1, 1, fmts[fi], {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
    ID3D11Texture2D *t = nullptr;
    if (FAILED(d->CreateTexture2D(&td, nullptr, &t))) { printf("FAIL %s create\n", fn[fi]); ++failures; continue; }
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D *tst = nullptr; d->CreateTexture2D(&td, nullptr, &tst);
    ID3D11ShaderResourceView *srv = nullptr; d->CreateShaderResourceView(t, 0, &srv);
    for (UINT i = 0; i < W * H * bpp; ++i) model[i] = (unsigned char)(i * 7u + round * 13u + 1u);
    c->UpdateSubresource(t, 0, nullptr, model, W * bpp, 0);
    for (int u = 0; u < updates; ++u) {
      UINT bw = 4 + rng() % 40, bh = 4 + rng() % 40, bx = rng() % (W - bw), by = rng() % (H - bh);
      unsigned seed = rng();
      for (UINT y = 0; y < bh; ++y) for (UINT x = 0; x < bw * bpp; ++x) {
        unsigned char v = (unsigned char)(seed + y * 31u + x * 17u);
        patch[y * bw * bpp + x] = v; model[(by + y) * W * bpp + bx * bpp + x] = v;
      }
      D3D11_BOX b = {bx, by, 0, bx + bw, by + bh, 1};
      c->UpdateSubresource(t, 0, &b, patch, bw * bpp, 0);
      if (u % 10 == 9) {  // sample the atlas between updates, as text draws do
        c->OMSetRenderTargets(1, &srtv, 0); c->RSSetViewports(1, &vp);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); c->IASetInputLayout(nullptr);
        c->VSSetShader(vs, 0, 0); c->PSSetShader(fi == 0 ? psc : psa, 0, 0);
        c->PSSetShaderResources(0, 1, &srv); c->Draw(3, 0);
      }
    }
    // Copy path.
    c->CopyResource(tst, t);
    int copy_bad = 0; Box cb = {W, H, 0, 0}; char first_copy[96] = "";
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(c->Map(tst, 0, D3D11_MAP_READ, 0, &m))) {
      for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x)
        if (memcmp((char *)m.pData + y * m.RowPitch + x * bpp, model + (y * W + x) * bpp, bpp)) {
          if (!copy_bad++) sprintf(first_copy, " first=(%u,%u) want=%02x got=%02x", x, y,
              model[(y * W + x) * bpp], ((unsigned char *)m.pData)[y * m.RowPitch + x * bpp]);
          grow(cb, x, y);
        }
      c->Unmap(tst, 0);
    } else copy_bad = -1;
    // Sampler path, twice: SV_VertexID only, then vertex buffer + layout.
    int draw_bad[3] = {0, 0, 0}; Box db[3] = {{W, H, 0, 0}, {W, H, 0, 0}, {W, H, 0, 0}};
    char first_draw[3][96] = {"", "", ""};
    for (int path = 0; path < 3; ++path) {
      const float clear[4] = {0.5f, 0.25f, 0.75f, 1}; c->ClearRenderTargetView(rtv, clear);
      c->OMSetRenderTargets(1, &rtv, 0); c->RSSetViewports(1, &vp);
      c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      if (path == 0) { c->IASetInputLayout(nullptr); c->VSSetShader(vs, 0, 0); }
      else { UINT stride = 8, off = 0; c->IASetInputLayout(il); c->IASetVertexBuffers(0, 1, &vb, &stride, &off);
             c->VSSetShader(path == 1 ? vsv : vs, 0, 0); }
      c->PSSetShader(fi == 0 ? psc : psa, 0, 0);
      c->PSSetShaderResources(0, 1, &srv); c->Draw(3, 0);
      ID3D11ShaderResourceView *none = nullptr; c->PSSetShaderResources(0, 1, &none);
      c->CopyResource(rtst, rt);
      if (SUCCEEDED(c->Map(rtst, 0, D3D11_MAP_READ, 0, &m))) {
        for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) {
          const unsigned char *p = (unsigned char *)m.pData + y * m.RowPitch + x * 4;
          const unsigned char *e = model + (y * W + x) * bpp;
          bool ok = fi == 0 ? !memcmp(p, e, 4) : (p[3] == e[0]);
          if (!ok) {
            if (!draw_bad[path]++) sprintf(first_draw[path], " first=(%u,%u) want=%02x got=%02x", x, y, e[0], fi == 0 ? p[0] : p[3]);
            grow(db[path], x, y);
          }
        }
        c->Unmap(rtst, 0);
      } else draw_bad[path] = -1;
    }
    c->IASetInputLayout(nullptr);
    bool ok = copy_bad == 0 && draw_bad[0] == 0 && draw_bad[1] == 0 && draw_bad[2] == 0;
    printf("%s %s round=%d copy_bad=%d%s vid_bad=%d%s vb_bad=%d%s vidvb_bad=%d%s\n",
           ok ? "PASS" : "FAIL", fn[fi], round, copy_bad, first_copy,
           draw_bad[0], first_draw[0], draw_bad[1], first_draw[1], draw_bad[2], first_draw[2]);
    if (!ok) ++failures;
    srv->Release(); tst->Release(); t->Release();
  }
  printf("RESULT failures=%d\n", failures);
  return failures;
}
