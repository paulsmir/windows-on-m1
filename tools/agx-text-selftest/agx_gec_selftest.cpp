// Grayscale enhanced contrast probe: D2D grayscale text with system default
// rendering params versus custom IDWriteRenderingParams1 grayscale contrast.
#include <windows.h>
#include <d3d11.h>
#include <d2d1.h>
#include <dwrite_1.h>
#include <stdio.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
static int count_dark(ID3D11Device *d, ID3D11DeviceContext *c, ID3D11Texture2D *t, UINT W, UINT H) {
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
  const UINT W = 256, H = 64; ID3D11Device *d; ID3D11DeviceContext *c; D3D_FEATURE_LEVEL got, lv[] = {D3D_FEATURE_LEVEL_10_0};
  if (FAILED(D3D11CreateDevice(0, D3D_DRIVER_TYPE_HARDWARE, 0, D3D11_CREATE_DEVICE_BGRA_SUPPORT, lv, 1, D3D11_SDK_VERSION, &d, &got, &c))) return 2;
  ID2D1Factory *f; D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &f);
  IDWriteFactory1 *dw; DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory1), (IUnknown **)&dw);
  IDWriteRenderingParams *def = nullptr; dw->CreateRenderingParams(&def);
  IDWriteRenderingParams1 *def1 = nullptr; def->QueryInterface(__uuidof(IDWriteRenderingParams1), (void **)&def1);
  printf("default gamma=%.2f contrast=%.2f cleartype=%.2f mode=%d grayscale_contrast=%.2f\n", def->GetGamma(),
         def->GetEnhancedContrast(), def->GetClearTypeLevel(), def->GetRenderingMode(),
         def1 ? def1->GetGrayscaleEnhancedContrast() : -1.0f);
  IDWriteTextFormat *tf; dw->CreateTextFormat(L"Segoe UI", 0, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                              DWRITE_FONT_STRETCH_NORMAL, 32.0f, L"en-us", &tf);
  const float gec[] = {-1.0f, -1.0f, 0.0f, 1.0f, -1.0f};
  int fails = 0;
  for (int v = 0; v < 5; ++v) {
    D3D11_TEXTURE2D_DESC td = {W, H, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT,
                               D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0};
    ID3D11Texture2D *t; d->CreateTexture2D(&td, 0, &t); IDXGISurface *sf; t->QueryInterface(__uuidof(IDXGISurface), (void **)&sf);
    D2D1_RENDER_TARGET_PROPERTIES p = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_HARDWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    ID2D1RenderTarget *rt; if (FAILED(f->CreateDxgiSurfaceRenderTarget(sf, &p, &rt))) return 3;
    rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    IDWriteRenderingParams1 *rp = nullptr;
    if (gec[v] >= 0.0f) { dw->CreateCustomRenderingParams(def->GetGamma(), def->GetEnhancedContrast(), gec[v],
        def->GetClearTypeLevel(), def->GetPixelGeometry(), def->GetRenderingMode(), &rp); rt->SetTextRenderingParams(rp); }
    ID2D1SolidColorBrush *b; rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &b);
    rt->BeginDraw(); rt->Clear(D2D1::ColorF(1, 1, 1, 1));
    rt->DrawText(L"Hello World", 11, tf, D2D1::RectF(4, 4, (float)W, (float)H), b);
    HRESULT hr = rt->EndDraw(); c->Flush();
    int n = count_dark(d, c, t, W, H); bool ok = SUCCEEDED(hr) && n > 50;
    printf("%s run%d grayscale gec=%.2f dark=%d\n", ok ? "PASS" : "FAIL", v, gec[v], n);
    if (!ok) ++fails; if (rp) rp->Release(); b->Release(); rt->Release(); sf->Release(); t->Release();
  }
  printf("RESULT failures=%d\n", fails); return fails;
}
