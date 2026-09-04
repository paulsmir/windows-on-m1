#include "apple_agx_uat.h"

#define APPLE_AGX_UAT_BIT(_bit) (1ULL << (_bit))
#define APPLE_AGX_UAT_PAGE_MASK (J313_AGX_G2_PAGE_SIZE - 1ULL)
#define APPLE_AGX_UAT_PHYSICAL_LIMIT \
  (1ULL << J313_AGX_G2_UAT_OUTPUT_ADDRESS_BITS)
#define APPLE_AGX_UAT_LOW_LIMIT \
  (1ULL << J313_AGX_G2_UAT_INPUT_ADDRESS_BITS)
#define APPLE_AGX_UAT_HIGH_BASE \
  (~(APPLE_AGX_UAT_LOW_LIMIT - 1ULL))

#define APPLE_AGX_UAT_PTE_OWNER APPLE_AGX_UAT_BIT(55)
#define APPLE_AGX_UAT_PTE_UXN APPLE_AGX_UAT_BIT(54)
#define APPLE_AGX_UAT_PTE_PXN APPLE_AGX_UAT_BIT(53)
#define APPLE_AGX_UAT_PTE_NON_GLOBAL APPLE_AGX_UAT_BIT(11)
#define APPLE_AGX_UAT_PTE_ACCESS_FLAG APPLE_AGX_UAT_BIT(10)
#define APPLE_AGX_UAT_PTE_AP(_value) ((unsigned long long)(_value) << 6)
#define APPLE_AGX_UAT_PTE_ATTR(_value) ((unsigned long long)(_value) << 2)
#define APPLE_AGX_UAT_PTE_LEAF_TABLE 3ULL
#define APPLE_AGX_UAT_TTBR_VALID 1ULL
#define APPLE_AGX_UAT_TTBR_ASID(_context) \
  ((unsigned long long)(_context) << 48)

#define APPLE_AGX_UAT_ATTR_CACHED 0u
#define APPLE_AGX_UAT_ATTR_DEVICE 1u
#define APPLE_AGX_UAT_ATTR_UNCACHED 2u
#define APPLE_AGX_UAT_AP_FIRMWARE_GPU 0u
#define APPLE_AGX_UAT_AP_FIRMWARE 1u
#define APPLE_AGX_UAT_AP_GPU 2u

static APPLE_AGX_UAT_RESULT AppleAgxUatProtectionBits(
    unsigned int Context, APPLE_AGX_UAT_PROTECTION Protection,
    unsigned long long *Bits) {
  unsigned int attribute;
  unsigned int ap;
  unsigned char uxn;
  unsigned char pxn;

  if (Context >= J313_AGX_G2_UAT_CONTEXT_COUNT) {
    return AppleAgxUatResultUnsupportedContext;
  }
  if (Bits == 0) {
    return AppleAgxUatResultInvalidArgument;
  }

  switch (Protection) {
    case AppleAgxUatFirmwareDeviceReadWrite:
      attribute = APPLE_AGX_UAT_ATTR_DEVICE;
      ap = APPLE_AGX_UAT_AP_FIRMWARE;
      uxn = 1u;
      pxn = 0u;
      break;
    case AppleAgxUatFirmwareSharedReadWrite:
      attribute = APPLE_AGX_UAT_ATTR_UNCACHED;
      ap = APPLE_AGX_UAT_AP_FIRMWARE;
      uxn = 1u;
      pxn = 0u;
      break;
    case AppleAgxUatFirmwarePrivateReadWrite:
      attribute = APPLE_AGX_UAT_ATTR_CACHED;
      ap = APPLE_AGX_UAT_AP_FIRMWARE;
      uxn = 1u;
      pxn = 0u;
      break;
    case AppleAgxUatFirmwareGpuPrivateReadWrite:
      attribute = APPLE_AGX_UAT_ATTR_CACHED;
      ap = APPLE_AGX_UAT_AP_FIRMWARE_GPU;
      uxn = 1u;
      pxn = 1u;
      break;
    case AppleAgxUatFirmwareReadWriteGpuReadOnly:
      attribute = APPLE_AGX_UAT_ATTR_CACHED;
      ap = APPLE_AGX_UAT_AP_FIRMWARE;
      uxn = 1u;
      pxn = 1u;
      break;
    case AppleAgxUatGpuSharedReadOnly:
      attribute = APPLE_AGX_UAT_ATTR_UNCACHED;
      ap = APPLE_AGX_UAT_AP_GPU;
      uxn = 0u;
      pxn = 0u;
      break;
    case AppleAgxUatGpuSharedWriteOnly:
      attribute = APPLE_AGX_UAT_ATTR_UNCACHED;
      ap = APPLE_AGX_UAT_AP_GPU;
      uxn = 0u;
      pxn = 1u;
      break;
    case AppleAgxUatGpuSharedReadWrite:
      attribute = APPLE_AGX_UAT_ATTR_UNCACHED;
      ap = APPLE_AGX_UAT_AP_GPU;
      uxn = 1u;
      pxn = 0u;
      break;
    default:
      return AppleAgxUatResultUnsupportedProtection;
  }

  if ((Context == J313_AGX_G2_UAT_FIRMWARE_CONTEXT &&
       Protection >= AppleAgxUatGpuSharedReadOnly) ||
      (Context != J313_AGX_G2_UAT_FIRMWARE_CONTEXT &&
       Protection <= AppleAgxUatFirmwareReadWriteGpuReadOnly)) {
    return AppleAgxUatResultUnsupportedProtection;
  }

  *Bits = APPLE_AGX_UAT_PTE_OWNER | APPLE_AGX_UAT_PTE_ACCESS_FLAG |
          APPLE_AGX_UAT_PTE_AP(ap) | APPLE_AGX_UAT_PTE_ATTR(attribute) |
          APPLE_AGX_UAT_PTE_LEAF_TABLE;
  if (uxn != 0u) {
    *Bits |= APPLE_AGX_UAT_PTE_UXN;
  }
  if (pxn != 0u) {
    *Bits |= APPLE_AGX_UAT_PTE_PXN;
  }
  if (Context != J313_AGX_G2_UAT_FIRMWARE_CONTEXT) {
    *Bits |= APPLE_AGX_UAT_PTE_NON_GLOBAL;
  }
  return AppleAgxUatResultOk;
}

APPLE_AGX_UAT_RESULT AppleAgxUatEncodeTableDescriptor(
    unsigned long long PhysicalAddress, unsigned long long *Descriptor) {
  if (Descriptor == 0) {
    return AppleAgxUatResultInvalidArgument;
  }
  if ((PhysicalAddress & APPLE_AGX_UAT_PAGE_MASK) != 0ULL) {
    return AppleAgxUatResultMisaligned;
  }
  if (PhysicalAddress >= APPLE_AGX_UAT_PHYSICAL_LIMIT) {
    return AppleAgxUatResultOutOfRange;
  }
  *Descriptor = PhysicalAddress | APPLE_AGX_UAT_PTE_LEAF_TABLE;
  return AppleAgxUatResultOk;
}

APPLE_AGX_UAT_RESULT AppleAgxUatEncodeTtbrPair(
    unsigned int Context, const APPLE_AGX_UAT_ROOTS *Roots,
    APPLE_AGX_UAT_TTBR_PAIR *Pair) {
  APPLE_AGX_UAT_RESULT result;
  unsigned long long ignored_descriptor;
  unsigned long long asid;

  if (Roots == 0 || Pair == 0) {
    return AppleAgxUatResultInvalidArgument;
  }
  if (Context >= J313_AGX_G2_UAT_CONTEXT_COUNT) {
    return AppleAgxUatResultUnsupportedContext;
  }
  result = AppleAgxUatEncodeTableDescriptor(
      Roots->Ttbr0PhysicalAddress, &ignored_descriptor);
  if (result != AppleAgxUatResultOk) {
    return result;
  }
  result = AppleAgxUatEncodeTableDescriptor(
      Roots->Ttbr1PhysicalAddress, &ignored_descriptor);
  if (result != AppleAgxUatResultOk) {
    return result;
  }

  asid = APPLE_AGX_UAT_TTBR_ASID(Context);
  Pair->Ttbr0 = asid | Roots->Ttbr0PhysicalAddress | APPLE_AGX_UAT_TTBR_VALID;
  Pair->Ttbr1 = asid | Roots->Ttbr1PhysicalAddress | APPLE_AGX_UAT_TTBR_VALID;
  return AppleAgxUatResultOk;
}

void AppleAgxUatClearTtbrPair(APPLE_AGX_UAT_TTBR_PAIR *Pair) {
  if (Pair != 0) {
    Pair->Ttbr0 = 0ULL;
    Pair->Ttbr1 = 0ULL;
  }
}

APPLE_AGX_UAT_RESULT AppleAgxUatEncodePageDescriptor(
    unsigned int Context, unsigned long long PhysicalAddress,
    APPLE_AGX_UAT_PROTECTION Protection, unsigned long long *Descriptor) {
  APPLE_AGX_UAT_RESULT result;
  unsigned long long bits;

  if (Descriptor == 0) {
    return AppleAgxUatResultInvalidArgument;
  }
  result = AppleAgxUatProtectionBits(Context, Protection, &bits);
  if (result != AppleAgxUatResultOk) {
    return result;
  }
  if ((PhysicalAddress & APPLE_AGX_UAT_PAGE_MASK) != 0ULL) {
    return AppleAgxUatResultMisaligned;
  }
  if (PhysicalAddress >= APPLE_AGX_UAT_PHYSICAL_LIMIT) {
    return AppleAgxUatResultOutOfRange;
  }
  *Descriptor = PhysicalAddress | bits;
  return AppleAgxUatResultOk;
}

APPLE_AGX_UAT_RESULT AppleAgxUatValidateRange(
    unsigned int Context, unsigned long long VirtualAddress,
    unsigned long long PhysicalAddress, unsigned long long Length,
    APPLE_AGX_UAT_PROTECTION Protection, APPLE_AGX_UAT_HALF *Half) {
  APPLE_AGX_UAT_RESULT result;
  unsigned long long ignored_bits;
  unsigned long long last_virtual;

  if (Half == 0 || Length == 0ULL) {
    return AppleAgxUatResultInvalidArgument;
  }
  result = AppleAgxUatProtectionBits(Context, Protection, &ignored_bits);
  if (result != AppleAgxUatResultOk) {
    return result;
  }
  if (((VirtualAddress | PhysicalAddress | Length) &
       APPLE_AGX_UAT_PAGE_MASK) != 0ULL) {
    return AppleAgxUatResultMisaligned;
  }
  if (Length - 1ULL > ~VirtualAddress) {
    return AppleAgxUatResultOverflow;
  }
  last_virtual = VirtualAddress + Length - 1ULL;
  if (PhysicalAddress >= APPLE_AGX_UAT_PHYSICAL_LIMIT ||
      Length > APPLE_AGX_UAT_PHYSICAL_LIMIT - PhysicalAddress) {
    return AppleAgxUatResultOutOfRange;
  }

  if (VirtualAddress < APPLE_AGX_UAT_LOW_LIMIT &&
      last_virtual < APPLE_AGX_UAT_LOW_LIMIT) {
    *Half = AppleAgxUatTtbr0;
    return AppleAgxUatResultOk;
  }
  if (VirtualAddress >= APPLE_AGX_UAT_HIGH_BASE) {
    *Half = AppleAgxUatTtbr1;
    return AppleAgxUatResultOk;
  }
  return AppleAgxUatResultOutOfRange;
}
