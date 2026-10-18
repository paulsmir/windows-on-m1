cbuffer C : register(b0) { float4 offset; };
struct V { float4 p : SV_Position; float4 c : COLOR0; };
V main(uint vertex : SV_VertexID) {
  V v; uint id = vertex / 6, j = vertex % 6;
  float2 centre = float2((id & 1) ? 0.5 : -0.5, (id & 2) ? -0.5 : 0.5);
  float2 d = j == 0 ? float2(0.03, 0) : j == 2 ? float2(-0.03, 0) :
             j == 4 ? float2(0, 0) : float2(0.5, 0.5);
  v.p = float4(centre + d + offset.xy, 0, 1);
  v.c = float4((id == 0 || id == 3) ? 1 : 0, (id == 1 || id == 3) ? 1 : 0,
               id == 2 ? 1 : 0, 1);
  return v;
}
