struct VI { uint id : ID; };
VI main(uint id : SV_VertexID) { VI o; o.id = id; return o; }
