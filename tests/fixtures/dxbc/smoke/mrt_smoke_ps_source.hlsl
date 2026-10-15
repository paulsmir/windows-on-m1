cbuffer C : register(b0) { uint iteration; uint3 pad; };
float4 colour(uint t) {
  uint r = ((t + 1) * 0x20 + iteration * 4) & 0xff;
  uint g = (0xff - t * 0x30 - iteration * 2) & 0xff;
  uint b = (t * 0x40) & 0xff;
  return float4(r, g, b, 255) / 255.0;
}
struct O { float4 c0 : SV_Target0; float4 c1 : SV_Target1;
           float4 c2 : SV_Target2; float4 c3 : SV_Target3;
           float4 c4 : SV_Target4; float4 c5 : SV_Target5;
           float4 c6 : SV_Target6; float4 c7 : SV_Target7; };
O main(float4 p : SV_Position) {
  O o; o.c0 = colour(0); o.c1 = colour(1); o.c2 = colour(2); o.c3 = colour(3);
  o.c4 = colour(4); o.c5 = colour(5); o.c6 = colour(6); o.c7 = colour(7);
  return o;
}
