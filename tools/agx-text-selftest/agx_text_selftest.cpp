// Text rendering probe: Direct2D/DirectWrite text drawn on a D3D11 BGRA
// render target of the hardware adapter, read back and counted. Variants:
// grayscale and ClearType antialiasing, and a plain filled rectangle control.
#include <windows.h>
#include <d3d11.h>
#include <d2d1.h>
#include <dwrite.h>
#include <stdio.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

static int count_dark(ID3D11Device *d, ID3D11DeviceContext *c, ID3D11Texture2D *t, UINT W, UINT H) {
  D3D11_TEXTURE2D_DESC desc; t->GetDesc(&desc);
  desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.MiscFlags = 0;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ID3D11Texture2D *s = nullptr;
  if (FAILED(d->CreateTexture2D(&desc, nullptr, &s))) return -1;
  c->CopyResource(s, t);
  D3D11_MAPPED_SUBRESOURCE m = {};
  if (FAILED(c->Map(s, 0, D3D11_MAP_READ, 0, &m))) { s->Release(); return -2; }
  int n = 0;
  for (UINT y = 0; y < H; ++y) {
    const unsigned *row = (const unsigned *)((const char *)m.pData + y * m.RowPitch);
    for (UINT x = 0; x < W; ++x) if ((row[x] & 0xffu) < 0x80u) ++n;  // dark blue channel on white
  }
  c->Unmap(s, 0); s->Release();
  return n;
}

int main() {
  const UINT W = 256, H = 64;
  ID3D11Device *d = nullptr; ID3D11DeviceContext *c = nullptr; D3D_FEATURE_LEVEL got;
  D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_10_0};
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
      D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 1, D3D11_SDK_VERSION, &d, &got, &c);
  printf("device hr=0x%08lx\n", hr); if (FAILED(hr)) return 1;
  const DXGI_FORMAT probe[] = {DXGI_FORMAT_A8_UNORM, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM,
                               DXGI_FORMAT_R8G8B8A8_UNORM};
  const char *pn[] = {"A8", "R8", "BGRA8", "RGBA8"};
  for (int i = 0; i < 4; ++i) { UINT caps = 0; HRESULT ch = d->CheckFormatSupport(probe[i], &caps);
    printf("format %s hr=0x%08lx support=0x%08x sample=%u tex2d=%u rt=%u blend=%u\n", pn[i], ch, caps,
           (caps & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) != 0, (caps & D3D11_FORMAT_SUPPORT_TEXTURE2D) != 0,
           (caps & D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0, (caps & D3D11_FORMAT_SUPPORT_BLENDABLE) != 0); }
  ID2D1Factory *f = nullptr; IDWriteFactory *dw = nullptr;
  D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &f);
  DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown **)&dw);
  IDWriteTextFormat *tf = nullptr;
  dw->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                       DWRITE_FONT_STRETCH_NORMAL, 32.0f, L"en-us", &tf);
  const char *names[] = {"rect", "text-grayscale", "text-cleartype", "text-aliased",
                         "text-grayscale-gamma1", "text-grayscale-natural", "text-grayscale-gdi",
                         "ellipse-aa", "ellipse-aliased", "text-grayscale-outline"};
  IDWriteRenderingParams *gamma1 = nullptr, *natural = nullptr, *gdi = nullptr;
  dw->CreateCustomRenderingParams(1.0f, 0.0f, 0.0f, DWRITE_PIXEL_GEOMETRY_FLAT,
                                  DWRITE_RENDERING_MODE_DEFAULT, &gamma1);
  dw->CreateCustomRenderingParams(1.8f, 0.5f, 0.0f, DWRITE_PIXEL_GEOMETRY_FLAT,
                                  DWRITE_RENDERING_MODE_NATURAL, &natural);
  dw->CreateCustomRenderingParams(1.8f, 0.5f, 0.0f, DWRITE_PIXEL_GEOMETRY_FLAT,
                                  DWRITE_RENDERING_MODE_GDI_CLASSIC, &gdi);
  int fails = 0;
  IDWriteRenderingParams *outline = nullptr;
  dw->CreateCustomRenderingParams(1.8f, 0.5f, 0.0f, DWRITE_PIXEL_GEOMETRY_FLAT,
                                  DWRITE_RENDERING_MODE_OUTLINE, &outline);
  for (int variant = 0; variant < 10; ++variant) {
    D3D11_TEXTURE2D_DESC td = {W, H, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT,
                               D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0};
    ID3D11Texture2D *t = nullptr; d->CreateTexture2D(&td, nullptr, &t);
    IDXGISurface *surf = nullptr; t->QueryInterface(__uuidof(IDXGISurface), (void **)&surf);
    D2D1_RENDER_TARGET_PROPERTIES p = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_HARDWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    ID2D1RenderTarget *rt = nullptr;
    hr = f->CreateDxgiSurfaceRenderTarget(surf, &p, &rt);
    if (FAILED(hr)) { printf("FAIL %s rt hr=0x%08lx\n", names[variant], hr); ++fails; continue; }
    rt->SetTextAntialiasMode(variant == 2 ? D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE :
                             variant == 3 ? D2D1_TEXT_ANTIALIAS_MODE_ALIASED : D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    if (variant == 4) rt->SetTextRenderingParams(gamma1);
    if (variant == 5) rt->SetTextRenderingParams(natural);
    if (variant == 6) rt->SetTextRenderingParams(gdi);
    if (variant == 9) rt->SetTextRenderingParams(outline);
    rt->SetAntialiasMode(variant == 8 ? D2D1_ANTIALIAS_MODE_ALIASED : D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    ID2D1SolidColorBrush *b = nullptr; rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &b);
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(1, 1, 1, 1));
    if (variant == 0) rt->FillRectangle(D2D1::RectF(10, 10, 60, 50), b);
    else if (variant == 7 || variant == 8) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(40, 32), 28, 24), b);
    else rt->DrawText(L"Hello World", 11, tf, D2D1::RectF(4, 4, (float)W, (float)H), b);
    hr = rt->EndDraw();
    c->Flush();
    int n = count_dark(d, c, t, W, H);
    bool ok = SUCCEEDED(hr) && n > 50;
    printf("%s %s enddraw=0x%08lx dark_pixels=%d\n", ok ? "PASS" : "FAIL", names[variant], hr, n);
    if (!ok) ++fails;
    b->Release(); rt->Release(); surf->Release(); t->Release();
  }
  printf("RESULT failures=%d\n", fails);
  return fails;
}
