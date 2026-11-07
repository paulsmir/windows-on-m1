// Metal twin of the D3D12 MEMBW probe step (drivers/apple-agx/windows/icd/agx_d3d12_probe.c,
// agx_d3d12_probe_cs_membw.hlsl): same 128 MiB private buffer, same Sattolo cycles (seed
// 2463534242, regions 16 KiB..64 MiB), same kernel, 1024 and 32 chasing threads, time =
// (11 dispatches - 1 dispatch) / 10, best of three, GPU time from the command buffer.
// Build: clang -fobjc-arc -framework Metal -framework Foundation membw_metal.m -o membw_metal
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
static NSString *src = @"#include <metal_stdlib>\nusing namespace metal;\n"
  "struct P { uint count16, mode, steps, threads, first, lines; };\n"
  "kernel void k(device const uint *s [[buffer(0)]], device uint *o [[buffer(1)]], constant P &p [[buffer(2)]],\n"
  "  uint id [[thread_position_in_grid]]) {\n"
  "  uint4 acc = 0;\n"
  "  if (p.mode == 0) { for (uint i = id; i < p.count16; i += p.threads) acc ^= ((device const uint4 *)s)[i]; }\n"
  "  else { if (id >= p.threads) return; uint node = p.first + (id * 4099u) % p.lines;\n"
  "    for (uint t = 0; t < p.steps; ++t) node = s[node * 16u]; acc.x = node; }\n"
  "  if (acc.x == 0xdeadbeefu && acc.y == 0x01234567u) o[0] = acc.z ^ acc.w;\n"
  "  if (p.mode == 1 && acc.x == 0xffffffffu) o[1] = acc.x; }\n";
int main(void) { @autoreleasepool {
  enum { BYTES = 128u << 20, LINES = BYTES / 64u, STEPS = 512u, REGIONS = 7, CASES = 1 + 2 * REGIONS };
  static const unsigned kib[REGIONS] = {16, 64, 256, 1024, 4096, 16384, 65536};
  unsigned rfirst[REGIONS];
  id<MTLDevice> d = MTLCreateSystemDefaultDevice(); NSError *e = nil;
  id<MTLLibrary> l = [d newLibraryWithSource:src options:nil error:&e]; if (!l) { NSLog(@"%@", e); return 1; }
  id<MTLComputePipelineState> ps = [d newComputePipelineStateWithFunction:[l newFunctionWithName:@"k"] error:&e];
  id<MTLBuffer> up = [d newBufferWithLength:BYTES options:MTLResourceStorageModeShared];
  id<MTLBuffer> buf = [d newBufferWithLength:BYTES options:MTLResourceStorageModePrivate];
  id<MTLBuffer> out = [d newBufferWithLength:64 options:MTLResourceStorageModePrivate];
  unsigned *w = up.contents, *next = malloc(LINES * 4), seed = 2463534242u, first = 0;
  for (unsigned r = 0; r < REGIONS; ++r) {
    unsigned count = kib[r] * 1024u / 64u; rfirst[r] = first;
    for (unsigned i = 0; i < count; ++i) next[first + i] = first + i;
    for (unsigned i = count - 1u; i > 0u; --i) {
      seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
      unsigned j = seed % i, v = next[first + i]; next[first + i] = next[first + j]; next[first + j] = v; }
    first += count; }
  for (unsigned i = first; i < LINES; ++i) next[i] = i;
  for (unsigned i = 0; i < LINES; ++i) { w[i * 16u] = next[i]; for (unsigned k = 1; k < 16u; ++k) w[i * 16u + k] = i ^ (k * 0x9e3779b9u); }
  id<MTLCommandQueue> q = [d newCommandQueue];
  { id<MTLCommandBuffer> cb = [q commandBuffer]; id<MTLBlitCommandEncoder> b = [cb blitCommandEncoder];
    [b copyFromBuffer:up sourceOffset:0 toBuffer:buf destinationOffset:0 size:BYTES]; [b endEncoding]; [cb commit]; [cb waitUntilCompleted]; }
  double best[CASES][2];
  for (int m = 0; m < CASES; ++m) best[m][0] = best[m][1] = 1e30;
  for (int rep = 0; rep < 3; ++rep) for (unsigned m = 0; m < CASES; ++m) for (int k = 0; k < 2; ++k) {
    unsigned mode = m ? 1 : 0, region = m ? (m - 1) % REGIONS : 0, n = k ? 11 : 1;
    unsigned threads = !mode ? 65536u : m > REGIONS ? 32u : 1024u;
    unsigned p[6] = {BYTES / 16u, mode, STEPS, threads, m ? rfirst[region] : 0u, m ? kib[region] * 1024u / 64u : 1u};
    id<MTLCommandBuffer> cb = [q commandBuffer]; id<MTLComputeCommandEncoder> c = [cb computeCommandEncoder];
    [c setComputePipelineState:ps]; [c setBuffer:buf offset:0 atIndex:0]; [c setBuffer:out offset:0 atIndex:1];
    [c setBytes:p length:sizeof p atIndex:2];
    for (unsigned i = 0; i < n; ++i) {
      [c dispatchThreadgroups:MTLSizeMake((threads + 255) / 256, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
      [c memoryBarrierWithScope:MTLBarrierScopeBuffers]; }
    [c endEncoding]; [cb commit]; [cb waitUntilCompleted];
    double ms = (cb.GPUEndTime - cb.GPUStartTime) * 1e3;
    if (ms < best[m][k]) best[m][k] = ms; }
  double sms = (best[0][1] - best[0][0]) / 10.0;
  printf("MEMBW_STREAM_GBPS %.2f (%.3f ms per 128 MiB pass)\n", (double)BYTES / (sms * 1e6), sms);
  for (unsigned m = 1; m < CASES; ++m) {
    double cms = (best[m][1] - best[m][0]) / 10.0;
    printf("MEMBW_CHASE_NS %u KiB %.1f (%u threads x %u dependent loads, %.3f ms per dispatch)\n", kib[(m - 1) % REGIONS],
           cms * 1e6 / STEPS, m > REGIONS ? 32u : 1024u, STEPS, cms); }
  return 0; } }
