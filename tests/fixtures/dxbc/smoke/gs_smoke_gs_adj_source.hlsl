struct V { float4 p : SV_Position; float4 c : COLOR0; };
[maxvertexcount(4)]
void main(triangleadj V input[6], inout TriangleStream<V> s) {
  const float h = 0.25;
  float4 centre = (input[0].p + input[2].p + input[4].p) / 3;
  V o; o.c = input[0].c;
  o.p = centre + float4(-h,  h, 0, 0); s.Append(o);
  o.p = centre + float4( h,  h, 0, 0); s.Append(o);
  o.p = centre + float4(-h, -h, 0, 0); s.Append(o);
  o.p = centre + float4( h, -h, 0, 0); s.Append(o);
}
