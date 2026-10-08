// Presentation probe: D2D text in flip-model swap chains of RGBA8 and BGRA8,
// then the composed screen pixels of each window are read back through GDI.
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1.h>
#include <dwrite.h>
#include <stdio.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
static int screen_dark(int x, int y, int w, int hgt) {
  HDC s = GetDC(nullptr), m = CreateCompatibleDC(s); HBITMAP b = CreateCompatibleBitmap(s, w, hgt);
  SelectObject(m, b); BitBlt(m, 0, 0, w, hgt, s, x, y, SRCCOPY);
  BITMAPINFO bi = {}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -hgt;
  bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; static unsigned px[600 * 200];
  GetDIBits(m, b, 0, hgt, px, &bi, DIB_RGB_COLORS); int n = 0, red = 0;
  for (int i = 0; i < w * hgt; ++i) { unsigned p = px[i]; unsigned r = (p >> 16) & 255, g = (p >> 8) & 255, bl = p & 255;
    if (r < 80 && g < 80 && bl < 80) ++n; if (r > 160 && g < 80 && bl < 80) ++red; }
  DeleteObject(b); DeleteDC(m); ReleaseDC(nullptr, s); return n * 1000 + (red > 1000 ? 1 : 0);
}
int main() {
  setvbuf(stdout, nullptr, _IONBF, 0); SetProcessDPIAware();
  FILE *out = nullptr; fopen_s(&out, "C:\\Users\\pavel\\swap-result.txt", "w");
  WNDCLASSW wc = {}; wc.lpfnWndProc = proc; wc.hInstance = GetModuleHandleW(0); wc.lpszClassName = L"AgxSwap"; RegisterClassW(&wc);
  ID3D11Device *d; ID3D11DeviceContext *c; D3D_FEATURE_LEVEL got, lv[] = {D3D_FEATURE_LEVEL_10_0};
  if (FAILED(D3D11CreateDevice(0, D3D_DRIVER_TYPE_HARDWARE, 0, D3D11_CREATE_DEVICE_BGRA_SUPPORT, lv, 1, D3D11_SDK_VERSION, &d, &got, &c))) return 2;
  IDXGIDevice *xd; d->QueryInterface(__uuidof(IDXGIDevice), (void **)&xd); IDXGIAdapter *ad; xd->GetAdapter(&ad);
  IDXGIFactory2 *f; ad->GetParent(__uuidof(IDXGIFactory2), (void **)&f);
  ID2D1Factory *df; D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &df);
  IDWriteFactory *dw; DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown **)&dw);
  IDWriteTextFormat *tf; dw->CreateTextFormat(L"Segoe UI", 0, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 48.0f, L"en-us", &tf);
  const DXGI_FORMAT fmts[] = {DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM};
  const char *names[] = {"BGRA8", "RGBA8"};
  for (int i = 0; i < 2; ++i) {
    int X = 100, Y = 100 + i * 260, W = 600, H = 200;
    HWND h = CreateWindowExW(WS_EX_TOPMOST, L"AgxSwap", L"swap", WS_POPUP | WS_VISIBLE, X, Y, W, H, 0, 0, wc.hInstance, 0);
    DXGI_SWAP_CHAIN_DESC1 sd = {}; sd.Width = W; sd.Height = H; sd.Format = fmts[i]; sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 2; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    IDXGISwapChain1 *sc = nullptr; HRESULT hr = f->CreateSwapChainForHwnd(d, h, &sd, 0, 0, &sc);
    if (FAILED(hr)) { fprintf(out, "%s swapchain 0x%08lx\n", names[i], hr); continue; }
    for (int frame = 0; frame < 3; ++frame) {
      IDXGISurface *s; sc->GetBuffer(0, __uuidof(IDXGISurface), (void **)&s);
      D2D1_RENDER_TARGET_PROPERTIES p = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_HARDWARE,
          D2D1::PixelFormat(fmts[i], D2D1_ALPHA_MODE_IGNORE), 96, 96);
      ID2D1RenderTarget *rt; hr = df->CreateDxgiSurfaceRenderTarget(s, &p, &rt);
      if (FAILED(hr)) { fprintf(out, "%s rt 0x%08lx\n", names[i], hr); s->Release(); break; }
      ID2D1SolidColorBrush *b, *r; rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &b); rt->CreateSolidColorBrush(D2D1::ColorF(1, 0, 0, 1), &r);
      rt->BeginDraw(); rt->Clear(D2D1::ColorF(1, 1, 1, 1)); rt->FillRectangle(D2D1::RectF(500, 20, 580, 180), r);
      rt->DrawText(L"Hello Menu", 10, tf, D2D1::RectF(10, 40, 480, 160), b); hr = rt->EndDraw();
      b->Release(); r->Release(); rt->Release(); s->Release(); sc->Present(1, 0);
      MSG msg; while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageW(&msg); Sleep(300);
    }
    Sleep(1500); MSG msg; while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    int v = screen_dark(X, Y, W, H);
    fprintf(out, "%s enddraw=0x%08lx screen_dark=%d red=%d\n", names[i], hr, v / 1000, v % 1000); fflush(out);
    sc->Release(); DestroyWindow(h);
  }
  fprintf(out, "done\n"); fclose(out); return 0;
}
