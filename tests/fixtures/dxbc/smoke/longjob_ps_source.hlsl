cbuffer C : register(b0) { uint n; uint3 pad; };
float4 main(float4 p : SV_Position) : SV_Target {
  float a = p.x * 0.001 + p.y * 0.002;
  [loop] for (uint i = 0; i < n; ++i) { a = a * 0.9999 + 0.0001; }
  return float4(a, a, a, 1);
}
