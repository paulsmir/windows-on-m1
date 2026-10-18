cbuffer C : register(b0) { float4 rect; float4 zc; float4 colour; };
float4 main(uint id : SV_VertexID) : SV_Position {
  float2 c;
  if (zc.y > 0.5) c = id == 0 ? float2(-1, 1) : id == 1 ? float2(1, -1) : float2(-1, -1);
  else c = float2((id == 1 || id == 3 || id == 4) ? rect.z : rect.x,
                  (id == 2 || id == 4 || id == 5) ? rect.w : rect.y);
  return float4(c, zc.x, 1);
}
