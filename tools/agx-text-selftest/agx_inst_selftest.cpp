// Instancing probe: one quad per instance with a per-instance offset and
// colour (D3D11_INPUT_PER_INSTANCE_DATA), as Direct2D batches glyph quads.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <string.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
static const char *kShader =
  "struct V { float4 p : SV_Position; float4 c : COLOR0; };\n"
  "V vs(float2 p : POSITION, float2 off : OFFSET, uint id : SV_InstanceID) { V o;\n"
  "  o.p = float4(p * 0.25 + off, 0, 1); o.c = float4(0, 0, 0, 1); return o; }\n"
  "V vsid(float2 p : POSITION, uint id : SV_InstanceID) { V o;\n"
  "  float2 off = float2(-0.5 + (id % 2) * 1.0, -0.5 + (id / 2) * 1.0); o.p = float4(p * 0.25 + off, 0, 1); o.c = float4(0,0,0,1); return o; }\n"
  "float4 ps(V i) : SV_Target { return i.c; }\n";
static int dark(ID3D11DeviceContext *c, ID3D11Texture2D *st, ID3D11Texture2D *rt, UINT W, UINT H) {
  c->CopyResource(st, rt); D3D11_MAPPED_SUBRESOURCE m; int n = 0;
  if (SUCCEEDED(c->Map(st, 0, D3D11_MAP_READ, 0, &m))) { for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x)
      if ((((unsigned *)((char *)m.pData + y * m.RowPitch))[x] & 0xff) < 0x80) ++n; c->Unmap(st, 0); }
  return n;
}
int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  const UINT W = 64, H = 64;
  ID3D11Device *d; ID3D11DeviceContext *c; D3D_FEATURE_LEVEL got, lv[] = {D3D_FEATURE_LEVEL_10_0};
  if (FAILED(D3D11CreateDevice(0, D3D_DRIVER_TYPE_HARDWARE, 0, 0, lv, 1, D3D11_SDK_VERSION, &d, &got, &c))) return 2;
  ID3DBlob *vb1, *vb2, *pb, *e = 0;
  D3DCompile(kShader, strlen(kShader), 0, 0, 0, "vs", "vs_4_0", 0, 0, &vb1, &e);
  D3DCompile(kShader, strlen(kShader), 0, 0, 0, "vsid", "vs_4_0", 0, 0, &vb2, &e);
  D3DCompile(kShader, strlen(kShader), 0, 0, 0, "ps", "ps_4_0", 0, 0, &pb, &e);
  ID3D11VertexShader *vs1, *vs2; ID3D11PixelShader *ps;
  d->CreateVertexShader(vb1->GetBufferPointer(), vb1->GetBufferSize(), 0, &vs1);
  d->CreateVertexShader(vb2->GetBufferPointer(), vb2->GetBufferSize(), 0, &vs2);
  d->CreatePixelShader(pb->GetBufferPointer(), pb->GetBufferSize(), 0, &ps);
  const float quad[] = {-1, -1, -1, 1, 1, -1, 1, -1, -1, 1, 1, 1};  // two triangles
  const float offs[] = {-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f};
  D3D11_BUFFER_DESC bd = {sizeof(quad), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
  D3D11_SUBRESOURCE_DATA q = {quad, 0, 0}, o = {offs, 0, 0}; ID3D11Buffer *qb, *ob;
  d->CreateBuffer(&bd, &q, &qb); bd.ByteWidth = sizeof(offs); d->CreateBuffer(&bd, &o, &ob);
  const D3D11_INPUT_ELEMENT_DESC el1[] = {{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"OFFSET", 0, DXGI_FORMAT_R32G32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1}};
  const D3D11_INPUT_ELEMENT_DESC el2[] = {{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}};
  ID3D11InputLayout *il1, *il2;
  d->CreateInputLayout(el1, 2, vb1->GetBufferPointer(), vb1->GetBufferSize(), &il1);
  d->CreateInputLayout(el2, 1, vb2->GetBufferPointer(), vb2->GetBufferSize(), &il2);
  D3D11_TEXTURE2D_DESC rd = {W, H, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0};
  ID3D11Texture2D *rt, *st; d->CreateTexture2D(&rd, 0, &rt); ID3D11RenderTargetView *rtv; d->CreateRenderTargetView(rt, 0, &rtv);
  rd.Usage = D3D11_USAGE_STAGING; rd.BindFlags = 0; rd.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d->CreateTexture2D(&rd, 0, &st);
  D3D11_VIEWPORT vp = {0, 0, (float)W, (float)H, 0, 1}; const float white[4] = {1, 1, 1, 1};
  // Indexed: 4 quads x 4 corners in one buffer, 6 indices per quad.
  float corners[4 * 4 * 2]; unsigned short idx16[24 + 6]; unsigned idx32[24];
  const float cx[4] = {-0.5f, 0.5f, -0.5f, 0.5f}, cy[4] = {-0.5f, -0.5f, 0.5f, 0.5f};
  for (int qd = 0; qd < 4; ++qd) {
    const float px[4] = {-1, -1, 1, 1}, py[4] = {-1, 1, -1, 1};
    for (int k = 0; k < 4; ++k) { corners[(qd * 4 + k) * 2] = px[k] * 0.25f + cx[qd]; corners[(qd * 4 + k) * 2 + 1] = py[k] * 0.25f + cy[qd]; }
    const int tri[6] = {0, 1, 2, 2, 1, 3};
    for (int k = 0; k < 6; ++k) { idx16[6 + qd * 6 + k] = (unsigned short)(qd * 4 + tri[k]); idx32[qd * 6 + k] = qd * 4 + tri[k]; }
  }
  for (int k = 0; k < 6; ++k) idx16[k] = 0;  // 12-byte prefix exercising the IB offset
  D3D11_BUFFER_DESC cbd = {sizeof(corners), D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0};
  D3D11_SUBRESOURCE_DATA cd = {corners, 0, 0}; ID3D11Buffer *cbuf; d->CreateBuffer(&cbd, &cd, &cbuf);
  D3D11_BUFFER_DESC ibd = {sizeof(idx16), D3D11_USAGE_DEFAULT, D3D11_BIND_INDEX_BUFFER, 0, 0, 0};
  D3D11_SUBRESOURCE_DATA i16 = {idx16, 0, 0}, i32 = {idx32, 0, 0}; ID3D11Buffer *ib16, *ib32;
  d->CreateBuffer(&ibd, &i16, &ib16); ibd.ByteWidth = sizeof(idx32); d->CreateBuffer(&ibd, &i32, &ib32);
  const char *kPlain = "float4 vs(float2 p : POSITION) : SV_Position { return float4(p, 0, 1); }\n"
                       "float4 ps() : SV_Target { return float4(0, 0, 0, 1); }\n";
  ID3DBlob *pvb, *ppb; D3DCompile(kPlain, strlen(kPlain), 0, 0, 0, "vs", "vs_4_0", 0, 0, &pvb, &e);
  D3DCompile(kPlain, strlen(kPlain), 0, 0, 0, "ps", "ps_4_0", 0, 0, &ppb, &e);
  ID3D11VertexShader *pvs; ID3D11PixelShader *pps; d->CreateVertexShader(pvb->GetBufferPointer(), pvb->GetBufferSize(), 0, &pvs);
  d->CreatePixelShader(ppb->GetBufferPointer(), ppb->GetBufferSize(), 0, &pps);
  ID3D11InputLayout *il3; d->CreateInputLayout(el2, 1, pvb->GetBufferPointer(), pvb->GetBufferSize(), &il3);
  int fails = 0; const int expect = 4 * (W / 8) * (H / 8) * 4 / 4;  // four quads of 16x16 px
  for (int mode = 0; mode < 8; ++mode) {
    c->ClearRenderTargetView(rtv, white); c->OMSetRenderTargets(1, &rtv, 0); c->RSSetViewports(1, &vp);
    c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); c->PSSetShader(ps, 0, 0);
    UINT strides[2] = {8, 8}, offsets[2] = {0, 0}; ID3D11Buffer *bufs[2] = {qb, ob};
    if (mode == 0) { c->IASetInputLayout(il1); c->IASetVertexBuffers(0, 2, bufs, strides, offsets); c->VSSetShader(vs1, 0, 0); c->DrawInstanced(6, 4, 0, 0); }
    if (mode == 1) { c->IASetInputLayout(il2); c->IASetVertexBuffers(0, 1, bufs, strides, offsets); c->VSSetShader(vs2, 0, 0); c->DrawInstanced(6, 4, 0, 0); }
    if (mode == 2) { c->IASetInputLayout(il1); c->IASetVertexBuffers(0, 2, bufs, strides, offsets); c->VSSetShader(vs1, 0, 0); c->DrawInstanced(6, 2, 0, 2); }
    if (mode >= 3) {
      UINT st8 = 8, z = 0; c->IASetInputLayout(il3); c->IASetVertexBuffers(0, 1, &cbuf, &st8, &z);
      c->VSSetShader(pvs, 0, 0); c->PSSetShader(pps, 0, 0);
      if (mode == 3) { c->IASetIndexBuffer(ib16, DXGI_FORMAT_R16_UINT, 12); c->DrawIndexed(24, 0, 0); }
      if (mode == 4) { c->IASetIndexBuffer(ib32, DXGI_FORMAT_R32_UINT, 0); c->DrawIndexed(24, 0, 0); }
      if (mode == 5) { c->IASetIndexBuffer(ib16, DXGI_FORMAT_R16_UINT, 0); c->DrawIndexed(12, 6, 0); }   // start index
      if (mode == 6) { c->IASetIndexBuffer(ib32, DXGI_FORMAT_R32_UINT, 0); c->DrawIndexed(6, 0, 4); }    // base vertex: quad 1
      if (mode == 7) { c->IASetIndexBuffer(ib32, DXGI_FORMAT_R32_UINT, 0); c->DrawIndexedInstanced(12, 2, 0, 0, 0); }
    }
    int n = dark(c, st, rt, W, H); int want = mode == 2 || mode == 5 ? 512 : mode == 6 ? 256 : mode == 7 ? 512 : 1024;
    bool ok = n >= want * 9 / 10 && n <= want * 11 / 10;
    const char *mn[] = {"instance-attr", "instance-id", "start-instance", "indexed16-iboffset", "indexed32",
                        "indexed16-startindex", "indexed32-basevertex", "indexed-instanced"};
    printf("%s %s dark=%d expect=%d\n", ok ? "PASS" : "FAIL", mn[mode], n, want);
    if (!ok) ++fails;
  }
  printf("RESULT failures=%d\n", fails); return fails;
}
