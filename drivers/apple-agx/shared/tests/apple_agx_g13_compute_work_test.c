#include "apple_agx_g13_compute_work.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct __attribute__((packed)) { uint64_t v; } AGX_TEST_U64;
typedef struct __attribute__((packed)) { uint64_t v; } AGX_TEST_PTR;
typedef struct __attribute__((packed)) { uint32_t v; } AGX_TEST_U32;
typedef struct {
  AGX_TEST_PTR preempt1; AGX_TEST_U64 cdm; AGX_TEST_PTR preempt2,preempt3,preempt4,preempt5;
  AGX_TEST_U64 usc,unk38; uint32_t helper,unk44; AGX_TEST_U64 helper_arg;
  uint32_t helper_cfg,unk54,unk58,unk5c,iogpu; unsigned char pad[0xfc];
} AGX_TEST_JOB1;
typedef struct {
  uint32_t unk00; unsigned char unk0[0x24]; AGX_TEST_PTR preempt1;
  AGX_TEST_U64 cdm_end; unsigned char unk34[0x20]; uint32_t g14x,unk58;
} AGX_TEST_JOB2;
typedef struct {
  uint32_t a,b,c,d,e,f; AGX_TEST_U64 sampler; uint32_t count,max;
} AGX_TEST_ENCODER;
typedef struct {
  uint16_t unk0; uint8_t unk2,no_preempt; AGX_TEST_PTR stamp,fw_stamp;
  uint32_t value,slot,evctl,flush,uuid,event_seq;
} AGX_TEST_META;
typedef struct { AGX_TEST_PTR start,end; } AGX_TEST_TIMESTAMPS;
typedef struct {
  uint32_t tag; AGX_TEST_U64 counter; uint32_t unk4,vm; AGX_TEST_PTR notifier;
  uint32_t pointee; unsigned char pad0[0x50]; AGX_TEST_JOB1 agx_job1;
  unsigned char pad1[0x20]; AGX_TEST_PTR micro; uint32_t micro_size;
  AGX_TEST_JOB2 agx_job2; AGX_TEST_ENCODER agx_encoder; AGX_TEST_META agx_meta; AGX_TEST_U64 agx_command_time;
  AGX_TEST_TIMESTAMPS agx_timestamps,agx_user_timestamps; uint8_t agx_client,agx_pad2d1[3];
  uint32_t unk2d4; uint8_t unk2d8; AGX_TEST_U64 agx_request,agx_complete;
  unsigned char unk2e9[0x14]; AGX_TEST_U32 agx_flag; unsigned char tail[0x10];
} AGX_TEST_RUN_COMPUTE;

_Static_assert(offsetof(AGX_TEST_RUN_COMPUTE,agx_job1)==0x70,"JobParameters1");
_Static_assert(offsetof(AGX_TEST_RUN_COMPUTE,micro)==0x1f0,"microsequence");
_Static_assert(offsetof(AGX_TEST_RUN_COMPUTE,agx_job2)==0x1fc,"JobParameters2");
_Static_assert(offsetof(AGX_TEST_RUN_COMPUTE,agx_encoder)==0x25c,"EncoderParams");
_Static_assert(offsetof(AGX_TEST_RUN_COMPUTE,agx_meta)==0x284,"JobMeta");
_Static_assert(offsetof(AGX_TEST_RUN_COMPUTE,agx_client)==0x2d8,"client_sequence");
_Static_assert(sizeof(AGX_TEST_RUN_COMPUTE)==APPLE_AGX_G13_COMPUTE_WORK_BYTES,
               "RunCompute bytes");

static unsigned long long get64(const unsigned char *p) {
  unsigned long long v=0;
  for(unsigned i=0;i<8;++i)v|=(unsigned long long)p[i]<<(8u*i);
  return v;
}
static unsigned get32(const unsigned char *p) {
  return (unsigned)get64(p)&0xffffffffu;
}
unsigned AppleAgxG13ComputeWorkContractTests(void) {
  unsigned failures=0;
#undef assert
#define assert(x) do { if(!(x)) ++failures; } while(0)
  APPLE_AGX_G13_COMPUTE_WORK_INPUT i={0};
  unsigned char work[APPLE_AGX_G13_COMPUTE_WORK_BYTES];
  memset(work,0xa5,sizeof(work));
  i.Counter=7;i.VmSlot=2;i.NotifierGpuAddress=0x1500010000ULL;
  i.PreemptionGpuAddress=0x1500020000ULL;
  i.CdmStreamBase=0x1500030000ULL;i.CdmStreamEnd=0x1500030200ULL;
  i.UscExecutionBase=0x1100000000ULL;
  i.MicrosequenceGpuAddress=0x1500040000ULL;i.MicrosequenceBytes=0x100;
  i.StampGpuAddress=0x1500050000ULL;i.FirmwareStampGpuAddress=0x1500051000ULL;
  i.StampValue=9;i.StampSlot=3;i.EventControlIndex=4;i.EventSequence=11;
  i.ClientSequence=5;
  assert(AppleAgxG13ComputeWorkBuild(&i,work));
  assert(get32(work)==3 && get64(work+4)==7 && get32(work+0x10)==2);
  assert(get64(work+0x70)==i.PreemptionGpuAddress);
  assert(get64(work+0x78)==i.CdmStreamBase);
  assert(get64(work+0x80)==i.PreemptionGpuAddress+0x7f80);
  assert(get64(work+0x88)==i.PreemptionGpuAddress+0x7f88);
  assert(get64(work+0x90)==i.PreemptionGpuAddress+0x7f90);
  assert(get64(work+0x98)==i.PreemptionGpuAddress+0x7f98);
  assert(get64(work+0xa0)==i.UscExecutionBase && get64(work+0xa8)==0x8c60);
  assert(get64(work+0x1f0)==i.MicrosequenceGpuAddress);
  assert(get32(work+0x1f8)==i.MicrosequenceBytes);
  assert(get64(work+0x224)==i.PreemptionGpuAddress);
  assert(get64(work+0x22c)==i.CdmStreamEnd);
  assert(get64(work+0x288)==i.StampGpuAddress);
  assert(get64(work+0x290)==i.FirmwareStampGpuAddress);
  assert(get32(work+0x298)==i.StampValue && get32(work+0x29c)==i.StampSlot);
  assert(get32(work+0x2a0)==i.EventControlIndex &&
         get32(work+0x2ac)==i.EventSequence && work[0x2d8]==5);
  assert(work[0x2d9]==0 && work[sizeof(work)-1]==0);
  {
    unsigned char before[sizeof(work)];memset(work,0x5a,sizeof(work));
    memcpy(before,work,sizeof(work));i.CdmStreamEnd=i.CdmStreamBase;
    assert(!AppleAgxG13ComputeWorkBuild(&i,work));
    assert(!memcmp(work,before,sizeof(work)));
  }
  {
    APPLE_AGX_G13_COMPUTE_MICROSEQUENCE_INPUT m={0};
    unsigned char seq[APPLE_AGX_G13_COMPUTE_MICROSEQUENCE_BYTES];
    m.WorkGpuAddress=0x1500100000ULL;m.StatisticsGpuAddress=0x1500200000ULL;
    m.QueueInfoGpuAddress=0x1500300000ULL;
    m.NotifierBufferGpuAddress=0x1500400000ULL;
    m.FirmwareStampGpuAddress=0x1500500000ULL;
    m.Counter=7;m.EventSequence=11;m.EventGeneration=13;m.VmSlot=2;
    m.StampValue=9;
    assert(AppleAgxG13ComputeMicrosequenceBuild(&m,seq));
    assert(get32(seq)==0x29 && get64(seq+0xc)==m.WorkGpuAddress+0x70);
    assert(get32(seq+0x16c)==0x00800001u && get32(seq+0x170)==0x2a);
    assert(get64(seq+0x198)==m.FirmwareStampGpuAddress);
    assert(get32(seq+0x1c8)==0xfffffe90u);
    assert(get64(seq+0x1dd)==m.WorkGpuAddress+0x305);
    assert(get32(seq+0x1ec)==0x40000018u);
  }
  return failures;
}

#if defined(APPLE_AGX_COMPUTE_WORK_STANDALONE)
int main(void) { return (int)AppleAgxG13ComputeWorkContractTests(); }
#endif
