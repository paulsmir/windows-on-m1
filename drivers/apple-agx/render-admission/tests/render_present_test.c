#include "render_present.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>

typedef struct { unsigned int Pixels[8]; unsigned int Reads, Writes, FailRead; } MEMORY;
typedef struct { unsigned char Source[32768], Destination[32768]; unsigned int Reads, Writes; } GPUVA_MEMORY;
static int translate_gpuva(void *opaque, unsigned long long va, unsigned long long *ipa) {
  (void)opaque;
  if (va >= 0x100000ULL && va < 0x108000ULL) {
    *ipa=(va < 0x104000ULL ? 0x900000ULL : 0xb00000ULL)+(va & 0x3fffULL);return 1;
  }
  if (va >= 0x200000ULL && va < 0x208000ULL) {
    *ipa=(va < 0x204000ULL ? 0xa00000ULL : 0xc00000ULL)+(va & 0x3fffULL);return 1;
  }
  return 0;
}
static int read_gpuva(void *opaque, unsigned long long ipa, void *bytes, unsigned int count) {
  GPUVA_MEMORY *memory=opaque;unsigned int offset;
  if (ipa>=0x900000ULL && ipa+count<=0x904000ULL) offset=(unsigned int)(ipa-0x900000ULL);
  else if (ipa>=0xb00000ULL && ipa+count<=0xb04000ULL) offset=0x4000u+(unsigned int)(ipa-0xb00000ULL);
  else return 0;
  ++memory->Reads;memcpy(bytes,memory->Source+offset,count);return 1;
}
static int write_gpuva(void *opaque, unsigned long long ipa, const void *bytes, unsigned int count, int commit) {
  GPUVA_MEMORY *memory=opaque;unsigned int offset;
  if (ipa>=0xa00000ULL && ipa+count<=0xa04000ULL) offset=(unsigned int)(ipa-0xa00000ULL);
  else if (ipa>=0xc00000ULL && ipa+count<=0xc04000ULL) offset=0x4000u+(unsigned int)(ipa-0xc00000ULL);
  else return 0;
  if (commit) {++memory->Writes;memcpy(memory->Destination+offset,bytes,count);}
  return 1;
}
static int read_source(void *opaque, unsigned long long offset, void *bytes, unsigned int count) {
  MEMORY *m = opaque;
  ++m->Reads;
  if (m->FailRead == m->Reads) return 0;
  assert(offset <= sizeof(m->Pixels) && count <= sizeof(m->Pixels) - offset);
  memcpy(bytes, (unsigned char *)m->Pixels + offset, count); return 1;
}
static int write_destination(void *opaque, unsigned long long offset, void *bytes, unsigned int count) {
  MEMORY *m = opaque; ++m->Writes;
  assert(offset <= sizeof(m->Pixels) && count <= sizeof(m->Pixels) - offset);
  memcpy((unsigned char *)m->Pixels + offset, bytes, count); return 1;
}
int main(void) {
  ADMISSION_PRESENT_BLT_INPUT input = {0}; ADMISSION_PRESENT_BLT_COMMAND decoded;
  APPLE_AGX_GDI_RECT rects[2] = {{1,0,2,2},{2,0,4,2}};
  unsigned char command[4096], scratch[24]; unsigned int used, next, segment;
  unsigned long long address, copied;
  MEMORY m = {{11,12,13,14,21,22,23,24},0,0,0};
  const unsigned int expected[8] = {11,11,12,13,21,21,22,23};
  assert(sizeof(ADMISSION_PRESENT_BLT_COMMAND) == 168);
  assert(offsetof(ADMISSION_PRESENT_BLT_COMMAND, SourceLocation) == 144);
  assert(offsetof(ADMISSION_PRESENT_BLT_COMMAND, DestinationLocation) == 152);
  assert(AdmissionAllocationDescribe(4,2,4,1,21,1,&input.Command.SourceDescription));
  assert(AdmissionAllocationDescribe(4,2,4,1,21,0,&input.Command.DestinationDescription));
  input.Command.SourceRect=(APPLE_AGX_GDI_RECT){0,0,3,2};
  input.Command.DestinationRect=(APPLE_AGX_GDI_RECT){1,0,4,2};
  input.Command.ContextToken=7; input.Rects=rects;input.RectCount=2;
  input.SameAllocation=1;
  assert(AdmissionPresentLocationEncode(2,0x1500000000ULL,&input.Command.SourceLocation));
  input.Command.DestinationLocation=input.Command.SourceLocation;
  assert(AdmissionPresentLocationDecode(input.Command.SourceLocation,&segment,&address));
  assert(segment==2 && address==0x1500000000ULL);
  assert(!AdmissionPresentLocationEncode(0,0x1500000000ULL,&address));
  assert(!AdmissionPresentLocationEncode(1,0x100000000000000ULL,&address));
  assert(AdmissionPresentBltEncode(&input,command,sizeof(command),&used,&next));
  assert(used==200 && next==2);
  assert(AdmissionPresentBltValidate(command,used,1,&decoded));
  assert(AdmissionPresentBltExecute(command,used,read_source,write_destination,&m,scratch,sizeof(scratch),&copied));
  assert(copied==24 && m.Reads==2 && m.Writes==4);
  assert(memcmp(expected,m.Pixels,sizeof(expected))==0);
  m=(MEMORY){{11,12,13,14,21,22,23,24},0,0,2};
  assert(!AdmissionPresentBltExecute(command,used,read_source,write_destination,&m,scratch,sizeof(scratch),&copied));
  assert(m.Writes==0 && m.Pixels[1]==12 && copied==0);
  memset(command,0xa5,sizeof(command));
  assert(!AdmissionPresentBltEncode(&input,command,184,&used,&next));
  assert(command[0]==0xa5); /* reject overlap split before first write */
  input.SameAllocation=0;
  assert(AdmissionPresentBltEncode(&input,command,184,&used,&next));
  assert(used==184 && next==1);
  input.MultipassOffset=next;
  assert(AdmissionPresentBltEncode(&input,command,184,&used,&next));
  assert(next==2);
  input.MultipassOffset=0;input.Command.SourceLocation=0;
  assert(AdmissionPresentBltEncode(&input,command,sizeof(command),&used,&next));
  assert(AdmissionPresentBltValidate(command,used,0,&decoded));
  assert(!AdmissionPresentBltValidate(command,used,1,&decoded));
  {
    GPUVA_MEMORY gpuva={0};
    unsigned char copy_scratch[32768];
    ADMISSION_PRESENT_BLT_INPUT scattered={0};
    APPLE_AGX_GDI_RECT region={0,0,8192,1};
    assert(AdmissionAllocationDescribe(8192,1,4,1,21,1,&scattered.Command.SourceDescription));
    assert(AdmissionAllocationDescribe(8192,1,4,1,21,0,&scattered.Command.DestinationDescription));
    scattered.Command.Version=ADMISSION_PRESENT_BLT_GPUVA_VERSION;
    scattered.Command.SourceLocation=0x100000ULL;
    scattered.Command.DestinationLocation=0x200000ULL;
    scattered.Command.ContextToken=7;
    scattered.Command.SourceRect=scattered.Command.DestinationRect=region;
    scattered.Rects=&region;scattered.RectCount=1;
    memset(gpuva.Source,0x5a,sizeof(gpuva.Source));
    assert(AdmissionPresentBltEncode(&scattered,command,sizeof(command),&used,&next));
    assert(AdmissionPresentBltExecuteGpuva(command,used,translate_gpuva,read_gpuva,
        write_gpuva,&gpuva,copy_scratch,sizeof(copy_scratch),&copied));
    assert(copied==32768 && gpuva.Reads>=2 && gpuva.Writes>=2);
    assert(memcmp(gpuva.Source,gpuva.Destination,sizeof(gpuva.Source))==0);
  }
  input.Command.SourceRect.Right=4; /* stretching is not a 1:1 BLT */
  memset(command,0xa5,sizeof(command));
  assert(!AdmissionPresentBltEncode(&input,command,sizeof(command),&used,&next));
  assert(command[0]==0xa5);
  input.Command.SourceRect.Right=3;rects[1].Right=5;
  assert(!AdmissionPresentBltEncode(&input,command,sizeof(command),&used,&next));
  rects[1].Right=4;
  input.Command.Version=ADMISSION_PRESENT_BLT_GPUVA_VERSION;
  input.Command.SourceLocation=0x100000ULL;
  input.Command.DestinationLocation=0x200000ULL;
  assert(AdmissionPresentBltEncode(&input,command,sizeof(command),&used,&next));
  assert(AdmissionPresentBltValidate(command,used,1,&decoded));
  assert(decoded.Version==ADMISSION_PRESENT_BLT_GPUVA_VERSION);
  assert(decoded.SourceLocation==0x100000ULL);
  assert(decoded.DestinationLocation==0x200000ULL);
  m=(MEMORY){{11,12,13,14,21,22,23,24},0,0,0};
  assert(AdmissionPresentBltExecute(command,used,read_source,write_destination,&m,scratch,sizeof(scratch),&copied));
  assert(memcmp(expected,m.Pixels,sizeof(expected))==0);
  input.Command.SourceLocation=0x3000ULL;
  assert(AdmissionPresentBltEncode(&input,command,sizeof(command),&used,&next));
  assert(!AdmissionPresentBltValidate(command,used,1,&decoded));
  return 0;
}
