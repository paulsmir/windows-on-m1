#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "apple_agx_hwdata_profile.h"

static unsigned mutation=~0u,mode;
static AGX_HWDATA_RECEIPT receipt_wire;
static unsigned receipt_reads,receipt_change;
static unsigned long long read_receipt(void *ctx,unsigned offset) {
    unsigned long long value=0;(void)ctx;
    if(receipt_change && receipt_reads++==8)receipt_wire.Epoch++;
    assert(AgxHwdataReceiptWord(&receipt_wire,offset-AGX_HWDATA_RECEIPT_OFFSET,&value,0,3));
    return value;
}
static const unsigned char *read_property(void *ctx,const char *name,unsigned *length) {
    unsigned i;
    static unsigned char changed[512];
    (void)ctx;
    for(i=0;i<AGX_HWDATA_INPUT_COUNT;i++)if(!strcmp(name,AgxHwdataInputs[i].Name)) {
        const AGX_HWDATA_INPUT *e=&AgxHwdataInputs[i];
        if(i==mutation) {
            if(e->Length==~0u){*length=1;changed[0]=0;return changed;}
            if(mode==1){*length=0;return 0;}
            assert(e->Length<sizeof(changed));
            memcpy(changed,e->Bytes,e->Length);
            *length=e->Length;
            if(mode==2 || !*length)++*length;else changed[0]^=1;
            return changed;
        }
        *length=e->Length==~0u?0:e->Length;
        return e->Length==~0u?0:e->Bytes;
    }
    assert(0);return 0;
}
int main(void) {
    unsigned i,j;
    AGX_HWDATA_RECEIPT receipt={0},bad;
    AGX_FW_IO_MANIFEST io={0};
    unsigned char a[0x8000],b[0x4000],saved_a[sizeof(a)],saved_b[sizeof(b)];
    assert(AgxHwdataInputsMatch(read_property,0));
    for(i=0;i<AGX_HWDATA_INPUT_COUNT;i++)for(j=0;j<3;j++) {
        mutation=i;mode=j;assert(!AgxHwdataInputsMatch(read_property,0));
    }
    mutation=~0u;
    receipt.Magic=AGX_HWDATA_RECEIPT_MAGIC;receipt.Version=1;
    receipt.Bytes=sizeof(receipt);receipt.Chip=0x8103;receipt.Epoch=3;
    receipt.Root=0x9fff78000ULL;memcpy(receipt.ProfileId,AgxHwdataProfileId,32);
    receipt_wire=receipt;
    assert(AgxHwdataReadReceipt(read_receipt,0,3,receipt.Root,&bad));
    receipt_change=1;receipt_reads=0;
    assert(!AgxHwdataReadReceipt(read_receipt,0,3,receipt.Root,&bad));
    assert(bad.Epoch==0);
    {
        unsigned long long value=0x1234;
        assert(!AgxHwdataReceiptWord(&receipt,0,&value,1,3));
        assert(!AgxHwdataReceiptWord(&receipt,1,&value,0,3));
        assert(!AgxHwdataReceiptWord(&receipt,64,&value,0,3));
        assert(value==0x1234);
    }
    io.Magic=AGX_FW_IO_MAGIC;io.Version=1;io.Bytes=sizeof(io);io.Chip=0x8103;
    io.Epoch=3;io.Root=receipt.Root;io.Ready=1;io.Count=25;
    for(i=0;i<25;i++)assert(AgxFwIoProfile(i,&io.Records[i]));
    memset(a,0x5a,sizeof(a));memset(b,0x5a,sizeof(b));
    assert(AgxHwdataMaterialize(&receipt,&io,3,receipt.Root,a,sizeof(a),b,sizeof(b)));
    assert(!memcmp(a,AgxHwdataAProfile,AGX_HWDATA_A_BYTES));
    for(i=AGX_HWDATA_A_BYTES;i<sizeof(a);i++)assert(a[i]==0x5a);
    for(i=AGX_HWDATA_B_BYTES;i<sizeof(b);i++)assert(b[i]==0x5a);
    assert(b[0x28+3]==0x71 && b[0x28+4]==0xa0);
    memcpy(saved_a,a,sizeof(a));memcpy(saved_b,b,sizeof(b));
    bad=receipt;bad.ProfileId[0]^=1;
    assert(!AgxHwdataMaterialize(&bad,&io,3,receipt.Root,a,sizeof(a),b,sizeof(b)));
    bad=receipt;bad.Epoch++;
    assert(!AgxHwdataMaterialize(&bad,&io,3,receipt.Root,a,sizeof(a),b,sizeof(b)));
    assert(!AgxHwdataMaterialize(&receipt,&io,3,receipt.Root,a,AGX_HWDATA_A_BYTES-1,b,sizeof(b)));
    assert(!AgxHwdataMaterialize(&receipt,&io,3,receipt.Root,a,sizeof(a),a,sizeof(a)));
    io.Records[0].Phys++;
    assert(!AgxHwdataMaterialize(&receipt,&io,3,receipt.Root,a,sizeof(a),b,sizeof(b)));
    assert(!memcmp(a,saved_a,sizeof(a)) && !memcmp(b,saved_b,sizeof(b)));
    return 0;
}
