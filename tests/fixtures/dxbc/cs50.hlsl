// cs_5_0: typed/raw/structured UAVs, append/consume, groupshared memory,
// atomics, barriers, bit operations, half conversion.
RWTexture2D<float4> img : register(u0);
RWStructuredBuffer<uint2> sb : register(u1);
RWByteAddressBuffer raw : register(u2);
AppendStructuredBuffer<float4> app : register(u3);
ConsumeStructuredBuffer<float4> con : register(u4);
StructuredBuffer<float4> srb : register(t0);
ByteAddressBuffer srraw : register(t1);
groupshared uint shared_sum;
groupshared float4 tile[8][8];
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID, uint3 gtid : SV_GroupThreadID, uint3 gid : SV_GroupID, uint gi : SV_GroupIndex) {
  if (gi == 0) shared_sum = 0;
  GroupMemoryBarrierWithGroupSync();
  tile[gtid.y][gtid.x] = srb[gi] + asfloat(srraw.Load4(gi * 16));
  InterlockedAdd(shared_sum, countbits(dtid.x) + firstbithigh(dtid.y) + firstbitlow(gid.x));
  GroupMemoryBarrierWithGroupSync();
  uint old; InterlockedMax(sb[gi].x, dtid.x, old);
  InterlockedCompareStore(sb[gi].y, 0, old);
  raw.InterlockedOr(gi * 4, reversebits(dtid.x));
  raw.Store2(gi * 8, uint2(f32tof16(tile[gtid.x][gtid.y].x), f16tof32(old)));
  uint v; raw.InterlockedCompareExchange(0, dtid.x, gi, v);
  img[dtid.xy] = tile[gtid.x][7 - gtid.y] * (float)shared_sum + float((v >> 3) & 7) + float(((dtid.x << 2) >> 5));
  if (dtid.x == 0) app.Append(con.Consume());
  uint a, b; uint c = 0;
  a = dtid.x + dtid.y; b = dtid.y - dtid.x; c = msad4(a, uint2(b, c), uint4(0,0,0,0)).x;
  DeviceMemoryBarrier();
  AllMemoryBarrierWithGroupSync();
  sb[gi] = uint2(c, sb.IncrementCounter());
}
