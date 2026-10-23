// ps_5_0 (D3D12 C53T, Tiled Resources Tier 2): the feedback (_s) opcodes and
// CheckAccessFullyMapped: Sample with clamp, SampleBias/SampleGrad with clamp,
// SampleLevel, SampleCmpLevelZero, Load, Gather and a raw buffer Load, each
// with a status the shader tests.
Texture2D t : register(t0);
ByteAddressBuffer raw : register(t1);
SamplerState s : register(s0);
SamplerComparisonState sc : register(s1);
float4 main(float2 uv : TEXCOORD0) : SV_Target {
  uint st[8];
  float4 a = t.Sample(s, uv, int2(0, 0), 1.5, st[0]);
  float4 b = t.SampleBias(s, uv, 0.5, int2(0, 0), 2.0, st[1]);
  float4 c = t.SampleGrad(s, uv, ddx(uv), ddy(uv), int2(0, 0), 0.5, st[2]);
  float4 d = t.SampleLevel(s, uv, 1.0, int2(0, 0), st[3]);
  float e = t.SampleCmpLevelZero(sc, uv, 0.5, int2(0, 0), st[4]);
  float4 f = t.Load(int3(uv * 16, 0), int2(0, 0), st[5]);
  float4 g = t.Gather(s, uv, int2(0, 0), st[6]);
  uint h = raw.Load(uint(uv.x * 64) * 4, st[7]);
  uint mapped = 0;
  [unroll] for (uint i = 0; i < 8; ++i) mapped += CheckAccessFullyMapped(st[i]) ? (1u << i) : 0u;
  return a + b + c + d + e + f + g + float(h) + float(mapped);
}
