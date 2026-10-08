// Font probe: D2D grayscale text across font families and XAML-like sizes.
#include <windows.h>
#include <d3d11.h>
#include <d2d1.h>
#include <dwrite.h>
#include <stdio.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
static ID3D11Device *d; static ID3D11DeviceContext *c;
static int count_dark(ID3D11Texture2D *t, UINT W, UINT H) {
  D3D11_TEXTURE2D_DESC desc; t->GetDesc(&desc); desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0;
  desc.MiscFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; ID3D11Texture2D *s = nullptr;
  if (FAILED(d->CreateTexture2D(&desc, nullptr, &s))) return -1; c->CopyResource(s, t);
  D3D11_MAPPED_SUBRESOURCE m = {}; if (FAILED(c->Map(s, 0, D3D11_MAP_READ, 0, &m))) { s->Release(); return -2; }
  int n = 0; for (UINT y = 0; y < H; ++y) { const unsigned *row = (const unsigned *)((const char *)m.pData + y * m.RowPitch);
    for (UINT x = 0; x < W; ++x) if ((row[x] & 0xffu) < 0xc0u) ++n; }
  c->Unmap(s, 0); s->Release(); return n;
}
int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  const UINT W = 256, H = 48; D3D_FEATURE_LEVEL got, lv[] = {D3D_FEATURE_LEVEL_10_0};
  if (FAILED(D3D11CreateDevice(0, D3D_DRIVER_TYPE_HARDWARE, 0, D3D11_CREATE_DEVICE_BGRA_SUPPORT, lv, 1, D3D11_SDK_VERSION, &d, &got, &c))) return 2;
  ID2D1Factory *f; D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &f);
  IDWriteFactory *dw; DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown **)&dw);
  const wchar_t *fams[] = {L"Segoe UI", L"Segoe UI Variable Text", L"Segoe UI Variable Display", L"Segoe Fluent Icons"};
  const float sizes[] = {12.0f, 14.0f, 20.0f};
  const wchar_t *txt[] = {L"File Edit View", L"File Edit View", L"File Edit View", L"\xE70D\xE713\xE710"};
  D3D11_TEXTURE2D_DESC td = {W, H, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT,
                             D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0};
  ID3D11Texture2D *t; d->CreateTexture2D(&td, 0, &t); IDXGISurface *sf; t->QueryInterface(__uuidof(IDXGISurface), (void **)&sf);
  D2D1_RENDER_TARGET_PROPERTIES p = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_HARDWARE,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
  ID2D1RenderTarget *rt; if (FAILED(f->CreateDxgiSurfaceRenderTarget(sf, &p, &rt))) return 3;
  ID2D1SolidColorBrush *b; rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &b);
  for (int mode = 0; mode < 2; ++mode) {
    rt->SetTextAntialiasMode(mode ? D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE : D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    for (int fi = 0; fi < 4; ++fi) for (int si = 0; si < 3; ++si) {
      IDWriteTextFormat *tf = nullptr; HRESULT hr = dw->CreateTextFormat(fams[fi], 0, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
          DWRITE_FONT_STRETCH_NORMAL, sizes[si], L"en-us", &tf);
      if (FAILED(hr)) { printf("%ls %.0f format 0x%08lx\n", fams[fi], sizes[si], hr); continue; }
      rt->BeginDraw(); rt->Clear(D2D1::ColorF(1, 1, 1, 1));
      rt->DrawText(txt[fi], (UINT32)wcslen(txt[fi]), tf, D2D1::RectF(2, 2, (float)W, (float)H), b);
      hr = rt->EndDraw(); c->Flush();
      int n = count_dark(t, W, H);
      printf("%s %-26ls %4.0f %s dark=%d hr=0x%08lx\n", n > 20 ? "PASS" : "FAIL", fams[fi], sizes[si], mode ? "cleartype" : "grayscale", n, hr);
      tf->Release();
    }
  }
  return 0;
}
