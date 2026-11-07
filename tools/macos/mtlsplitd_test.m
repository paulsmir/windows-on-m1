// Regression test for tools/macos/mtlsplitd.m (EXP2226): setBuffer/setBufferOffset/setBytes on one
// index must replay in call order. Build: clang -fobjc-arc -framework Metal -framework Foundation
// mtlsplitd_test.m -o t; run: MTLSPLITD_ENC=all DYLD_INSERT_LIBRARIES=./mtlsplitd.dylib ./t -> "mismatches 0".
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
static NSString *src=@"#include <metal_stdlib>\nusing namespace metal;\n"
 "kernel void addk(device uint *b [[buffer(0)]], constant uint &k [[buffer(1)]], uint i [[thread_position_in_grid]]){ for(uint j=0;j<2000;j++) b[i]=b[i]*1664525u+k; }\n"
 "kernel void mul2(device uint *b [[buffer(0)]], uint i [[thread_position_in_grid]]){ b[i]=b[i]*2u; }\n";
int main(){ @autoreleasepool{
 id<MTLDevice> d=MTLCreateSystemDefaultDevice(); NSError*e=nil;
 id<MTLLibrary> l=[d newLibraryWithSource:src options:nil error:&e]; if(!l){NSLog(@"%@",e);return 1;}
 id<MTLComputePipelineState> pa=[d newComputePipelineStateWithFunction:[l newFunctionWithName:@"addk"] error:&e];
 id<MTLComputePipelineState> pm=[d newComputePipelineStateWithFunction:[l newFunctionWithName:@"mul2"] error:&e];
 const uint N=1<<20; id<MTLBuffer> b=[d newBufferWithLength:N*8 options:MTLResourceStorageModeShared];
 uint *p=b.contents; for(uint i=0;i<2*N;i++) p[i]=i;
 typedef struct {uint x,y,z;} G; id<MTLBuffer> ind=[d newBufferWithLength:12 options:MTLResourceStorageModeShared]; ((G*)ind.contents)[0]=(G){N/256,1,1};
 id<MTLCommandQueue> q=[d newCommandQueue];
 for(int f=0;f<3;f++){
  id<MTLCommandBuffer> cb=[q commandBuffer];
  id<MTLComputeCommandEncoder> c=[cb computeCommandEncoder];
  [c setComputePipelineState:pa]; [c setBuffer:b offset:0 atIndex:0];
  uint k=7; [c setBytes:&k length:4 atIndex:1]; k=99; // app reuses its memory
  [c dispatchThreadgroups:MTLSizeMake(N/256,1,1) threadsPerThreadgroup:MTLSizeMake(256,1,1)];
  [c memoryBarrierWithScope:MTLBarrierScopeBuffers];
  [c setComputePipelineState:pm];
  [c dispatchThreadgroupsWithIndirectBuffer:ind indirectBufferOffset:0 threadsPerThreadgroup:MTLSizeMake(256,1,1)];
  [c setComputePipelineState:pa]; uint k2=3; [c setBytes:&k2 length:4 atIndex:1];
  [c dispatchThreads:MTLSizeMake(N,1,1) threadsPerThreadgroup:MTLSizeMake(256,1,1)];
  [c setComputePipelineState:pm]; [c setBufferOffset:N*4 atIndex:0];
  [c dispatchThreads:MTLSizeMake(N,1,1) threadsPerThreadgroup:MTLSizeMake(256,1,1)];
  [c setBytes:&k2 length:4 atIndex:0];
  [c setBuffer:b offset:N*4 atIndex:0];
  [c dispatchThreads:MTLSizeMake(N,1,1) threadsPerThreadgroup:MTLSizeMake(256,1,1)];
  [c endEncoding]; [cb commit]; [cb waitUntilCompleted];
 }
 // CPU reference
 uint *r=malloc(N*4); for(uint i=0;i<N;i++) r[i]=i;
 for(int f=0;f<3;f++) for(uint i=0;i<N;i++){ uint v=r[i]; for(int j=0;j<2000;j++) v=v*1664525u+7; v*=2; for(int j=0;j<2000;j++) v=v*1664525u+3; r[i]=v; }
 uint bad=0; for(uint i=0;i<N;i++) bad+=p[i]!=r[i];
 for(uint i=0;i<N;i++) bad+=p[N+i]!=(N+i)*64u;
 printf("mismatches %u\n",bad); return bad?2:0; }}
