// First-use glyph probe: separates (a) readback racing the draw, (b) the
// first glyph-atlas upload of a process, (c) any upload of new glyphs.
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
    for (UINT x = 0; x < W; ++x) if ((row[x] & 0xffu) < 0x80u) ++n; }
  c->Unmap(s, 0); s->Release(); return n;
}
int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  const UINT W = 256, H = 64; D3D_FEATURE_LEVEL got, lv[] = {D3D_FEATURE_LEVEL_10_0};
  if (FAILED(D3D11CreateDevice(0, D3D_DRIVER_TYPE_HARDWARE, 0, D3D11_CREATE_DEVICE_BGRA_SUPPORT, lv, 1, D3D11_SDK_VERSION, &d, &got, &c))) return 2;
  ID2D1Factory *f; D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &f);
  IDWriteFactory *dw; DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown **)&dw);
  IDWriteTextFormat *tf; dw->CreateTextFormat(L"Segoe UI", 0, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                              DWRITE_FONT_STRETCH_NORMAL, 32.0f, L"en-us", &tf);
  D3D11_TEXTURE2D_DESC td = {W, H, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT,
                             D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0};
  ID3D11Texture2D *t; d->CreateTexture2D(&td, 0, &t); IDXGISurface *sf; t->QueryInterface(__uuidof(IDXGISurface), (void **)&sf);
  D2D1_RENDER_TARGET_PROPERTIES p = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_HARDWARE,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
  ID2D1RenderTarget *rt; if (FAILED(f->CreateDxgiSurfaceRenderTarget(sf, &p, &rt))) return 3;
  rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  ID2D1SolidColorBrush *b; rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &b);
  const wchar_t *texts[] = {L"Hello World", L"Hello World", L"Quartz jinx", L"Quartz jinx", L"Hello Quartz"};
  for (int v = 0; v < 5; ++v) {
    rt->BeginDraw(); rt->Clear(D2D1::ColorF(1, 1, 1, 1));
    rt->DrawText(texts[v], (UINT32)wcslen(texts[v]), tf, D2D1::RectF(4, 4, (float)W, (float)H), b);
    HRESULT hr = rt->EndDraw(); c->Flush();
    int n0 = count_dark(t, W, H);
    c->Flush(); Sleep(200);
    int n1 = count_dark(t, W, H);
    printf("step%d '%ls' hr=0x%08lx dark_now=%d dark_later=%d\n", v, texts[v], (unsigned long)hr, n0, n1);
  }
  // Same glyphs twice inside one EndDraw: second instance after first upload.
  rt->BeginDraw(); rt->Clear(D2D1::ColorF(1, 1, 1, 1));
  rt->DrawText(L"Vex", 3, tf, D2D1::RectF(4, 4, 120, (float)H), b);
  rt->DrawText(L"Vex", 3, tf, D2D1::RectF(130, 4, (float)W, (float)H), b);
  rt->EndDraw(); c->Flush();
  int left = 0, right = 0; {
    D3D11_TEXTURE2D_DESC desc; t->GetDesc(&desc); desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; ID3D11Texture2D *s; d->CreateTexture2D(&desc, nullptr, &s);
    c->CopyResource(s, t); D3D11_MAPPED_SUBRESOURCE m = {}; c->Map(s, 0, D3D11_MAP_READ, 0, &m);
    for (UINT y = 0; y < H; ++y) { const unsigned *row = (const unsigned *)((const char *)m.pData + y * m.RowPitch);
      for (UINT x = 0; x < W; ++x) if ((row[x] & 0xffu) < 0x80u) (x < 125 ? left : right)++; }
    c->Unmap(s, 0); s->Release(); }
  printf("pair 'Vex' left=%d right=%d\n", left, right);
  return 0;
}
