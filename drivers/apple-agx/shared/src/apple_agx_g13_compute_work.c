#include "apple_agx_g13_compute_work.h"

#define COMPUTE_NULL ((void *)0)

static void put32(unsigned char *p, APPLE_AGX_BACKEND_U32 v) {
  p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);
  p[2]=(unsigned char)(v>>16);p[3]=(unsigned char)(v>>24);
}
static void put64(unsigned char *p, APPLE_AGX_BACKEND_U64 v) {
  put32(p,(APPLE_AGX_BACKEND_U32)v);put32(p+4,(APPLE_AGX_BACKEND_U32)(v>>32));
}
static void zero(void *p, APPLE_AGX_BACKEND_U32 n) {
  unsigned char *b=(unsigned char *)p;
  for(APPLE_AGX_BACKEND_U32 i=0;i<n;++i)b[i]=0;
}
static APPLE_AGX_BACKEND_BOOL aligned(APPLE_AGX_BACKEND_U64 v,
                                      APPLE_AGX_BACKEND_U64 a) {
  return v && (v&(a-1u))==0u;
}

APPLE_AGX_BACKEND_BOOL AppleAgxG13ComputeWorkBuild(
    const APPLE_AGX_G13_COMPUTE_WORK_INPUT *i,
    unsigned char work[APPLE_AGX_G13_COMPUTE_WORK_BYTES]) {
  APPLE_AGX_BACKEND_U64 p2,p3,p4,p5;
  if(!i||!work||!i->Counter||i->VmSlot>=0x10u||
     !aligned(i->NotifierGpuAddress,8u)||
     !aligned(i->PreemptionGpuAddress,0x40u)||
     !aligned(i->CdmStreamBase,4u)||i->CdmStreamEnd<=i->CdmStreamBase||
     !aligned(i->UscExecutionBase,0x1000u)||
     !aligned(i->MicrosequenceGpuAddress,4u)||!i->MicrosequenceBytes||
     (i->MicrosequenceBytes&3u)||
     !aligned(i->StampGpuAddress,4u)||
     !aligned(i->FirmwareStampGpuAddress,4u)||!i->StampValue||
     i->StampSlot>=0x80u||i->EventControlIndex>=0x80u||
     i->EventSequence==0u||i->ClientSequence>0xffu||
     ((i->HelperProgram==0u)!=(i->HelperArgument==0u))||
     ((i->HelperProgram==0u)!=(i->HelperConfig==0u)))
    return APPLE_AGX_BACKEND_FALSE;
  if(i->PreemptionGpuAddress>~0ULL-(APPLE_AGX_G13_COMPUTE_PREEMPT_BYTES-8u))
    return APPLE_AGX_BACKEND_FALSE;
  p2=i->PreemptionGpuAddress+0x7f80u;
  p3=i->PreemptionGpuAddress+0x7f88u;
  p4=i->PreemptionGpuAddress+0x7f90u;
  p5=i->PreemptionGpuAddress+0x7f98u;
  zero(work,APPLE_AGX_G13_COMPUTE_WORK_BYTES);
  put32(work+0x000u,APPLE_AGX_G13_COMPUTE_TAG);
  put64(work+0x004u,i->Counter);
  put32(work+0x010u,i->VmSlot);
  put64(work+0x014u,i->NotifierGpuAddress);
  /* raw::JobParameters1 begins at 0x70 for G13/V13_5. */
  put64(work+0x070u,i->PreemptionGpuAddress);
  put64(work+0x078u,i->CdmStreamBase);
  put64(work+0x080u,p2);put64(work+0x088u,p3);
  put64(work+0x090u,p4);put64(work+0x098u,p5);
  put64(work+0x0a0u,i->UscExecutionBase);
  put64(work+0x0a8u,0x8c60u);
  put32(work+0x0b0u,i->HelperProgram);
  put64(work+0x0b8u,i->HelperArgument);
  put32(work+0x0c0u,i->HelperConfig);
  put32(work+0x0c8u,1u);
  put64(work+0x1f0u,i->MicrosequenceGpuAddress);
  put32(work+0x1f8u,i->MicrosequenceBytes);
  /* V13_5 JobParameters2 begins at 0x1fc. */
  put64(work+0x224u,i->PreemptionGpuAddress);
  put64(work+0x22cu,i->CdmStreamEnd);
  /* JobMeta begins at 0x284 with packed firmware pointers. */
  put64(work+0x288u,i->StampGpuAddress);
  put64(work+0x290u,i->FirmwareStampGpuAddress);
  put32(work+0x298u,i->StampValue);
  put32(work+0x29cu,i->StampSlot);
  put32(work+0x2a0u,i->EventControlIndex);
  put32(work+0x2acu,i->EventSequence);
  work[0x2d8u]=(unsigned char)i->ClientSequence;
  return APPLE_AGX_BACKEND_TRUE;
}

APPLE_AGX_BACKEND_BOOL AppleAgxG13ComputeMicrosequenceBuild(
    const APPLE_AGX_G13_COMPUTE_MICROSEQUENCE_INPUT *i,
    unsigned char seq[APPLE_AGX_G13_COMPUTE_MICROSEQUENCE_BYTES]) {
  const APPLE_AGX_BACKEND_U32 finalize=0x170u;
  if(!i||!seq||!aligned(i->WorkGpuAddress,4u)||
     !aligned(i->StatisticsGpuAddress,4u)||
     !aligned(i->QueueInfoGpuAddress,8u)||
     !aligned(i->NotifierBufferGpuAddress,8u)||
     !aligned(i->FirmwareStampGpuAddress,4u)||!i->Counter||
     !i->EventSequence||!i->EventGeneration||i->VmSlot>=0x10u||
     !i->StampValue||i->WorkGpuAddress>~0ULL-0x309u)
    return APPLE_AGX_BACKEND_FALSE;
  zero(seq,APPLE_AGX_G13_COMPUTE_MICROSEQUENCE_BYTES);
  /* StartCompute::ver<G13,V13_5>, no attachments or timestamps. */
  put32(seq+0x000u,0x29u);
  put64(seq+0x004u,i->WorkGpuAddress+0x1cu);
  put64(seq+0x00cu,i->WorkGpuAddress+0x70u);
  put64(seq+0x014u,i->StatisticsGpuAddress);
  put64(seq+0x01cu,i->QueueInfoGpuAddress);
  put32(seq+0x024u,i->VmSlot);put32(seq+0x028u,1u);
  put32(seq+0x02cu,i->EventGeneration);
  put64(seq+0x030u,i->EventSequence);
  put64(seq+0x03cu,i->WorkGpuAddress+0x1fcu);
  put64(seq+0x154u,i->WorkGpuAddress+0x305u);
  put64(seq+0x15cu,i->Counter);
  put64(seq+0x164u,i->NotifierBufferGpuAddress);
  /* WaitForIdle(Compute). */
  put32(seq+0x16cu,0x00800001u);
  /* FinalizeCompute::ver<G13,V13_5>. */
  put32(seq+finalize+0x00u,0x2au);
  put64(seq+finalize+0x04u,i->StatisticsGpuAddress);
  put64(seq+finalize+0x0cu,i->QueueInfoGpuAddress);
  put32(seq+finalize+0x14u,i->VmSlot);
  put64(seq+finalize+0x18u,i->WorkGpuAddress+0x1fcu);
  put64(seq+finalize+0x28u,i->FirmwareStampGpuAddress);
  put32(seq+finalize+0x30u,i->StampValue);
  put32(seq+finalize+0x58u,(APPLE_AGX_BACKEND_U32)(0u-finalize));
  put64(seq+finalize+0x6du,i->WorkGpuAddress+0x305u);
  /* RetireStamp with the source-defined 0x40000000 argument. */
  put32(seq+0x1ecu,0x40000018u);
  return APPLE_AGX_BACKEND_TRUE;
}
