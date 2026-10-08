// Composed-frame capture through DXGI desktop duplication; writes a 32-bit BMP.
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <stdlib.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "user32.lib")
int main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : "C:\\Users\\pavel\\dup.bmp";
  int waitms = argc > 2 ? atoi(argv[2]) : 2000;
  FILE *log = nullptr; fopen_s(&log, "C:\\Users\\pavel\\dup-log.txt", "w");
  ID3D11Device *d; ID3D11DeviceContext *c; D3D_FEATURE_LEVEL got, lv[] = {D3D_FEATURE_LEVEL_10_0};
  HRESULT hr = D3D11CreateDevice(0, D3D_DRIVER_TYPE_HARDWARE, 0, 0, lv, 1, D3D11_SDK_VERSION, &d, &got, &c);
  if (FAILED(hr)) { fprintf(log, "device 0x%08lx\n", hr); return 2; }
  IDXGIDevice *xd; d->QueryInterface(__uuidof(IDXGIDevice), (void **)&xd); IDXGIAdapter *ad; xd->GetAdapter(&ad);
  IDXGIOutput *o = nullptr; hr = ad->EnumOutputs(0, &o);
  if (FAILED(hr)) { fprintf(log, "output 0x%08lx\n", hr); return 3; }
  IDXGIOutput1 *o1; o->QueryInterface(__uuidof(IDXGIOutput1), (void **)&o1);
  IDXGIOutputDuplication *dup = nullptr; hr = o1->DuplicateOutput(d, &dup);
  if (FAILED(hr)) { fprintf(log, "duplicate 0x%08lx\n", hr); return 4; }
  IDXGIResource *res = nullptr; DXGI_OUTDUPL_FRAME_INFO fi;
  ID3D11Texture2D *last = nullptr;
  DWORD t0 = GetTickCount(); int frames = 0;
  while ((int)(GetTickCount() - t0) < waitms) {
    hr = dup->AcquireNextFrame(200, &fi, &res);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) continue;
    if (FAILED(hr)) { fprintf(log, "acquire 0x%08lx\n", hr); break; }
    ID3D11Texture2D *tex; res->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&tex);
    D3D11_TEXTURE2D_DESC td; tex->GetDesc(&td);
    if (!last) { D3D11_TEXTURE2D_DESC sd = td; sd.Usage = D3D11_USAGE_STAGING; sd.BindFlags = 0; sd.MiscFlags = 0;
      sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d->CreateTexture2D(&sd, 0, &last); }
    if (fi.LastPresentTime.QuadPart || !frames) { c->CopyResource(last, tex); ++frames; }
    SetCursorPos(1200 + (frames & 7), 900);
    tex->Release(); res->Release(); dup->ReleaseFrame();
  }
  if (!last || !frames) { fprintf(log, "no frame (frames=%d)\n", frames); if (!last) return 5; }
  D3D11_TEXTURE2D_DESC td; last->GetDesc(&td); D3D11_MAPPED_SUBRESOURCE m;
  hr = c->Map(last, 0, D3D11_MAP_READ, 0, &m); if (FAILED(hr)) { fprintf(log, "map 0x%08lx\n", hr); return 6; }
  FILE *f = nullptr; fopen_s(&f, path, "wb");
  BITMAPFILEHEADER fh = {0x4d42}; BITMAPINFOHEADER ih = {sizeof(ih)}; ih.biWidth = td.Width; ih.biHeight = -(LONG)td.Height;
  ih.biPlanes = 1; ih.biBitCount = 32; fh.bfOffBits = sizeof(fh) + sizeof(ih); fh.bfSize = fh.bfOffBits + td.Width * td.Height * 4;
  fwrite(&fh, sizeof(fh), 1, f); fwrite(&ih, sizeof(ih), 1, f);
  for (UINT y = 0; y < td.Height; ++y) fwrite((char *)m.pData + y * m.RowPitch, 4, td.Width, f);
  fclose(f); c->Unmap(last, 0);
  fprintf(log, "ok %ux%u format %u frames %d\n", td.Width, td.Height, td.Format, frames); fclose(log);
  return 0;
}
