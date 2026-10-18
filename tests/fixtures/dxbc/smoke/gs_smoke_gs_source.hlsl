struct V { float4 p : SV_Position; float4 c : COLOR0; };
[maxvertexcount(4)]
void main(point V input[1], inout TriangleStream<V> s) {
  const float h = 0.25;
  V o; o.c = input[0].c;
  o.p = input[0].p + float4(-h,  h, 0, 0); s.Append(o);
  o.p = input[0].p + float4( h,  h, 0, 0); s.Append(o);
  o.p = input[0].p + float4(-h, -h, 0, 0); s.Append(o);
  o.p = input[0].p + float4( h, -h, 0, 0); s.Append(o);
}
