Texture2D<float> depth : register(t0);
float main(float4 p : SV_Position) : SV_Target { return depth.Load(int3(p.xy, 0)); }
