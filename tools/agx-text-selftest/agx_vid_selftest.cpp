// SV_VertexID check (EXP1067). The atlas probe showed that a vertex shader
// deriving positions from SV_VertexID draws nothing, with or without an input
// layout, while a POSITION shader with the same buffer is correct. Positions
// here come from a vertex buffer (full-screen triangle); the vertex shader
// passes one value derived from SV_VertexID, as v/8 in a BGRA8 target. At the
// centre pixel the barycentrics are (0.5, 0.25, 0.25), so the expected values
// are: id -> 0.75, (id << 1) & 2 -> 0.5, id & 2 -> 0.5, (float)id * 2 -> 1.5.
// A constant offset in id shows up in the first row; a translation defect in
// the integer operations shows up in the others.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <string.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

static const char *kShader =
  "struct V { float4 p : SV_Position; float v : VALUE; };\n"
  "V vs0(float2 p : POSITION, uint id : SV_VertexID) { V o; o.p = float4(p, 0, 1); o.v = (float)id; return o; }\n"
  "V vs1(float2 p : POSITION, uint id : SV_VertexID) { V o; o.p = float4(p, 0, 1); o.v = (float)((id << 1) & 2); return o; }\n"
  "V vs2(float2 p : POSITION, uint id : SV_VertexID) { V o; o.p = float4(p, 0, 1); o.v = (float)(id & 2); return o; }\n"
  "V vs3(float2 p : POSITION, uint id : SV_VertexID) { V o; o.p = float4(p, 0, 1); o.v = (float)id * 2.0; return o; }\n"
  "V vs4(float2 p : POSITION) { V o; o.p = float4(p, 0, 1); o.v = 1.0; return o; }\n"
  "V vs5(float2 p : POSITION, uint iid : SV_InstanceID) { V o; o.p = float4(p, 0, 1); o.v = (float)iid; return o; }\n"
  "float4 ps(V i) : SV_Target { return float4(0, 0, i.v / 8.0, 1); }\n";

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  const UINT W = 64, H = 64;
  ID3D11Device *d = nullptr; ID3D11DeviceContext *c = nullptr; D3D_FEATURE_LEVEL got;
  D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_10_0};
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1,
                               D3D11_SDK_VERSION, &d, &got, &c))) { printf("FAIL device\n"); return 2; }
  // Rows: 6 = vs0 drawn with StartVertexLocation 5 (zero-based SV_VertexID
  // keeps 0.75; a start-including one gives 5.75).
  const char *names[] = {"vs0", "vs1", "vs2", "vs3", "vs4", "vs5", "vs0"};
  const char *what[] = {"id", "(id<<1)&2", "id&2", "id*2.0", "const1", "iid(2 inst)", "id start=5"};
  const float expect[] = {0.75f, 0.5f, 0.5f, 1.5f, 1.0f, 1.0f, 0.75f};
  const int rows = 7;
  ID3DBlob *vsb[7] = {}, *psb = nullptr, *err = nullptr;
  for (int i = 0; i < rows; ++i)
    if (FAILED(D3DCompile(kShader, strlen(kShader), 0, 0, 0, names[i], "vs_4_0", 0, 0, &vsb[i], &err))) {
      printf("FAIL compile %s\n", names[i]); return 2; }
  D3DCompile(kShader, strlen(kShader), 0, 0, 0, "ps", "ps_4_0", 0, 0, &psb, &err);
  ID3D11VertexShader *vs[7]; ID3D11PixelShader *ps;
  for (int i = 0; i < rows; ++i) d->CreateVertexShader(vsb[i]->GetBufferPointer(), vsb[i]->GetBufferSize(), 0, &vs[i]);
  d->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), 0, &ps);
  // The triangle is repeated at vertex 5 for the StartVertexLocation row.
  const float tri[] = {-1, 1, 3, 1, -1, -3, 0, 0, 0, 0, -1, 1, 3, 1, -1, -3};
  D3D11_BUFFER_DESC bd = {sizeof(tri), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
  D3D11_SUBRESOURCE_DATA vdata = {tri, 0, 0}; ID3D11Buffer *vb; d->CreateBuffer(&bd, &vdata, &vb);
  const D3D11_INPUT_ELEMENT_DESC el[] = {{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}};
  ID3D11InputLayout *il = nullptr; d->CreateInputLayout(el, 1, vsb[4]->GetBufferPointer(), vsb[4]->GetBufferSize(), &il);
  D3D11_TEXTURE2D_DESC rd = {W, H, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
  ID3D11Texture2D *rt = nullptr, *st = nullptr; ID3D11RenderTargetView *rtv = nullptr;
  if (FAILED(d->CreateTexture2D(&rd, 0, &rt)) || FAILED(d->CreateRenderTargetView(rt, 0, &rtv))) { printf("FAIL rt\n"); return 2; }
  rd.Usage = D3D11_USAGE_STAGING; rd.BindFlags = 0; rd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  if (FAILED(d->CreateTexture2D(&rd, 0, &st))) { printf("FAIL staging\n"); return 2; }
  if (!vb || !il) { printf("FAIL vb/il\n"); return 2; }
  D3D11_VIEWPORT vp = {0, 0, (float)W, (float)H, 0, 1};
  int failures = 0;
  for (int i = 0; i < rows; ++i) {
    const float clear[4] = {0, 0, 1, 1}; c->ClearRenderTargetView(rtv, clear);
    UINT stride = 8, off = 0;
    c->OMSetRenderTargets(1, &rtv, 0); c->RSSetViewports(1, &vp);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); c->IASetInputLayout(il);
    c->IASetVertexBuffers(0, 1, &vb, &stride, &off); c->VSSetShader(vs[i], 0, 0); c->PSSetShader(ps, 0, 0);
    if (i == 5) c->DrawInstanced(3, 2, 0, 0);
    else if (i == 6) c->Draw(3, 5);
    else c->Draw(3, 0);
    c->CopyResource(st, rt);
    D3D11_MAPPED_SUBRESOURCE m; float centre = -99, corner = -99;
    if (SUCCEEDED(c->Map(st, 0, D3D11_MAP_READ, 0, &m))) {
      // Pixel centres (31.5, 31.5) and (0.5, 0.5); the exact centre is between pixels.
      // The value is in blue (byte 0 of BGRA8); value = byte / 255 * 8, and an
      // undrawn pixel reads as the clear value 8.0.
      centre = ((unsigned char *)m.pData)[31 * m.RowPitch + 31 * 4 + 0] / 255.0f * 8.0f;
      corner = ((unsigned char *)m.pData)[0] / 255.0f * 8.0f;
      c->Unmap(st, 0);
    }
    bool ok = centre > expect[i] - 0.12f && centre < expect[i] + 0.12f;
    printf("%s %-10s centre=%.4f expect~%.2f corner=%.4f\n", ok ? "PASS" : "FAIL", what[i], centre, expect[i], corner);
    if (!ok) ++failures;
  }
  printf("RESULT failures=%d\n", failures);
  return failures;
}
