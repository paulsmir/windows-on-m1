// ps_4_0: textures and samplers of every 4.0 dimension, sample variants,
// ld, discard, switch, derivatives, depth and two targets.
Texture2D t2d : register(t0); TextureCube tcube : register(t1);
Texture2DArray<float4> tarr : register(t2); Texture3D<float4> t3d : register(t3);
Texture1D<float> t1d : register(t4); Buffer<uint4> buf : register(t5);
Texture2D<float> shadow : register(t6);
SamplerState s0 : register(s0); SamplerComparisonState scmp : register(s1);
cbuffer C : register(b0) { float4 params; int mode; };
struct PSIn { float4 pos : SV_Position; centroid float2 uv : TEXCOORD0; noperspective float3 dir : TEXCOORD1; bool front : SV_IsFrontFace; };
struct PSOut { float4 c0 : SV_Target0; float4 c1 : SV_Target1; float depth : SV_Depth; };
PSOut main(PSIn i) {
  PSOut o;
  float4 c = t2d.Sample(s0, i.uv);
  c += tcube.SampleLevel(s0, i.dir, params.x);
  c += tarr.SampleGrad(s0, float3(i.uv, 2), ddx(i.uv), ddy(i.uv));
  c += t3d.SampleBias(s0, i.dir, params.y, int3(1, -2, 3));
  c.x += t1d.Load(int2(i.pos.x, 0));
  c.y += shadow.SampleCmp(scmp, i.uv, params.z) + shadow.SampleCmpLevelZero(scmp, i.uv, params.w);
  uint4 b = buf.Load((int)i.pos.y);
  uint w, h, levels; t2d.GetDimensions(0, w, h, levels);
  switch (mode) {
  case 0: c *= 2; break;
  case 1: c = c.wzyx; break;
  default: c += float(b.x + w + h) * 0.001f; break;
  }
  if (c.a < 0.01f) discard;
  o.c0 = i.front ? c : -c;
  o.c1 = float4(exp2(c.x), log2(abs(c.y) + 1), round(c.z), trunc(c.w));
  o.depth = saturate(i.pos.z + ceil(c.x) * floor(c.y));
  return o;
}
