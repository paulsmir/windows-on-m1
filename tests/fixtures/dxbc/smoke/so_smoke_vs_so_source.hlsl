cbuffer C : register(b0) { float base; float3 pad; };
struct V { float4 pos : SV_Position; float2 a : TEXCOORD0; float4 b : TEXCOORD1; };
V main(uint id : SV_VertexID) {
  V v; float f = (float)id + base;
  v.pos = float4(f, f * 2, 0.5, 1); v.a = float2(f + 0.25, f + 0.5);
  v.b = float4(f * 10, f * 10 + 1, f * 10 + 2, f * 10 + 3);
  return v;
}
