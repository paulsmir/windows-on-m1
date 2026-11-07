// DXBC -> DXIL through the system dxilconv.dll (the D3D12 runtime's converter)
#include <windows.h>
#include <stdio.h>
#include <vector>
struct __declspec(uuid("5F956ED5-78D1-4B15-8247-F7187614A041")) IDxbcConverter : public IUnknown {
  virtual HRESULT STDMETHODCALLTYPE Convert(LPCVOID pDxbc, UINT32 DxbcSize, LPCWSTR pExtraOptions, LPVOID *ppDxil,
                                            UINT32 *pDxilSize, LPWSTR *ppDiag) = 0;
};
static const CLSID CLSID_DxbcConverter = {0x4900391e, 0xb752, 0x4edd, {0xa8, 0x85, 0x6f, 0xb7, 0x6e, 0x25, 0xad, 0xdb}};
typedef HRESULT(WINAPI *Create)(REFCLSID, REFIID, LPVOID *);
int main(int argc, char **argv) {
  if (argc < 3) return 2;
  FILE *f = fopen(argv[1], "rb"); if (!f) return 3;
  std::vector<char> in; char buf[4096]; size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) in.insert(in.end(), buf, buf + n);
  fclose(f);
  HMODULE m = LoadLibraryA("dxilconv.dll"); if (!m) { printf("load fail\n"); return 4; }
  Create c = (Create)GetProcAddress(m, "DxcCreateInstance"); if (!c) { printf("no export\n"); return 5; }
  IDxbcConverter *conv = nullptr;
  HRESULT hr = c(CLSID_DxbcConverter, __uuidof(IDxbcConverter), (void **)&conv);
  if (FAILED(hr)) { printf("create 0x%08lx\n", hr); return 6; }
  void *out = nullptr; UINT32 size = 0; LPWSTR diag = nullptr;
  hr = conv->Convert(in.data(), (UINT32)in.size(), nullptr, &out, &size, &diag);
  printf("convert 0x%08lx size %u\n", hr, size);
  if (diag) wprintf(L"diag: %s\n", diag);
  if (FAILED(hr)) return 7;
  f = fopen(argv[2], "wb"); fwrite(out, 1, size, f); fclose(f);
  return 0;
}
