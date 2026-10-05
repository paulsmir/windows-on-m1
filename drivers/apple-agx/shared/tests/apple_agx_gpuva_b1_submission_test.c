#include <assert.h>
#include <string.h>
#include "../include/apple_agx_gpuva_b1_submission.h"
int main(void){
 unsigned char storage[512],dma[32]={1};APPLE_AGX_BACKEND_SUBMISSION sub;
 APPLE_AGX_DMA_SHADOW shadow;APPLE_AGX_DMA_SHADOW_VIEW view;
 assert(AppleAgxGpuvaB1PrepareSubmission(storage,sizeof(storage),dma,sizeof(dma),1,1,&sub));
 assert(sub.PrivateData==storage&&sub.PrivateDataBytes==sizeof(storage));
 assert(sub.PrivateDataEnd<sub.PrivateDataBytes&&sub.DmaSubmissionEnd==sizeof(dma));
 assert(AppleAgxDmaShadowOpen(&shadow,(void *)sub.PrivateData,sub.PrivateDataBytes));
 assert(AppleAgxDmaShadowIsSealedForFence(storage,shadow.BytesUsed,1));
 assert(AppleAgxDmaShadowFind(storage,shadow.BytesUsed,0,sizeof(dma),&view));
 assert(!memcmp(view.Bytes,dma,sizeof(dma)));
 assert(!AppleAgxGpuvaB1PrepareSubmission(storage,16,dma,sizeof(dma),2,1,&sub));
 return 0;
}
