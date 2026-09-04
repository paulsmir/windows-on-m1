#include "apple_agx_uat_table.h"

#define APPLE_AGX_UAT_TABLE_PAGE_MASK (J313_AGX_G2_PAGE_SIZE - 1ULL)
#define APPLE_AGX_UAT_TABLE_ADDRESS_MASK 0x000000ffffffc000ULL
#define APPLE_AGX_UAT_TABLE_DESCRIPTOR_MASK 3ULL
#define APPLE_AGX_UAT_TABLE_ENTRY_COUNT 2048u

static APPLE_AGX_UAT_RESULT AppleAgxUatCheckOwnerArguments(
    const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    const APPLE_AGX_UAT_INVENTORY *Inventory) {
  if (Allocator == 0 || Inventory == 0 || Allocator->AllocatePage == 0 ||
      Allocator->ReleasePage == 0 || Inventory->Pages == 0 ||
      Inventory->Mappings == 0 || Inventory->PageCount > Inventory->PageCapacity ||
      Inventory->MappingCount > Inventory->MappingCapacity) {
    return AppleAgxUatResultInvalidArgument;
  }
  return AppleAgxUatResultOk;
}

static APPLE_AGX_UAT_PAGE *AppleAgxUatFindPage(
    APPLE_AGX_UAT_INVENTORY *Inventory, unsigned long long PhysicalAddress,
    unsigned int Level) {
  unsigned int index;
  for (index = 0; index < Inventory->PageCount; ++index) {
    if (Inventory->Pages[index].PhysicalAddress == PhysicalAddress &&
        Inventory->Pages[index].Level == Level) {
      return &Inventory->Pages[index];
    }
  }
  return 0;
}

static unsigned char AppleAgxUatPageIsZero(const APPLE_AGX_UAT_PAGE *Page) {
  unsigned int index;
  for (index = 0; index < APPLE_AGX_UAT_TABLE_ENTRY_COUNT; ++index) {
    if (Page->Entries[index] != 0ULL) {
      return 0u;
    }
  }
  return 1u;
}

static APPLE_AGX_UAT_RESULT AppleAgxUatAllocateRecordedPage(
    unsigned int Level, const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory, APPLE_AGX_UAT_PAGE **Page) {
  APPLE_AGX_UAT_PAGE candidate;

  if (Inventory->PageCount >= Inventory->PageCapacity) {
    return AppleAgxUatResultCapacity;
  }
  candidate.PhysicalAddress = 0ULL;
  candidate.Entries = 0;
  candidate.Level = Level;
  if (Allocator->AllocatePage(Allocator->Context, &candidate) == 0u) {
    return AppleAgxUatResultAllocationFailed;
  }
  if (candidate.PhysicalAddress == 0ULL || candidate.Entries == 0 ||
      (candidate.PhysicalAddress & APPLE_AGX_UAT_TABLE_PAGE_MASK) != 0ULL ||
      (((unsigned long long)(void *)candidate.Entries) &
       APPLE_AGX_UAT_TABLE_PAGE_MASK) != 0ULL ||
      candidate.PhysicalAddress >=
          (1ULL << J313_AGX_G2_UAT_OUTPUT_ADDRESS_BITS) ||
      AppleAgxUatPageIsZero(&candidate) == 0u) {
    Allocator->ReleasePage(Allocator->Context, &candidate);
    return AppleAgxUatResultAllocationFailed;
  }
  candidate.Level = Level;
  Inventory->Pages[Inventory->PageCount] = candidate;
  *Page = &Inventory->Pages[Inventory->PageCount++];
  return AppleAgxUatResultOk;
}

static void AppleAgxUatReleaseFrom(
    unsigned int FirstPage, const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory) {
  while (Inventory->PageCount > FirstPage) {
    APPLE_AGX_UAT_PAGE *page = &Inventory->Pages[Inventory->PageCount - 1u];
    Allocator->ReleasePage(Allocator->Context, page);
    page->PhysicalAddress = 0ULL;
    page->Entries = 0;
    page->Level = 0u;
    --Inventory->PageCount;
  }
}

static APPLE_AGX_UAT_RESULT AppleAgxUatGetChild(
    APPLE_AGX_UAT_PAGE *Parent, unsigned int EntryIndex,
    unsigned int ChildLevel, const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory, APPLE_AGX_UAT_PAGE **Child) {
  APPLE_AGX_UAT_RESULT result;
  unsigned long long descriptor;

  descriptor = Parent->Entries[EntryIndex];
  if (descriptor != 0ULL) {
    if ((descriptor & APPLE_AGX_UAT_TABLE_DESCRIPTOR_MASK) !=
        APPLE_AGX_UAT_TABLE_DESCRIPTOR_MASK) {
      return AppleAgxUatResultAlreadyMapped;
    }
    *Child = AppleAgxUatFindPage(
        Inventory, descriptor & APPLE_AGX_UAT_TABLE_ADDRESS_MASK, ChildLevel);
    return *Child == 0 ? AppleAgxUatResultInvalidArgument
                       : AppleAgxUatResultOk;
  }

  result = AppleAgxUatAllocateRecordedPage(ChildLevel, Allocator, Inventory,
                                           Child);
  if (result != AppleAgxUatResultOk) {
    return result;
  }
  result = AppleAgxUatEncodeTableDescriptor((*Child)->PhysicalAddress,
                                             &descriptor);
  if (result != AppleAgxUatResultOk) {
    return result;
  }
  Parent->Entries[EntryIndex] = descriptor;
  return AppleAgxUatResultOk;
}

static APPLE_AGX_UAT_RESULT AppleAgxUatFindLeaf(
    APPLE_AGX_UAT_PAGE *Root, unsigned long long VirtualAddress,
    const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory, unsigned long long **Leaf) {
  APPLE_AGX_UAT_RESULT result;
  APPLE_AGX_UAT_PAGE *level1;
  APPLE_AGX_UAT_PAGE *level2;
  unsigned int level0_index =
      (unsigned int)((VirtualAddress >> J313_AGX_G2_UAT_LEVEL0_SHIFT) & 7ULL);
  unsigned int level1_index = (unsigned int)(
      (VirtualAddress >> J313_AGX_G2_UAT_LEVEL1_SHIFT) & 2047ULL);
  unsigned int level2_index = (unsigned int)(
      (VirtualAddress >> J313_AGX_G2_UAT_LEVEL2_SHIFT) & 2047ULL);

  result = AppleAgxUatGetChild(Root, level0_index, 1u, Allocator, Inventory,
                               &level1);
  if (result != AppleAgxUatResultOk) {
    return result;
  }
  result = AppleAgxUatGetChild(level1, level1_index, 2u, Allocator, Inventory,
                               &level2);
  if (result != AppleAgxUatResultOk) {
    return result;
  }
  *Leaf = &level2->Entries[level2_index];
  return AppleAgxUatResultOk;
}

static void AppleAgxUatClearNewParents(
    unsigned int FirstPage, APPLE_AGX_UAT_INVENTORY *Inventory) {
  unsigned int page_index;
  unsigned int entry_index;
  unsigned int child_index;

  for (page_index = 0; page_index < Inventory->PageCount; ++page_index) {
    APPLE_AGX_UAT_PAGE *page = &Inventory->Pages[page_index];
    unsigned int entry_count = page->Level == 0u ? 8u : 2048u;
    if (page->Level >= 2u) {
      continue;
    }
    for (entry_index = 0; entry_index < entry_count; ++entry_index) {
      unsigned long long target =
          page->Entries[entry_index] & APPLE_AGX_UAT_TABLE_ADDRESS_MASK;
      for (child_index = FirstPage; child_index < Inventory->PageCount;
           ++child_index) {
        if (target != 0ULL &&
            target == Inventory->Pages[child_index].PhysicalAddress) {
          page->Entries[entry_index] = 0ULL;
          break;
        }
      }
    }
  }
}

static APPLE_AGX_UAT_RESULT AppleAgxUatFindExistingLeaf(
    APPLE_AGX_UAT_PAGE *Root, unsigned long long VirtualAddress,
    APPLE_AGX_UAT_INVENTORY *Inventory, unsigned long long **Leaf) {
  APPLE_AGX_UAT_PAGE *level1;
  APPLE_AGX_UAT_PAGE *level2;
  unsigned long long descriptor;
  unsigned int index;

  index = (unsigned int)((VirtualAddress >>
                          J313_AGX_G2_UAT_LEVEL0_SHIFT) & 7ULL);
  descriptor = Root->Entries[index];
  level1 = AppleAgxUatFindPage(
      Inventory, descriptor & APPLE_AGX_UAT_TABLE_ADDRESS_MASK, 1u);
  if (level1 == 0) {
    return AppleAgxUatResultInvalidArgument;
  }
  index = (unsigned int)((VirtualAddress >>
                          J313_AGX_G2_UAT_LEVEL1_SHIFT) & 2047ULL);
  descriptor = level1->Entries[index];
  level2 = AppleAgxUatFindPage(
      Inventory, descriptor & APPLE_AGX_UAT_TABLE_ADDRESS_MASK, 2u);
  if (level2 == 0) {
    return AppleAgxUatResultInvalidArgument;
  }
  index = (unsigned int)((VirtualAddress >>
                          J313_AGX_G2_UAT_LEVEL2_SHIFT) & 2047ULL);
  *Leaf = &level2->Entries[index];
  return AppleAgxUatResultOk;
}

APPLE_AGX_UAT_RESULT AppleAgxUatResolvePage(
    unsigned int Context, const APPLE_AGX_UAT_ROOTS *Roots,
    unsigned long long VirtualAddress, APPLE_AGX_UAT_INVENTORY *Inventory,
    unsigned long long *PhysicalAddress,
    unsigned long long *Descriptor) {
  const unsigned long long inputLimit =
      1ULL << J313_AGX_G2_UAT_INPUT_ADDRESS_BITS;
  const unsigned long long highBase = ~(inputLimit - 1ULL);
  APPLE_AGX_UAT_PAGE *root;
  unsigned long long *leaf = 0;
  unsigned long long value;

  if (PhysicalAddress != 0)
    *PhysicalAddress = 0ULL;
  if (Descriptor != 0)
    *Descriptor = 0ULL;
  if (Roots == 0 || Inventory == 0 || PhysicalAddress == 0 ||
      Descriptor == 0 || Context >= J313_AGX_G2_UAT_CONTEXT_COUNT)
    return AppleAgxUatResultInvalidArgument;
  if ((VirtualAddress & APPLE_AGX_UAT_TABLE_PAGE_MASK) != 0ULL)
    return AppleAgxUatResultMisaligned;
  if (VirtualAddress >= inputLimit && VirtualAddress < highBase)
    return AppleAgxUatResultOutOfRange;
  root = AppleAgxUatFindPage(
      Inventory,
      VirtualAddress < inputLimit ? Roots->Ttbr0PhysicalAddress
                                  : Roots->Ttbr1PhysicalAddress,
      0u);
  if (root == 0 ||
      AppleAgxUatFindExistingLeaf(root, VirtualAddress, Inventory, &leaf) !=
          AppleAgxUatResultOk ||
      leaf == 0)
    return AppleAgxUatResultNotMapped;
  value = *leaf;
  if ((value & APPLE_AGX_UAT_TABLE_DESCRIPTOR_MASK) !=
          APPLE_AGX_UAT_TABLE_DESCRIPTOR_MASK ||
      (value & APPLE_AGX_UAT_TABLE_ADDRESS_MASK) == 0ULL)
    return AppleAgxUatResultNotMapped;
  *PhysicalAddress = value & APPLE_AGX_UAT_TABLE_ADDRESS_MASK;
  *Descriptor = value;
  return AppleAgxUatResultOk;
}

static void AppleAgxUatRollbackMap(
    APPLE_AGX_UAT_PAGE *Root, unsigned long long VirtualAddress,
    unsigned long long MappedLength, unsigned int FirstPage,
    const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory) {
  unsigned long long offset;
  for (offset = 0ULL; offset < MappedLength;
       offset += J313_AGX_G2_PAGE_SIZE) {
    unsigned long long *leaf = 0;
    if (AppleAgxUatFindExistingLeaf(Root, VirtualAddress + offset, Inventory,
                                   &leaf) == AppleAgxUatResultOk) {
      *leaf = 0ULL;
    }
  }
  AppleAgxUatClearNewParents(FirstPage, Inventory);
  AppleAgxUatReleaseFrom(FirstPage, Allocator, Inventory);
}

APPLE_AGX_UAT_RESULT AppleAgxUatCreateAddressSpace(
    unsigned int Context, const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory, APPLE_AGX_UAT_ROOTS *Roots) {
  APPLE_AGX_UAT_RESULT result;
  APPLE_AGX_UAT_PAGE *root;
  unsigned int first_page;

  if (Roots == 0 || Inventory == 0) {
    return AppleAgxUatResultInvalidArgument;
  }
  Roots->Ttbr0PhysicalAddress = 0ULL;
  Roots->Ttbr1PhysicalAddress = 0ULL;
  if (Context >= J313_AGX_G2_UAT_CONTEXT_COUNT) {
    return AppleAgxUatResultUnsupportedContext;
  }
  result = AppleAgxUatCheckOwnerArguments(Allocator, Inventory);
  if (result != AppleAgxUatResultOk) {
    return result;
  }
  first_page = Inventory->PageCount;
  result = AppleAgxUatAllocateRecordedPage(0u, Allocator, Inventory, &root);
  if (result != AppleAgxUatResultOk) {
    return result;
  }
  Roots->Ttbr0PhysicalAddress = root->PhysicalAddress;
  result = AppleAgxUatAllocateRecordedPage(0u, Allocator, Inventory, &root);
  if (result != AppleAgxUatResultOk) {
    AppleAgxUatReleaseFrom(first_page, Allocator, Inventory);
    Roots->Ttbr0PhysicalAddress = 0ULL;
    return result;
  }
  Roots->Ttbr1PhysicalAddress = root->PhysicalAddress;
  return AppleAgxUatResultOk;
}

APPLE_AGX_UAT_RESULT AppleAgxUatMap(
    unsigned int Context, const APPLE_AGX_UAT_ROOTS *Roots,
    unsigned long long VirtualAddress, unsigned long long PhysicalAddress,
    unsigned long long Length, APPLE_AGX_UAT_PROTECTION Protection,
    const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory) {
  APPLE_AGX_UAT_RESULT result;
  APPLE_AGX_UAT_HALF half;
  APPLE_AGX_UAT_PAGE *root;
  unsigned int first_page;
  unsigned int mapping_index;
  unsigned long long offset;
  unsigned long long mapped_length = 0ULL;

  if (Roots == 0 || Inventory == 0) {
    return AppleAgxUatResultInvalidArgument;
  }
  result = AppleAgxUatCheckOwnerArguments(Allocator, Inventory);
  if (result != AppleAgxUatResultOk) {
    return result;
  }
  result = AppleAgxUatValidateRange(Context, VirtualAddress, PhysicalAddress,
                                    Length, Protection, &half);
  if (result != AppleAgxUatResultOk) {
    return result;
  }
  if (Inventory->MappingCount >= Inventory->MappingCapacity) {
    return AppleAgxUatResultCapacity;
  }
  for (mapping_index = 0; mapping_index < Inventory->MappingCount;
       ++mapping_index) {
    APPLE_AGX_UAT_MAPPING *mapping = &Inventory->Mappings[mapping_index];
    unsigned long long last = VirtualAddress + Length - 1ULL;
    unsigned long long mapping_last =
        mapping->VirtualAddress + mapping->Length - 1ULL;
    if (mapping->Context == Context && VirtualAddress <= mapping_last &&
        mapping->VirtualAddress <= last) {
      return AppleAgxUatResultAlreadyMapped;
    }
  }

  root = AppleAgxUatFindPage(
      Inventory,
      half == AppleAgxUatTtbr0 ? Roots->Ttbr0PhysicalAddress
                               : Roots->Ttbr1PhysicalAddress,
      0u);
  if (root == 0) {
    return AppleAgxUatResultInvalidArgument;
  }
  first_page = Inventory->PageCount;
  for (offset = 0ULL; offset < Length; offset += J313_AGX_G2_PAGE_SIZE) {
    unsigned long long *leaf = 0;
    unsigned long long descriptor = 0ULL;
    result = AppleAgxUatFindLeaf(root, VirtualAddress + offset, Allocator,
                                 Inventory, &leaf);
    if (result != AppleAgxUatResultOk) {
      AppleAgxUatRollbackMap(root, VirtualAddress, mapped_length, first_page,
                             Allocator, Inventory);
      return result;
    }
    if (*leaf != 0ULL) {
      AppleAgxUatRollbackMap(root, VirtualAddress, mapped_length, first_page,
                             Allocator, Inventory);
      return AppleAgxUatResultAlreadyMapped;
    }
    result = AppleAgxUatEncodePageDescriptor(Context, PhysicalAddress + offset,
                                              Protection, &descriptor);
    if (result != AppleAgxUatResultOk) {
      AppleAgxUatRollbackMap(root, VirtualAddress, mapped_length, first_page,
                             Allocator, Inventory);
      return result;
    }
    *leaf = descriptor;
    mapped_length += J313_AGX_G2_PAGE_SIZE;
  }

  Inventory->Mappings[Inventory->MappingCount].Context = Context;
  Inventory->Mappings[Inventory->MappingCount].VirtualAddress = VirtualAddress;
  Inventory->Mappings[Inventory->MappingCount].PhysicalAddress = PhysicalAddress;
  Inventory->Mappings[Inventory->MappingCount].Length = Length;
  Inventory->Mappings[Inventory->MappingCount].Protection = Protection;
  Inventory->Mappings[Inventory->MappingCount].PhysicalStride =
      J313_AGX_G2_PAGE_SIZE;
  ++Inventory->MappingCount;
  return AppleAgxUatResultOk;
}

APPLE_AGX_UAT_RESULT AppleAgxUatMapPageList(
    unsigned int Context, const APPLE_AGX_UAT_ROOTS *Roots,
    unsigned long long VirtualAddress, const unsigned long long *PhysicalPages,
    unsigned int PageCount, APPLE_AGX_UAT_PROTECTION Protection,
    const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory) {
  APPLE_AGX_UAT_RESULT result;
  APPLE_AGX_UAT_HALF half;
  APPLE_AGX_UAT_PAGE *root;
  unsigned int first_page;
  unsigned int mapping_index;
  unsigned int index;
  unsigned long long length;
  unsigned long long mapped_length = 0ULL;

  if (Roots == 0 || Inventory == 0 || PhysicalPages == 0 || PageCount == 0u)
    return AppleAgxUatResultInvalidArgument;
  length = (unsigned long long)PageCount * J313_AGX_G2_PAGE_SIZE;
  result = AppleAgxUatCheckOwnerArguments(Allocator, Inventory);
  if (result != AppleAgxUatResultOk)
    return result;
  result = AppleAgxUatValidateRange(Context, VirtualAddress, PhysicalPages[0],
                                    length, Protection, &half);
  if (result != AppleAgxUatResultOk)
    return result;
  for (index = 0u; index < PageCount; ++index) {
    result = AppleAgxUatValidateRange(Context,
                                      VirtualAddress +
                                          (unsigned long long)index *
                                              J313_AGX_G2_PAGE_SIZE,
                                      PhysicalPages[index],
                                      J313_AGX_G2_PAGE_SIZE, Protection,
                                      &half);
    if (result != AppleAgxUatResultOk)
      return result;
  }
  if (Inventory->MappingCount >= Inventory->MappingCapacity)
    return AppleAgxUatResultCapacity;
  for (mapping_index = 0u; mapping_index < Inventory->MappingCount;
       ++mapping_index) {
    APPLE_AGX_UAT_MAPPING *mapping = &Inventory->Mappings[mapping_index];
    unsigned long long last = VirtualAddress + length - 1ULL;
    unsigned long long mapping_last = mapping->VirtualAddress + mapping->Length - 1ULL;
    if (mapping->Context == Context && VirtualAddress <= mapping_last &&
        mapping->VirtualAddress <= last)
      return AppleAgxUatResultAlreadyMapped;
  }
  root = AppleAgxUatFindPage(
      Inventory, half == AppleAgxUatTtbr0 ? Roots->Ttbr0PhysicalAddress
                                           : Roots->Ttbr1PhysicalAddress,
      0u);
  if (root == 0)
    return AppleAgxUatResultInvalidArgument;
  first_page = Inventory->PageCount;
  for (index = 0u; index < PageCount; ++index) {
    unsigned long long *leaf = 0;
    unsigned long long descriptor = 0ULL;
    result = AppleAgxUatFindLeaf(
        root, VirtualAddress + (unsigned long long)index * J313_AGX_G2_PAGE_SIZE,
        Allocator, Inventory, &leaf);
    if (result != AppleAgxUatResultOk || *leaf != 0ULL) {
      AppleAgxUatRollbackMap(root, VirtualAddress, mapped_length, first_page,
                             Allocator, Inventory);
      return result != AppleAgxUatResultOk ? result : AppleAgxUatResultAlreadyMapped;
    }
    result = AppleAgxUatEncodePageDescriptor(Context, PhysicalPages[index],
                                              Protection, &descriptor);
    if (result != AppleAgxUatResultOk) {
      AppleAgxUatRollbackMap(root, VirtualAddress, mapped_length, first_page,
                             Allocator, Inventory);
      return result;
    }
    *leaf = descriptor;
    mapped_length += J313_AGX_G2_PAGE_SIZE;
  }
  Inventory->Mappings[Inventory->MappingCount].Context = Context;
  Inventory->Mappings[Inventory->MappingCount].VirtualAddress = VirtualAddress;
  Inventory->Mappings[Inventory->MappingCount].PhysicalAddress = PhysicalPages[0];
  Inventory->Mappings[Inventory->MappingCount].Length = length;
  Inventory->Mappings[Inventory->MappingCount].Protection = Protection;
  /*
   * Do not conflate an MDL/ADL page list with the existing repeated-page
   * representation.  Callers must never derive a physical address from the
   * first page of this mapping.
   */
  Inventory->Mappings[Inventory->MappingCount].PhysicalStride =
      APPLE_AGX_UAT_PHYSICAL_STRIDE_SCATTER;
  ++Inventory->MappingCount;
  return AppleAgxUatResultOk;
}

static void AppleAgxUatClearParentReference(
    unsigned int ChildLevel, unsigned long long ChildPhysicalAddress,
    APPLE_AGX_UAT_INVENTORY *Inventory) {
  unsigned int page_index;
  unsigned int entry_index;

  for (page_index = 0; page_index < Inventory->PageCount; ++page_index) {
    APPLE_AGX_UAT_PAGE *parent = &Inventory->Pages[page_index];
    unsigned int entry_count;
    if (parent->Level + 1u != ChildLevel)
      continue;
    entry_count = parent->Level == 0u ? 8u : APPLE_AGX_UAT_TABLE_ENTRY_COUNT;
    for (entry_index = 0; entry_index < entry_count; ++entry_index) {
      if ((parent->Entries[entry_index] & APPLE_AGX_UAT_TABLE_ADDRESS_MASK) ==
          ChildPhysicalAddress) {
        parent->Entries[entry_index] = 0ULL;
        return;
      }
    }
  }
}

static void AppleAgxUatReleaseInventoryPage(
    unsigned int PageIndex, const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory) {
  APPLE_AGX_UAT_PAGE released = Inventory->Pages[PageIndex];
  unsigned int last_index = Inventory->PageCount - 1u;

  Allocator->ReleasePage(Allocator->Context, &released);
  if (PageIndex != last_index)
    Inventory->Pages[PageIndex] = Inventory->Pages[last_index];
  Inventory->Pages[last_index].PhysicalAddress = 0ULL;
  Inventory->Pages[last_index].Entries = 0;
  Inventory->Pages[last_index].Level = 0u;
  --Inventory->PageCount;
}

static void AppleAgxUatPruneEmptyTables(
    const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory) {
  unsigned int level;

  for (level = 2u; level > 0u; --level) {
    unsigned char removed;
    do {
      unsigned int page_index;
      removed = 0u;
      for (page_index = 0; page_index < Inventory->PageCount; ++page_index) {
        APPLE_AGX_UAT_PAGE *page = &Inventory->Pages[page_index];
        unsigned long long physical_address;
        if (page->Level != level || AppleAgxUatPageIsZero(page) == 0u)
          continue;
        physical_address = page->PhysicalAddress;
        AppleAgxUatClearParentReference(level, physical_address, Inventory);
        AppleAgxUatReleaseInventoryPage(page_index, Allocator, Inventory);
        removed = 1u;
        break;
      }
    } while (removed != 0u);
  }
}

APPLE_AGX_UAT_RESULT AppleAgxUatUnmap(
    unsigned int Context, const APPLE_AGX_UAT_ROOTS *Roots,
    unsigned long long VirtualAddress, unsigned long long Length,
    const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory) {
  APPLE_AGX_UAT_RESULT result;
  APPLE_AGX_UAT_HALF half;
  APPLE_AGX_UAT_PAGE *root;
  APPLE_AGX_UAT_MAPPING *mapping = 0;
  unsigned int mapping_index;
  unsigned long long offset;

  if (Roots == 0 || Inventory == 0)
    return AppleAgxUatResultInvalidArgument;
  result = AppleAgxUatCheckOwnerArguments(Allocator, Inventory);
  if (result != AppleAgxUatResultOk)
    return result;
  for (mapping_index = 0u; mapping_index < Inventory->MappingCount;
       ++mapping_index) {
    APPLE_AGX_UAT_MAPPING *candidate = &Inventory->Mappings[mapping_index];
    if (candidate->Context == Context &&
        candidate->VirtualAddress == VirtualAddress &&
        candidate->Length == Length) {
      mapping = candidate;
      break;
    }
  }
  if (mapping == 0)
    return AppleAgxUatResultNotMapped;

  result = AppleAgxUatValidateRange(Context, VirtualAddress,
                                    mapping->PhysicalAddress, Length,
                                    mapping->Protection, &half);
  if (result != AppleAgxUatResultOk)
    return result;
  root = AppleAgxUatFindPage(
      Inventory,
      half == AppleAgxUatTtbr0 ? Roots->Ttbr0PhysicalAddress
                               : Roots->Ttbr1PhysicalAddress,
      0u);
  if (root == 0)
    return AppleAgxUatResultInvalidArgument;

  /* Validate the full exact mapping before changing any leaf. */
  for (offset = 0ULL; offset < Length; offset += J313_AGX_G2_PAGE_SIZE) {
    unsigned long long *leaf = 0;
    if (AppleAgxUatFindExistingLeaf(root, VirtualAddress + offset, Inventory,
                                    &leaf) != AppleAgxUatResultOk ||
        leaf == 0 || *leaf == 0ULL)
      return AppleAgxUatResultNotMapped;
  }
  for (offset = 0ULL; offset < Length; offset += J313_AGX_G2_PAGE_SIZE) {
    unsigned long long *leaf = 0;
    (void)AppleAgxUatFindExistingLeaf(root, VirtualAddress + offset,
                                      Inventory, &leaf);
    *leaf = 0ULL;
  }

  for (; mapping_index + 1u < Inventory->MappingCount; ++mapping_index)
    Inventory->Mappings[mapping_index] =
        Inventory->Mappings[mapping_index + 1u];
  --Inventory->MappingCount;
  Inventory->Mappings[Inventory->MappingCount].Context = 0u;
  Inventory->Mappings[Inventory->MappingCount].VirtualAddress = 0ULL;
  Inventory->Mappings[Inventory->MappingCount].PhysicalAddress = 0ULL;
  Inventory->Mappings[Inventory->MappingCount].Length = 0ULL;
  Inventory->Mappings[Inventory->MappingCount].Protection =
      (APPLE_AGX_UAT_PROTECTION)0;
  Inventory->Mappings[Inventory->MappingCount].PhysicalStride = 0ULL;
  AppleAgxUatPruneEmptyTables(Allocator, Inventory);
  return AppleAgxUatResultOk;
}

static APPLE_AGX_UAT_RESULT AppleAgxUatValidateExactMapping(
    unsigned int Context, const APPLE_AGX_UAT_ROOTS *Roots,
    const APPLE_AGX_UAT_RANGE *Range,
    APPLE_AGX_UAT_INVENTORY *Inventory) {
  APPLE_AGX_UAT_HALF half;
  APPLE_AGX_UAT_PAGE *root;
  APPLE_AGX_UAT_RESULT result;
  unsigned int mapping_index;
  unsigned long long offset;
  unsigned char found = 0u;

  result = AppleAgxUatValidateRange(
      Context, Range->VirtualAddress, Range->PhysicalAddress, Range->Length,
      Range->Protection, &half);
  if (result != AppleAgxUatResultOk)
    return result;
  for (mapping_index = 0u; mapping_index < Inventory->MappingCount;
       ++mapping_index) {
    const APPLE_AGX_UAT_MAPPING *mapping =
        &Inventory->Mappings[mapping_index];
    if (mapping->Context == Context &&
        mapping->VirtualAddress == Range->VirtualAddress &&
        mapping->PhysicalAddress == Range->PhysicalAddress &&
        mapping->Length == Range->Length &&
        mapping->Protection == Range->Protection) {
      found = 1u;
      break;
    }
  }
  if (found == 0u)
    return AppleAgxUatResultNotMapped;
  root = AppleAgxUatFindPage(
      Inventory,
      half == AppleAgxUatTtbr0 ? Roots->Ttbr0PhysicalAddress
                               : Roots->Ttbr1PhysicalAddress,
      0u);
  if (root == 0)
    return AppleAgxUatResultInvalidArgument;
  for (offset = 0ULL; offset < Range->Length;
       offset += J313_AGX_G2_PAGE_SIZE) {
    unsigned long long *leaf = 0;
    if (AppleAgxUatFindExistingLeaf(
            root, Range->VirtualAddress + offset, Inventory, &leaf) !=
            AppleAgxUatResultOk ||
        leaf == 0 || *leaf == 0ULL)
      return AppleAgxUatResultNotMapped;
  }
  return AppleAgxUatResultOk;
}

APPLE_AGX_UAT_RESULT AppleAgxUatMapBatch(
    unsigned int Context, const APPLE_AGX_UAT_ROOTS *Roots,
    const APPLE_AGX_UAT_RANGE *Ranges, unsigned int RangeCount,
    const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory) {
  APPLE_AGX_UAT_RESULT result;
  unsigned int range_index;

  if (Roots == 0 || Ranges == 0 || Inventory == 0 || RangeCount == 0u)
    return AppleAgxUatResultInvalidArgument;
  result = AppleAgxUatCheckOwnerArguments(Allocator, Inventory);
  if (result != AppleAgxUatResultOk)
    return result;
  if (RangeCount > Inventory->MappingCapacity - Inventory->MappingCount)
    return AppleAgxUatResultCapacity;

  for (range_index = 0u; range_index < RangeCount; ++range_index) {
    result = AppleAgxUatMap(
        Context, Roots, Ranges[range_index].VirtualAddress,
        Ranges[range_index].PhysicalAddress, Ranges[range_index].Length,
        Ranges[range_index].Protection, Allocator, Inventory);
    if (result != AppleAgxUatResultOk) {
      while (range_index > 0u) {
        --range_index;
        (void)AppleAgxUatUnmap(Context, Roots,
                               Ranges[range_index].VirtualAddress,
                               Ranges[range_index].Length, Allocator,
                               Inventory);
      }
      return result;
    }
  }
  return AppleAgxUatResultOk;
}

APPLE_AGX_UAT_RESULT AppleAgxUatUnmapBatch(
    unsigned int Context, const APPLE_AGX_UAT_ROOTS *Roots,
    const APPLE_AGX_UAT_RANGE *Ranges, unsigned int RangeCount,
    const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory) {
  APPLE_AGX_UAT_RESULT result;
  unsigned int range_index;

  if (Roots == 0 || Ranges == 0 || Inventory == 0 || RangeCount == 0u)
    return AppleAgxUatResultInvalidArgument;
  result = AppleAgxUatCheckOwnerArguments(Allocator, Inventory);
  if (result != AppleAgxUatResultOk)
    return result;
  for (range_index = 0u; range_index < RangeCount; ++range_index) {
    result = AppleAgxUatValidateExactMapping(Context, Roots,
                                             &Ranges[range_index], Inventory);
    if (result != AppleAgxUatResultOk)
      return result;
  }
  for (range_index = 0u; range_index < RangeCount; ++range_index) {
    result = AppleAgxUatUnmap(Context, Roots,
                              Ranges[range_index].VirtualAddress,
                              Ranges[range_index].Length, Allocator, Inventory);
    if (result != AppleAgxUatResultOk)
      return result;
  }
  return AppleAgxUatResultOk;
}

APPLE_AGX_UAT_RESULT AppleAgxUatReplaceBatchWithPage(
    unsigned int Context, const APPLE_AGX_UAT_ROOTS *Roots,
    const APPLE_AGX_UAT_RANGE *Ranges, unsigned int RangeCount,
    unsigned long long ReplacementPhysicalAddress,
    APPLE_AGX_UAT_PROTECTION ReplacementProtection,
    const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory) {
  APPLE_AGX_UAT_RESULT result;
  unsigned int range_index;

  if (Roots == 0 || Ranges == 0 || Inventory == 0 || RangeCount == 0u)
    return AppleAgxUatResultInvalidArgument;
  result = AppleAgxUatCheckOwnerArguments(Allocator, Inventory);
  if (result != AppleAgxUatResultOk)
    return result;
  if ((ReplacementPhysicalAddress & (J313_AGX_G2_PAGE_SIZE - 1ULL)) != 0ULL)
    return AppleAgxUatResultMisaligned;

  /* Validate the complete transaction before changing a leaf or record. */
  for (range_index = 0u; range_index < RangeCount; ++range_index) {
    unsigned int earlier;
    APPLE_AGX_UAT_HALF ignored_half;
    result = AppleAgxUatValidateExactMapping(Context, Roots,
                                             &Ranges[range_index], Inventory);
    if (result != AppleAgxUatResultOk)
      return result;
    result = AppleAgxUatValidateRange(
        Context, Ranges[range_index].VirtualAddress,
        ReplacementPhysicalAddress, Ranges[range_index].Length,
        ReplacementProtection, &ignored_half);
    if (result != AppleAgxUatResultOk)
      return result;
    for (earlier = 0u; earlier < range_index; ++earlier) {
      if (Ranges[earlier].VirtualAddress ==
              Ranges[range_index].VirtualAddress &&
          Ranges[earlier].Length == Ranges[range_index].Length)
        return AppleAgxUatResultInvalidArgument;
    }
  }

  for (range_index = 0u; range_index < RangeCount; ++range_index) {
    APPLE_AGX_UAT_HALF half;
    APPLE_AGX_UAT_PAGE *root;
    APPLE_AGX_UAT_MAPPING *mapping = 0;
    unsigned int mapping_index;
    unsigned long long offset;

    (void)AppleAgxUatValidateRange(
        Context, Ranges[range_index].VirtualAddress,
        ReplacementPhysicalAddress, Ranges[range_index].Length,
        ReplacementProtection, &half);
    root = AppleAgxUatFindPage(
        Inventory,
        half == AppleAgxUatTtbr0 ? Roots->Ttbr0PhysicalAddress
                                 : Roots->Ttbr1PhysicalAddress,
        0u);
    for (mapping_index = 0u; mapping_index < Inventory->MappingCount;
         ++mapping_index) {
      APPLE_AGX_UAT_MAPPING *candidate =
          &Inventory->Mappings[mapping_index];
      if (candidate->Context == Context &&
          candidate->VirtualAddress == Ranges[range_index].VirtualAddress &&
          candidate->PhysicalAddress == Ranges[range_index].PhysicalAddress &&
          candidate->Length == Ranges[range_index].Length &&
          candidate->Protection == Ranges[range_index].Protection) {
        mapping = candidate;
        break;
      }
    }
    if (mapping == 0)
      return AppleAgxUatResultNotMapped;
    for (offset = 0ULL; offset < Ranges[range_index].Length;
         offset += J313_AGX_G2_PAGE_SIZE) {
      unsigned long long *leaf = 0;
      unsigned long long descriptor = 0ULL;
      (void)AppleAgxUatFindExistingLeaf(
          root, Ranges[range_index].VirtualAddress + offset, Inventory, &leaf);
      (void)AppleAgxUatEncodePageDescriptor(
          Context, ReplacementPhysicalAddress, ReplacementProtection,
          &descriptor);
      *leaf = descriptor;
    }
    mapping->PhysicalAddress = ReplacementPhysicalAddress;
    mapping->Protection = ReplacementProtection;
    mapping->PhysicalStride = 0ULL;
  }
  return AppleAgxUatResultOk;
}

APPLE_AGX_UAT_RESULT AppleAgxUatReplaceBatch(
    unsigned int Context, const APPLE_AGX_UAT_ROOTS *Roots,
    const APPLE_AGX_UAT_RANGE *OldRanges,
    const APPLE_AGX_UAT_RANGE *NewRanges, unsigned int RangeCount,
    const APPLE_AGX_UAT_ALLOCATOR *Allocator,
    APPLE_AGX_UAT_INVENTORY *Inventory) {
  APPLE_AGX_UAT_RESULT result;
  unsigned int range_index;

  if (Roots == 0 || OldRanges == 0 || NewRanges == 0 || Inventory == 0 ||
      RangeCount == 0u)
    return AppleAgxUatResultInvalidArgument;
  result = AppleAgxUatCheckOwnerArguments(Allocator, Inventory);
  if (result != AppleAgxUatResultOk)
    return result;

  for (range_index = 0u; range_index < RangeCount; ++range_index) {
    APPLE_AGX_UAT_HALF ignored_half;
    unsigned int earlier;
    if (OldRanges[range_index].VirtualAddress !=
            NewRanges[range_index].VirtualAddress ||
        OldRanges[range_index].Length != NewRanges[range_index].Length)
      return AppleAgxUatResultInvalidArgument;
    result = AppleAgxUatValidateExactMapping(Context, Roots,
                                             &OldRanges[range_index],
                                             Inventory);
    if (result != AppleAgxUatResultOk)
      return result;
    result = AppleAgxUatValidateRange(
        Context, NewRanges[range_index].VirtualAddress,
        NewRanges[range_index].PhysicalAddress,
        NewRanges[range_index].Length, NewRanges[range_index].Protection,
        &ignored_half);
    if (result != AppleAgxUatResultOk)
      return result;
    for (earlier = 0u; earlier < range_index; ++earlier) {
      if (OldRanges[earlier].VirtualAddress ==
              OldRanges[range_index].VirtualAddress &&
          OldRanges[earlier].Length == OldRanges[range_index].Length)
        return AppleAgxUatResultInvalidArgument;
    }
  }

  for (range_index = 0u; range_index < RangeCount; ++range_index) {
    APPLE_AGX_UAT_HALF half;
    APPLE_AGX_UAT_PAGE *root;
    APPLE_AGX_UAT_MAPPING *mapping = 0;
    unsigned int mapping_index;
    unsigned long long offset;
    (void)AppleAgxUatValidateRange(
        Context, NewRanges[range_index].VirtualAddress,
        NewRanges[range_index].PhysicalAddress,
        NewRanges[range_index].Length, NewRanges[range_index].Protection,
        &half);
    root = AppleAgxUatFindPage(
        Inventory,
        half == AppleAgxUatTtbr0 ? Roots->Ttbr0PhysicalAddress
                                 : Roots->Ttbr1PhysicalAddress,
        0u);
    for (mapping_index = 0u; mapping_index < Inventory->MappingCount;
         ++mapping_index) {
      APPLE_AGX_UAT_MAPPING *candidate =
          &Inventory->Mappings[mapping_index];
      if (candidate->Context == Context &&
          candidate->VirtualAddress == OldRanges[range_index].VirtualAddress &&
          candidate->PhysicalAddress == OldRanges[range_index].PhysicalAddress &&
          candidate->Length == OldRanges[range_index].Length &&
          candidate->Protection == OldRanges[range_index].Protection) {
        mapping = candidate;
        break;
      }
    }
    if (mapping == 0)
      return AppleAgxUatResultNotMapped;
    for (offset = 0ULL; offset < NewRanges[range_index].Length;
         offset += J313_AGX_G2_PAGE_SIZE) {
      unsigned long long *leaf = 0;
      unsigned long long descriptor = 0ULL;
      (void)AppleAgxUatFindExistingLeaf(
          root, NewRanges[range_index].VirtualAddress + offset, Inventory,
          &leaf);
      (void)AppleAgxUatEncodePageDescriptor(
          Context, NewRanges[range_index].PhysicalAddress + offset,
          NewRanges[range_index].Protection, &descriptor);
      *leaf = descriptor;
    }
    mapping->PhysicalAddress = NewRanges[range_index].PhysicalAddress;
    mapping->Protection = NewRanges[range_index].Protection;
    mapping->PhysicalStride = J313_AGX_G2_PAGE_SIZE;
  }
  return AppleAgxUatResultOk;
}

void AppleAgxUatDestroy(const APPLE_AGX_UAT_ALLOCATOR *Allocator,
                        APPLE_AGX_UAT_INVENTORY *Inventory) {
  if (Allocator == 0 || Inventory == 0 || Allocator->ReleasePage == 0) {
    return;
  }
  AppleAgxUatReleaseFrom(0u, Allocator, Inventory);
  while (Inventory->MappingCount > 0u) {
    APPLE_AGX_UAT_MAPPING *mapping =
        &Inventory->Mappings[Inventory->MappingCount - 1u];
    mapping->Context = 0u;
    mapping->VirtualAddress = 0ULL;
    mapping->PhysicalAddress = 0ULL;
    mapping->Length = 0ULL;
    mapping->Protection = (APPLE_AGX_UAT_PROTECTION)0;
    mapping->PhysicalStride = 0ULL;
    --Inventory->MappingCount;
  }
}
