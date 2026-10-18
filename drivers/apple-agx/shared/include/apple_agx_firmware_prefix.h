#ifndef APPLE_AGX_FIRMWARE_PREFIX_H
#define APPLE_AGX_FIRMWARE_PREFIX_H

/* Shared wire ABI, not a firmware table implementation. All fields little endian. */
#define AGX_FW_PREFIX_OFFSET 0x280u
#define AGX_FW_PREFIX_SIZE 64u
#define AGX_FW_PREFIX_MAGIC 0x50465841u
typedef struct _AGX_FW_PREFIX {
  unsigned int Magic, Version, Size, PrefixBytes;
  unsigned long long Base, Length, Epoch, Ready, Entries[2];
} AGX_FW_PREFIX;

static inline unsigned char AgxFwPrefixGeometry(
    unsigned long long base, unsigned long long length) {
  return base != 0 && (base & 0x3fffULL) == 0 &&
         length >= 0xc000ULL && length <= 0x40000ULL &&
         (length & 0x3fffULL) == 0 && base < (1ULL << 40) &&
         length <= (1ULL << 40) - base;
}

static inline unsigned char AgxFwPrefixValid(
    const AGX_FW_PREFIX *p, unsigned int bytes, unsigned long long epoch) {
  unsigned int i;
  if (!p || bytes != AGX_FW_PREFIX_SIZE || sizeof(*p) != AGX_FW_PREFIX_SIZE ||
      p->Magic != AGX_FW_PREFIX_MAGIC || p->Version != 1 ||
      p->Size != AGX_FW_PREFIX_SIZE || p->PrefixBytes != 16 ||
      p->Ready != 1 || !epoch || p->Epoch != epoch ||
      !AgxFwPrefixGeometry(p->Base, p->Length))
    return 0;
  for (i = 0; i < 2; ++i) {
    unsigned long long pa = p->Entries[i] & 0xffffffc000ULL;
    /* Firmware-owned table descriptor: TYPE/VALID plus optional AF, no OS bit.
     * Other permission/reserved encodings are outside this ABI and fail closed. */
    if ((p->Entries[i] & 3ULL) != 3ULL ||
        (p->Entries[i] & ~(0xffffffc000ULL | 0x403ULL)) != 0 ||
        pa < p->Base + 0x4000ULL || pa >= p->Base + p->Length ||
        0x4000ULL > p->Base + p->Length - pa)
      return 0;
  }
  return (p->Entries[0] & 0xffffffc000ULL) !=
         (p->Entries[1] & 0xffffffc000ULL);
}
#endif
