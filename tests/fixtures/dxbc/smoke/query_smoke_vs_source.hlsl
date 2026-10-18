cbuffer C : register(b0) { float shift; float3 pad; };
float4 main(uint id : SV_VertexID) : SV_Position {
  float2 uv = float2((id << 1) & 2, id & 2);
  return float4(uv * float2(2, -2) + float2(-1 + shift, 1), 0, 1);
}
