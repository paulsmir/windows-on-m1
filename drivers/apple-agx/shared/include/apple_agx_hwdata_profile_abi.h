#ifndef APPLE_AGX_HWDATA_PROFILE_ABI_H
#define APPLE_AGX_HWDATA_PROFILE_ABI_H
#define AGX_HWDATA_RECEIPT_MAGIC 0x50485741u
#define AGX_HWDATA_RECEIPT_OFFSET 0xc00u
#define AGX_HWDATA_RECEIPT_BYTES 64u
#define AGX_HWDATA_TIMESTAMP_BASE 0xffffffa071000000ULL
typedef struct _AGX_HWDATA_RECEIPT {
    unsigned int Magic,Version,Bytes,Chip;
    unsigned long long Epoch,Root;
    unsigned char ProfileId[32];
} AGX_HWDATA_RECEIPT;
#endif
