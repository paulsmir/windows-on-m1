Texture1D<uint> source1d : register(t0);
RWByteAddressBuffer output : register(u0);
[numthreads(64,1,1)]
void main(uint3 tid : SV_DispatchThreadID) {
 uint width,levels;source1d.GetDimensions(0,width,levels);
 if(tid.x==0) {output.Store(0,width);output.Store(4,1);output.Store(8,1);output.Store(12,levels);}
 if(tid.x<width)output.Store(16+4*tid.x,source1d.Load(int2(tid.x,0)));
}
