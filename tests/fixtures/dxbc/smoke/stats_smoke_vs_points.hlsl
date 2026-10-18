struct V { float4 p : SV_Position; };
V main(uint id : SV_VertexID) { V v; v.p = float4(id, 0, 0, 1); return v; }
