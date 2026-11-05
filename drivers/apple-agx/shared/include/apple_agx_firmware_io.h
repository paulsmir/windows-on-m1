#ifndef APPLE_AGX_FIRMWARE_IO_H
#define APPLE_AGX_FIRMWARE_IO_H

/* J313/T8103 hardware facts cross-checked against native m1n1 build_iomappings
 * (MIT) and Asahi t8103 hardware configuration (dual MIT/GPL). Independent
 * serializer; this is not a Windows physical-memory access API. */
#define AGX_FW_IO_MAGIC 0x4f495247u
#define AGX_FW_IO_VERSION 1u
#define AGX_FW_IO_OFFSET 0x800u
#define AGX_FW_IO_BYTES 864u
#define AGX_FW_IO_SLOTS 25u
#define AGX_FW_IO_LEAVES 77u
#define AGX_FW_IO_VA 0xffffffa068000000ULL
#define AGX_FW_IO_END 0xffffffa070000000ULL
#define AGX_FW_IO_HWDATA_OFFSET 0x640u

typedef struct _AGX_FW_IO_DESCRIPTOR {
    unsigned long long Phys,Virt;
    unsigned int Size,RangeSize;
    unsigned long long ReadWrite;
} AGX_FW_IO_DESCRIPTOR;
typedef struct _AGX_FW_IO_MANIFEST {
    unsigned int Magic,Version,Bytes,Chip;
    unsigned long long Epoch,Root;
    unsigned int Ready,Count;
    unsigned long long Reserved[3];
    AGX_FW_IO_DESCRIPTOR Records[AGX_FW_IO_SLOTS];
} AGX_FW_IO_MANIFEST;

static inline unsigned char AgxFwIoProfile(unsigned int slot,
                                          AGX_FW_IO_DESCRIPTOR *out) {
    static const struct { unsigned int Slot,Size,Rw; unsigned long long Phys; } entries[] = {
        {0,0x1c000,1,0x204d00000ULL},{1,0x4000,0,0x20e100000ULL},
        {2,0x4000,1,0x23b104000ULL},{3,0x20000,1,0x204000000ULL},
        {7,0x1000,0,0x23b2e8000ULL},{8,0x1000,1,0x23bc00000ULL},
        {9,0x5000,1,0x204d80000ULL},{10,0x1000,1,0x204d61000ULL},
        {11,0xd6400,1,0x200000000ULL},{13,0x1000,1,0x23b738000ULL}
    };
    unsigned int i;
    unsigned long long va=AGX_FW_IO_VA;
    if(!out || slot>=AGX_FW_IO_SLOTS) return 0;
    *out=(AGX_FW_IO_DESCRIPTOR){0};
    for(i=0;i<sizeof(entries)/sizeof(entries[0]);++i) {
        unsigned long long offset=entries[i].Phys&0x3fffULL;
        if(entries[i].Slot==slot) {
            out->Phys=entries[i].Phys;out->Virt=va+offset;
            out->Size=out->RangeSize=entries[i].Size;out->ReadWrite=entries[i].Rw;
            return 1;
        }
        va+=((entries[i].Size+offset+0x3fffULL)&~0x3fffULL)+0x4000ULL;
    }
    return 1;
}

static inline unsigned char AgxFwIoManifestValid(const AGX_FW_IO_MANIFEST *m,
    unsigned int bytes,unsigned long long epoch,unsigned long long root) {
    unsigned int i;
    if(!m || bytes!=AGX_FW_IO_BYTES || sizeof(*m)!=AGX_FW_IO_BYTES ||
       m->Magic!=AGX_FW_IO_MAGIC || m->Version!=AGX_FW_IO_VERSION ||
       m->Bytes!=AGX_FW_IO_BYTES || m->Chip!=0x8103 || m->Ready!=1 ||
       m->Count!=AGX_FW_IO_SLOTS || !epoch || m->Epoch!=epoch ||
       !root || root>=1ULL<<40 || (root&0x3fffULL) || m->Root!=root ||
       m->Reserved[0] || m->Reserved[1] || m->Reserved[2]) return 0;
    for(i=0;i<AGX_FW_IO_SLOTS;i++) {
        AGX_FW_IO_DESCRIPTOR e;
        const AGX_FW_IO_DESCRIPTOR *a=&m->Records[i];
        if(!AgxFwIoProfile(i,&e) || a->Phys!=e.Phys || a->Virt!=e.Virt ||
           a->Size!=e.Size || a->RangeSize!=e.RangeSize || a->ReadWrite!=e.ReadWrite)
            return 0;
    }
    return 1;
}

static inline void AgxFwIoPut(unsigned char *p,unsigned long long value,unsigned n) {
    unsigned i;for(i=0;i<n;i++)p[i]=(unsigned char)(value>>(i*8));
}
/* Read-only bounded owner window. A zero inactive snapshot is readable, but
 * cannot pass the consumer's manifest validation. */
static inline unsigned char AgxFwIoReadWord(const AGX_FW_IO_MANIFEST *m,
    unsigned long long offset,unsigned long long *value,unsigned char write,
    unsigned int width) {
    unsigned int i,bytes;
    const unsigned char *p=(const unsigned char *)m;
    if(!m || !value || write || width>3) return 0;
    bytes=1u<<width;
    if((offset&(bytes-1u)) || offset>AGX_FW_IO_BYTES-bytes) return 0;
    *value=0;
    for(i=0;i<bytes;i++)*value|=(unsigned long long)p[offset+i]<<(i*8);
    return 1;
}
typedef unsigned long long (*AGX_FW_IO_READ64)(void *,unsigned int);
static inline unsigned char AgxFwIoReadManifest(AGX_FW_IO_READ64 read,void *ctx,
    unsigned long long epoch,unsigned long long root,AGX_FW_IO_MANIFEST *out) {
    AGX_FW_IO_MANIFEST candidate={0};
    unsigned char header[64];
    unsigned int i;
    if(!out) return 0;
    *out=(AGX_FW_IO_MANIFEST){0};
    if(!read) return 0;
    for(i=0;i<AGX_FW_IO_BYTES;i+=8)
        AgxFwIoPut((unsigned char *)&candidate+i,read(ctx,AGX_FW_IO_OFFSET+i),8);
    for(i=0;i<64;i+=8)AgxFwIoPut(header+i,read(ctx,AGX_FW_IO_OFFSET+i),8);
    for(i=0;i<64;i++)if(header[i]!=((unsigned char *)&candidate)[i])return 0;
    if(!AgxFwIoManifestValid(&candidate,sizeof(candidate),epoch,root)) return 0;
    *out=candidate;
    return 1;
}
static inline unsigned char AgxFwIoEncodeHwdataB(const AGX_FW_IO_MANIFEST *m,
    unsigned int bytes,unsigned long long epoch,unsigned long long root,
    void *destination,unsigned long long capacity) {
    unsigned int i;
    unsigned char *p=destination;
    if(!p || capacity<AGX_FW_IO_HWDATA_OFFSET+AGX_FW_IO_SLOTS*32u ||
       !AgxFwIoManifestValid(m,bytes,epoch,root)) return 0;
    /* Complete validation precedes the first write. */
    for(i=0;i<AGX_FW_IO_SLOTS;i++) {
        const AGX_FW_IO_DESCRIPTOR *d=&m->Records[i];
        unsigned char *r=p+AGX_FW_IO_HWDATA_OFFSET+i*32u;
        AgxFwIoPut(r,d->Phys,8);AgxFwIoPut(r+8,d->Virt,8);
        AgxFwIoPut(r+16,d->Size,4);AgxFwIoPut(r+20,d->RangeSize,4);
        AgxFwIoPut(r+24,d->ReadWrite,8);
    }
    return 1;
}
#endif
