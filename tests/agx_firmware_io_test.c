#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "apple_agx_firmware_io.h"

static AGX_FW_IO_MANIFEST wire;
static unsigned reads,change_epoch;
static unsigned long long read_word(void *ctx,unsigned int offset) {
    unsigned long long value=0;
    (void)ctx;
    if(change_epoch && reads++==108) wire.Epoch++;
    assert(AgxFwIoReadWord(&wire,offset-AGX_FW_IO_OFFSET,&value,0,3));
    return value;
}

int main(int argc,char **argv) {
    AGX_FW_IO_MANIFEST m={0},bad;
    unsigned char bytes[0x1000],saved[sizeof(bytes)];
    unsigned i,nonzero=0,leaves=0;
    (void)argv;
    m.Magic=AGX_FW_IO_MAGIC;m.Version=1;m.Bytes=sizeof(m);m.Chip=0x8103;
    m.Epoch=4;m.Root=0x9fff78000ULL;m.Ready=1;m.Count=25;
    assert(sizeof(m)==864 && sizeof(m.Records[0])==32);
    for(i=0;i<25;i++) {
        assert(AgxFwIoProfile(i,&m.Records[i]));
        if(m.Records[i].Size) {
            ++nonzero;
            leaves+=(m.Records[i].Size+(m.Records[i].Phys&0x3fff)+0x3fff)/0x4000;
        }
    }
    assert(nonzero==10 && leaves==77);
    assert(m.Records[10].Phys==0x204d61000ULL && (m.Records[10].Virt&0x3fff)==0x1000);
    assert(AgxFwIoManifestValid(&m,sizeof(m),4,0x9fff78000ULL));
    wire=m;
    assert(AgxFwIoReadManifest(read_word,0,4,m.Root,&bad));
    assert(!memcmp(&bad,&m,sizeof(m)));
    change_epoch=1;reads=0;
    assert(!AgxFwIoReadManifest(read_word,0,4,m.Root,&bad));
    assert(bad.Ready==0);
    {
        unsigned long long value=0x1234;
        assert(!AgxFwIoReadWord(&m,0,&value,1,3));
        assert(!AgxFwIoReadWord(&m,1,&value,0,3));
        assert(!AgxFwIoReadWord(&m,864,&value,0,3));
        assert(!AgxFwIoReadWord(&m,0,&value,0,4));
        assert(value==0x1234);
    }
    memset(bytes,0x5a,sizeof(bytes));memcpy(saved,bytes,sizeof(bytes));
    assert(AgxFwIoEncodeHwdataB(&m,sizeof(m),4,m.Root,bytes,sizeof(bytes)));
    assert(!memcmp(bytes,saved,0x640));
    assert(!memcmp(bytes+0x960,saved+0x960,sizeof(bytes)-0x960));
    assert(bytes[0x640+4]==2 && bytes[0x640+0x18]==1);
    memcpy(saved,bytes,sizeof(bytes));
#define REJECT(change) do { bad=m;change;assert(!AgxFwIoManifestValid(&bad,sizeof(bad),4,m.Root));assert(!AgxFwIoEncodeHwdataB(&bad,sizeof(bad),4,m.Root,bytes,sizeof(bytes)));assert(!memcmp(bytes,saved,sizeof(bytes)));}while(0)
    REJECT(bad.Version=2);REJECT(bad.Bytes--);REJECT(bad.Epoch++);
    REJECT(bad.Root+=0x4000);REJECT(bad.Root++);REJECT(bad.Root=1ULL<<40);
    REJECT(bad.Ready=0);REJECT(bad.Count=24);REJECT(bad.Chip=0x8112);
    REJECT(bad.Reserved[0]=1);REJECT(bad.Records[4].Size=1);
    REJECT(bad.Records[0].Phys+=0x4000);REJECT(bad.Records[0].Virt+=1);
    REJECT(bad.Records[1].ReadWrite=1);REJECT(bad.Records[10].RangeSize++);
    assert(!AgxFwIoManifestValid(&m,sizeof(m)-1,4,m.Root));
    assert(!AgxFwIoEncodeHwdataB(&m,sizeof(m),4,m.Root,bytes,0x95f));
    if(argc>1) assert(fwrite(bytes+0x640,1,800,stdout)==800);
    return 0;
}
