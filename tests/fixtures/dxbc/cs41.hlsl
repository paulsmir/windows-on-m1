// cs_4_1: a raw UAV written at byte addresses, two-dimensional groups and
// thread IDs, an SRV read through a raw view.
ByteAddressBuffer src : register(t0);
RWByteAddressBuffer dst : register(u0);
[numthreads(8, 4, 1)]
void main(uint3 dtid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID) {
  uint index = dtid.y * 32 + dtid.x;
  uint2 v = src.Load2(index * 8);
  dst.Store3(index * 12, uint3(v.x ^ v.y, gtid.x + gtid.y * 8, gid.x | (gid.y << 16)));
}
