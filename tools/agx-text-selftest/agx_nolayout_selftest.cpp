// Missing vertex input check (EXP1068). EXP1066/EXP1067 hit a GPU timeout
// when the trace load started Notepad, after b1327c6e let layout-less draws
// reach the GPU instead of crashing at agx_update_vs. One case per process:
//   C  SV_VertexID-only vertex shader, NULL input layout (control)
//   B  POSITION vertex shader, layout bound, no vertex buffer bound
//   A  POSITION vertex shader, NULL input layout, no vertex buffer
// D3D gives the missing inputs no defined value; the driver must not hang.
// Usage: agx_nolayout_selftest A|B|C
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <string.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

static const char *kShader =
  "float4 vsid(uint id : SV_VertexID) : SV_Position {\n"
  "  float2 p = float2((id << 1) & 2, id & 2); return float4(p * float2(2, -2) + float2(-1, 1), 0, 1); }\n"
  "float4 vspos(float2 p : POSITION) : SV_Position { return float4(p, 0, 1); }\n"
  "float4 ps(float4 p : SV_Position) : SV_Target { return float4(0, 1, 0, 1); }\n";

int main(int argc, char **argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  char mode = argc > 1 ? argv[1][0] : 'C';
  const UINT W = 64, H = 64;
  ID3D11Device *d = nullptr; ID3D11DeviceContext *c = nullptr; D3D_FEATURE_LEVEL got;
  D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_10_0};
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1,
                               D3D11_SDK_VERSION, &d, &got, &c))) { printf("FAIL device\n"); return 2; }
  ID3DBlob *vsid = nullptr, *vspos = nullptr, *psb = nullptr, *err = nullptr;
  D3DCompile(kShader, strlen(kShader), 0, 0, 0, "vsid", "vs_4_0", 0, 0, &vsid, &err);
  D3DCompile(kShader, strlen(kShader), 0, 0, 0, "vspos", "vs_4_0", 0, 0, &vspos, &err);
  D3DCompile(kShader, strlen(kShader), 0, 0, 0, "ps", "ps_4_0", 0, 0, &psb, &err);
  if (!vsid || !vspos || !psb) { printf("FAIL compile\n"); return 2; }
  ID3D11VertexShader *vs = nullptr; ID3D11PixelShader *ps = nullptr;
  ID3DBlob *vb = mode == 'C' ? vsid : vspos;
  d->CreateVertexShader(vb->GetBufferPointer(), vb->GetBufferSize(), 0, &vs);
  d->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), 0, &ps);
  ID3D11InputLayout *il = nullptr;
  if (mode == 'B') {
    const D3D11_INPUT_ELEMENT_DESC el[] = {{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    if (FAILED(d->CreateInputLayout(el, 1, vspos->GetBufferPointer(), vspos->GetBufferSize(), &il))) { printf("FAIL layout\n"); return 2; }
  }
  D3D11_TEXTURE2D_DESC rd = {W, H, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
  ID3D11Texture2D *rt = nullptr, *st = nullptr; ID3D11RenderTargetView *rtv = nullptr;
  if (FAILED(d->CreateTexture2D(&rd, 0, &rt)) || FAILED(d->CreateRenderTargetView(rt, 0, &rtv))) { printf("FAIL rt\n"); return 2; }
  rd.Usage = D3D11_USAGE_STAGING; rd.BindFlags = 0; rd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  if (FAILED(d->CreateTexture2D(&rd, 0, &st))) { printf("FAIL staging\n"); return 2; }
  const float clear[4] = {1, 0, 0, 1}; c->ClearRenderTargetView(rtv, clear);
  D3D11_VIEWPORT vp = {0, 0, (float)W, (float)H, 0, 1};
  c->OMSetRenderTargets(1, &rtv, 0); c->RSSetViewports(1, &vp);
  c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  c->IASetInputLayout(il); c->VSSetShader(vs, 0, 0); c->PSSetShader(ps, 0, 0);
  printf("case %c draw\n", mode);
  c->Draw(3, 0);
  c->CopyResource(st, rt);
  printf("case %c map\n", mode);
  DWORD t0 = GetTickCount();
  D3D11_MAPPED_SUBRESOURCE m; HRESULT hr = c->Map(st, 0, D3D11_MAP_READ, 0, &m);
  DWORD ms = GetTickCount() - t0;
  if (FAILED(hr)) { printf("case %c map hr=0x%08lx after %lu ms removed=0x%08lx\n", mode, hr, ms, d->GetDeviceRemovedReason()); return 3; }
  unsigned px = ((unsigned *)((char *)m.pData + 32 * m.RowPitch))[32];
  c->Unmap(st, 0);
  printf("case %c done in %lu ms centre=%08x removed=0x%08lx\n", mode, ms, px, d->GetDeviceRemovedReason());
  return 0;
}
