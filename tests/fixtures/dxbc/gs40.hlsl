// gs_4_0: triangle input, stream output, emit and cut, primitive id.
struct V { float4 pos : SV_Position; float3 n : NORMAL; };
struct G { float4 pos : SV_Position; float3 n : NORMAL; uint prim : TEXCOORD0; uint layer : SV_RenderTargetArrayIndex; };
[maxvertexcount(6)]
void main(triangle V input[3], uint pid : SV_PrimitiveID, inout TriangleStream<G> stream) {
  for (int copy = 0; copy < 2; ++copy) {
    [unroll] for (int v = 0; v < 3; ++v) {
      G g; g.pos = input[v].pos + float4(copy, 0, 0, 0); g.n = normalize(input[v].n);
      g.prim = pid; g.layer = copy; stream.Append(g);
    }
    stream.RestartStrip();
  }
}
