float4 main(uint id : SV_VertexID) : SV_Position {
  float2 c[6] = { float2(-1, 1), float2(1, 1), float2(-1, -1),
                  float2(1, 1), float2(1, -1), float2(-1, -1) };
  return float4(c[id], 0, 1);
}
