RWByteAddressBuffer first : register(u0, space1);
RWByteAddressBuffer second : register(u0, space2);
cbuffer Values : register(b0, space3) { uint a; uint b; };
[numthreads(64,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
    first.Store(id.x * 4, id.x * 3 + a);
    second.Store(id.x * 4, id.x * 5 + b);
}
