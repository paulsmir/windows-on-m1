cbuffer C : register(b0) { float4 offset; };
struct V { float4 p : SV_Position; float4 c : COLOR0; };
V main(uint id : SV_VertexID) {
  V v;
  float2 centre = float2((id & 1) ? 0.5 : -0.5, (id & 2) ? -0.5 : 0.5);
  v.p = float4(centre + offset.xy, 0, 1);
  v.c = float4((id == 0 || id == 3) ? 1 : 0, (id == 1 || id == 3) ? 1 : 0,
               id == 2 ? 1 : 0, 1);
  return v;
}
