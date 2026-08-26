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
  ++Inventory->MappingCount;
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
    --Inventory->MappingCount;
  }
}
