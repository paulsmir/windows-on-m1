// gs_5_0: instancing and multiple output streams.
struct V { float4 pos : SV_Position; };
struct S0 { float4 pos : SV_Position; };
struct S1 { float4 data : TEXCOORD0; };
[instance(3)] [maxvertexcount(4)]
void main(point V p[1], uint inst : SV_GSInstanceID, inout PointStream<S0> a, inout PointStream<S1> b) {
  S0 x; x.pos = p[0].pos + inst; a.Append(x); a.RestartStrip();
  S1 y; y.data = p[0].pos * inst; b.Append(y); b.RestartStrip();
}
