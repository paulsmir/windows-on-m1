// Dynamic texture Map(WRITE_DISCARD) layout probe: prints the returned pitch
// and reads the texture back through a staging copy, row by row.
#include <windows.h>
#include <d3d11.h>
#include <stdio.h>
#pragma comment(lib, "d3d11.lib")
int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  ID3D11Device *d; ID3D11DeviceContext *c; D3D_FEATURE_LEVEL got, lv[] = {D3D_FEATURE_LEVEL_10_0};
  if (FAILED(D3D11CreateDevice(0, D3D_DRIVER_TYPE_HARDWARE, 0, 0, lv, 1, D3D11_SDK_VERSION, &d, &got, &c))) return 2;
  const DXGI_FORMAT fmts[] = {DXGI_FORMAT_A8_UNORM, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM};
  const char *fn[] = {"A8", "R8", "BGRA8"};
  for (int fi = 0; fi < 3; ++fi) for (int run = 0; run < 2; ++run) {
    const UINT TW = 32, TH = 32, bpp = fi == 2 ? 4 : 1;
    D3D11_TEXTURE2D_DESC td = {TW, TH, 1, 1, fmts[fi], {1, 0}, D3D11_USAGE_DYNAMIC, D3D11_BIND_SHADER_RESOURCE,
                               D3D11_CPU_ACCESS_WRITE, 0};
    ID3D11Texture2D *t; if (FAILED(d->CreateTexture2D(&td, 0, &t))) { printf("%s create failed\n", fn[fi]); continue; }
    D3D11_MAPPED_SUBRESOURCE ms = {};
    HRESULT hr = c->Map(t, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms);
    if (SUCCEEDED(hr)) { for (UINT y = 0; y < TH; ++y) memset((char *)ms.pData + y * ms.RowPitch, (int)(y + 1), TW * bpp); c->Unmap(t, 0); }
    td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D *s; d->CreateTexture2D(&td, 0, &s); c->CopyResource(s, t);
    D3D11_MAPPED_SUBRESOURCE mr = {}; int good = 0; char rows[TH + 1] = {0};
    if (SUCCEEDED(c->Map(s, 0, D3D11_MAP_READ, 0, &mr))) {
      for (UINT y = 0; y < TH; ++y) { const unsigned char *r = (const unsigned char *)mr.pData + y * mr.RowPitch; int ok = 1;
        for (UINT x = 0; x < TW * bpp; ++x) if (r[x] != (unsigned char)(y + 1)) { ok = 0; break; }
        good += ok; rows[y] = ok ? '1' : (r[0] ? 'x' : '0'); }
      c->Unmap(s, 0);
    }
    printf("%s run%d map=0x%08lx pitch=%u depth=%u readpitch=%u goodrows=%d rows=%s\n", fn[fi], run, hr, ms.RowPitch,
           ms.DepthPitch, mr.RowPitch, good, rows);
    s->Release(); t->Release();
  }
  return 0;
}
