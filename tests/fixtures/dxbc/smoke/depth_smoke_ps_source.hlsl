cbuffer C : register(b0) { float4 rect; float4 zc; float4 colour; };
float4 main(float4 p : SV_Position) : SV_Target { return colour; }
