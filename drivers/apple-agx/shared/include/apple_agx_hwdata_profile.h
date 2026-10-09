#ifndef APPLE_AGX_HWDATA_PROFILE_H
#define APPLE_AGX_HWDATA_PROFILE_H
#include "apple_agx_firmware_io.h"
#include "apple_agx_hwdata_profile_abi.h"
#include "apple_agx_hwdata_profile.generated.h"

typedef const unsigned char *(*AGX_HWDATA_READ_PROPERTY)(void *,const char *,unsigned int *);
static inline unsigned char AgxHwdataInputsMatch(AGX_HWDATA_READ_PROPERTY read,void *ctx) {
    unsigned int i,j;
    if(!read) return 0;
    for(i=0;i<AGX_HWDATA_INPUT_COUNT;i++) {
        const AGX_HWDATA_INPUT *e=&AgxHwdataInputs[i];
        unsigned int length=0;
        const unsigned char *p=read(ctx,e->Name,&length);
        if(e->Length==0xffffffffu){if(p)return 0;continue;}
        if(!p || length!=e->Length) return 0;
        for(j=0;j<length;j++)if(p[j]!=e->Bytes[j])return 0;
    }
    return 1;
}
static inline unsigned char AgxHwdataReceiptValid(const AGX_HWDATA_RECEIPT *r,
    unsigned long long epoch,unsigned long long root) {
    unsigned int i;
    if(!r || sizeof(*r)!=64 || r->Magic!=AGX_HWDATA_RECEIPT_MAGIC ||
       r->Version!=1 || r->Bytes!=64 || r->Chip!=0x8103 || !epoch ||
       r->Epoch!=epoch || !root || root>=1ULL<<40 || (root&0x3fffULL) || r->Root!=root)
        return 0;
    for(i=0;i<32;i++)if(r->ProfileId[i]!=AgxHwdataProfileId[i])return 0;
    return 1;
}
static inline unsigned char AgxHwdataReceiptWord(const AGX_HWDATA_RECEIPT *r,
    unsigned long long offset,unsigned long long *value,unsigned char write,unsigned width) {
    unsigned int bytes,i;
    if(!r || !value || write || width>3) return 0;
    bytes=1u<<width;
    if((offset&(bytes-1)) || offset>64u-bytes) return 0;
    *value=0;
    for(i=0;i<bytes;i++)*value|=(unsigned long long)((const unsigned char *)r)[offset+i]<<(i*8);
    return 1;
}
static inline unsigned char AgxHwdataReadReceipt(AGX_FW_IO_READ64 read,void *ctx,
    unsigned long long epoch,unsigned long long root,AGX_HWDATA_RECEIPT *out) {
    AGX_HWDATA_RECEIPT a={0},b={0};
    unsigned i;
    if(!out) return 0;
    *out=(AGX_HWDATA_RECEIPT){0};
    if(!read) return 0;
    for(i=0;i<64;i+=8)AgxFwIoPut((unsigned char *)&a+i,read(ctx,AGX_HWDATA_RECEIPT_OFFSET+i),8);
    for(i=0;i<64;i+=8)AgxFwIoPut((unsigned char *)&b+i,read(ctx,AGX_HWDATA_RECEIPT_OFFSET+i),8);
    for(i=0;i<64;i++)if(((unsigned char *)&a)[i]!=((unsigned char *)&b)[i])return 0;
    if(!AgxHwdataReceiptValid(&a,epoch,root)) return 0;
    *out=a;
    return 1;
}
/* EXP1102: J313 GPU DVFS floor. EXP1101 receipts: every DWM job ran at
 * HwDataA actual_pstate 1 (396 MHz of 1278): the profile's base and minimum
 * performance states are both 1 (J313 ADT gpu-perf-base-pstate = 1; m1n1's
 * default without that property is 3), and DWM's short bursts never move the
 * firmware's utilization filter. The floor raises base_pstate_scaled (0x44,
 * _2 0x7d8, _3 0x7e0, _4 0x81c) and min_pstate_scaled (0x54, u32) /
 * min_pstate_scaled_4 (0x860, float) together (min <= base), offsets from m1n1
 * initdata.py AGXHWDataA for G13 V13_5. max (600) and the power-off path are
 * unchanged. */
#define AGX_HWDATA_J313_DVFS_FLOOR_SCALED 300u
static inline unsigned char AgxHwdataApplyJ313DvfsFloor(void *a, unsigned long long a_bytes,
    unsigned int scaled) {
    static const unsigned int base_offsets[4]={0x44u,0x7d8u,0x7e0u,0x81cu};
    /* IEEE-754 single bits of 100.0 .. 600.0: no floating point in the KMD. */
    static const unsigned int float_bits[6]={0x42c80000u,0x43480000u,0x43960000u,
        0x43c80000u,0x43fa0000u,0x44160000u};
    volatile unsigned int *words;
    unsigned int i;
    if(!a || a_bytes<AGX_HWDATA_A_BYTES || (((unsigned long long)a)&3ULL) ||
       scaled<100u || scaled>600u || scaled%100u) return 0;
    words=(volatile unsigned int *)a;
    for(i=0;i<4u;i++) words[base_offsets[i]/4u]=scaled;
    words[0x54u/4u]=scaled;
    words[0x860u/4u]=float_bits[scaled/100u-1u];
    return 1;
}
static inline unsigned char AgxHwdataMaterialize(const AGX_HWDATA_RECEIPT *r,
    const AGX_FW_IO_MANIFEST *io,unsigned long long epoch,unsigned long long root,
    void *a,unsigned long long a_bytes,void *b,unsigned long long b_bytes) {
    unsigned int i;
    unsigned char *a_data=a,*b_data=b;
    unsigned long long ap,bp;
    ap=(unsigned long long)a_data;bp=(unsigned long long)b_data;
    if(a_bytes<AGX_HWDATA_A_BYTES || b_bytes<AGX_HWDATA_B_BYTES ||
       ap>~0ULL-AGX_HWDATA_A_BYTES || bp>~0ULL-AGX_HWDATA_B_BYTES ||
       (ap<bp+AGX_HWDATA_B_BYTES && bp<ap+AGX_HWDATA_A_BYTES) ||
       AGX_HWDATA_A_BYTES!=0x421c || AGX_HWDATA_B_BYTES!=0x1884 ||
       !AgxHwdataReceiptValid(r,epoch,root) ||
       !AgxFwIoManifestValid(io,AGX_FW_IO_BYTES,epoch,root)) return 0;
    /* Keep the pointer guard adjacent to stores, after integer overlap analysis. */
    if(a_data==0 || b_data==0) return 0;
    for(i=0;i<AGX_HWDATA_A_BYTES;i++)a_data[i]=AgxHwdataAProfile[i];
    for(i=0;i<AGX_HWDATA_B_BYTES;i++)b_data[i]=AgxHwdataBProfile[i];
    AgxFwIoPut(b_data+0x28,AGX_HWDATA_TIMESTAMP_BASE,8);
    return AgxFwIoEncodeHwdataB(io,AGX_FW_IO_BYTES,epoch,root,b_data,b_bytes);
}
#endif
