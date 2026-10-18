struct V { float4 p : SV_Position; };
[maxvertexcount(1)]
void main(point V i[1], inout PointStream<V> s) { s.Append(i[0]); }
