#include "render_win32_transport.h"

#include <assert.h>
#include <string.h>

typedef struct _TEST_COMMAND {
  APPLE_AGX_WIN32_COMMAND_HEADER Header;
  APPLE_AGX_WIN32_ALLOCATION_REFERENCE Reference;
  APPLE_AGX_WIN32_CLEAR_PAYLOAD Clear;
} TEST_COMMAND;

typedef struct _TEST_ALLOCATION {
  APPLE_AGX_U32 Index;
  APPLE_AGX_U64 Owner;
  ADMISSION_WIN32_ALLOCATION_FACT Fact;
} TEST_ALLOCATION;

typedef struct _TEST_LOOKUP {
  APPLE_AGX_U64 ExpectedOwner;
  TEST_ALLOCATION Allocations[16];
  APPLE_AGX_U32 Count;
} TEST_LOOKUP;

static void seal(TEST_COMMAND *command) {
  command->Header.ContentHash = 0ULL;
  command->Header.ContentHash =
      AppleAgxWin32CommandHash(command, sizeof(*command));
}

static TEST_COMMAND make_command(void) {
  TEST_COMMAND command;
  memset(&command, 0, sizeof(command));
  command.Header.Magic = APPLE_AGX_WIN32_COMMAND_MAGIC;
  command.Header.Version = APPLE_AGX_WIN32_COMMAND_VERSION;
  command.Header.HeaderBytes = sizeof(command.Header);
  command.Header.TotalBytes = sizeof(command);
  command.Header.Opcode = AppleAgxWin32OpcodeClear;
  command.Header.Generation = 7u;
  command.Header.ReferenceCount = 1u;
  command.Header.ReferencesOffset = sizeof(command.Header);
  command.Header.PayloadOffset =
      sizeof(command.Header) + sizeof(command.Reference);
  command.Header.PayloadBytes = sizeof(command.Clear);
  command.Reference.AllocationIndex = 1u;
  command.Reference.Access = AppleAgxWin32AccessWrite;
  command.Reference.Role = AppleAgxWin32RoleRenderTarget;
  command.Reference.Offset = 0x10000ULL;
  command.Reference.Bytes = 0x40000ULL;
  command.Clear.StructBytes = sizeof(command.Clear);
  command.Clear.Format = AppleAgxWin32FormatBgra8Unorm;
  command.Clear.Color = 0xff123456u;
  command.Clear.SurfaceWidth = 256u;
  command.Clear.SurfaceHeight = 256u;
  command.Clear.SurfacePitch = 1024u;
  command.Clear.Left = 0u;
  command.Clear.Top = 0u;
  command.Clear.Right = 256u;
  command.Clear.Bottom = 256u;
  command.Clear.DestinationReference = 0u;
  seal(&command);
  return command;
}

static TEST_LOOKUP make_lookup(void) {
  TEST_LOOKUP lookup;
  memset(&lookup, 0, sizeof(lookup));
  lookup.ExpectedOwner = 0x1111ULL;
  lookup.Count = 2u;
  lookup.Allocations[0].Index = 0u;
  lookup.Allocations[0].Owner = lookup.ExpectedOwner;
  lookup.Allocations[0].Fact.AllocationToken = 0xa000ULL;
  lookup.Allocations[0].Fact.Bytes = 0x100000ULL;
  lookup.Allocations[0].Fact.SegmentId = 2u;
  lookup.Allocations[0].Fact.Writable = 1u;
  lookup.Allocations[0].Fact.Generation = 7u;
  lookup.Allocations[1].Index = 1u;
  lookup.Allocations[1].Owner = lookup.ExpectedOwner;
  lookup.Allocations[1].Fact.AllocationToken = 0xb000ULL;
  lookup.Allocations[1].Fact.Bytes = 0x100000ULL;
  lookup.Allocations[1].Fact.SegmentId = 2u;
  lookup.Allocations[1].Fact.Writable = 1u;
  lookup.Allocations[1].Fact.Generation = 7u;
  return lookup;
}

static int lookup_allocation(void *Context, APPLE_AGX_U32 AllocationIndex,
                             ADMISSION_WIN32_ALLOCATION_FACT *Fact) {
  TEST_LOOKUP *lookup = (TEST_LOOKUP *)Context;
  APPLE_AGX_U32 index;
  if (lookup == NULL || Fact == NULL)
    return 0;
  for (index = 0u; index < lookup->Count; ++index) {
    if (lookup->Allocations[index].Index == AllocationIndex &&
        lookup->Allocations[index].Owner == lookup->ExpectedOwner) {
      *Fact = lookup->Allocations[index].Fact;
      return 1;
    }
  }
  return 0;
}

static ADMISSION_WIN32_TRANSPORT_RESULT validate(
    TEST_COMMAND *command, TEST_LOOKUP *lookup,
    ADMISSION_WIN32_ALLOCATION_FACT *facts) {
  APPLE_AGX_WIN32_COMMAND_VIEW view;
  assert(AppleAgxWin32CommandValidate(command, sizeof(*command), 7u, 2u,
                                      &view) == AppleAgxWin32AbiSuccess);
  return AdmissionWin32ValidateReferences(&view, 7u, lookup_allocation, lookup,
                                          facts, 1u);
}

static void test_valid_noncontiguous_index_and_range(void) {
  TEST_COMMAND command = make_command();
  TEST_LOOKUP lookup = make_lookup();
  ADMISSION_WIN32_ALLOCATION_FACT facts[1];
  memset(facts, 0, sizeof(facts));
  assert(validate(&command, &lookup, facts) ==
         AdmissionWin32TransportSuccess);
  assert(facts[0].AllocationToken == 0xb000ULL);
  assert(facts[0].Bytes == 0x100000ULL);
  assert(facts[0].SegmentId == 2u);
}

static void test_owner_generation_and_access_rejections(void) {
  TEST_COMMAND command;
  TEST_LOOKUP lookup;
  ADMISSION_WIN32_ALLOCATION_FACT facts[1];

  command = make_command();
  lookup = make_lookup();
  lookup.Allocations[1].Owner = 0x2222ULL;
  assert(validate(&command, &lookup, facts) ==
         AdmissionWin32TransportLookup);

  lookup = make_lookup();
  lookup.Allocations[1].Fact.Generation = 6u;
  assert(validate(&command, &lookup, facts) ==
         AdmissionWin32TransportStaleGeneration);

  lookup = make_lookup();
  lookup.Allocations[1].Fact.Writable = 0u;
  assert(validate(&command, &lookup, facts) ==
         AdmissionWin32TransportAccess);

  lookup = make_lookup();
  lookup.Allocations[1].Fact.ActiveForDisplay = 1u;
  assert(validate(&command, &lookup, facts) ==
         AdmissionWin32TransportActiveDisplay);
}

static void test_alignment_capacity_and_lookup_rejections(void) {
  TEST_COMMAND command;
  TEST_LOOKUP lookup;
  ADMISSION_WIN32_ALLOCATION_FACT facts[1];

  command = make_command();
  lookup = make_lookup();
  command.Reference.Offset = 2ULL;
  seal(&command);
  assert(validate(&command, &lookup, facts) ==
         AdmissionWin32TransportAlignment);

  command = make_command();
  command.Reference.Offset = 0xf0000ULL;
  command.Reference.Bytes = 0x20000ULL;
  seal(&command);
  assert(validate(&command, &lookup, facts) ==
         AdmissionWin32TransportRange);

  command = make_command();
  lookup.Count = 1u;
  assert(validate(&command, &lookup, facts) ==
         AdmissionWin32TransportLookup);
}

static void test_failed_validation_does_not_publish_partial_facts(void) {
  TEST_COMMAND command = make_command();
  TEST_LOOKUP lookup = make_lookup();
  ADMISSION_WIN32_ALLOCATION_FACT facts[1];
  ADMISSION_WIN32_ALLOCATION_FACT original[1];
  memset(facts, 0xa5, sizeof(facts));
  memcpy(original, facts, sizeof(facts));
  lookup.Allocations[1].Fact.ActiveForDisplay = 1u;
  assert(validate(&command, &lookup, facts) ==
         AdmissionWin32TransportActiveDisplay);
  assert(memcmp(facts, original, sizeof(facts)) == 0);
}

static void test_overlapping_writable_ranges_are_rejected(void) {
  APPLE_AGX_WIN32_COMMAND_HEADER header;
  APPLE_AGX_WIN32_ALLOCATION_REFERENCE references[2];
  APPLE_AGX_WIN32_CLEAR_PAYLOAD clear;
  APPLE_AGX_WIN32_COMMAND_VIEW view;
  TEST_LOOKUP lookup = make_lookup();
  ADMISSION_WIN32_ALLOCATION_FACT facts[2];
  memset(&header, 0, sizeof(header));
  memset(references, 0, sizeof(references));
  memset(&clear, 0, sizeof(clear));
  memset(&view, 0, sizeof(view));
  header.Generation = 7u;
  header.ReferenceCount = 2u;
  references[0].AllocationIndex = 1u;
  references[0].Access = AppleAgxWin32AccessWrite;
  references[0].Offset = 0x1000ULL;
  references[0].Bytes = 0x4000ULL;
  references[1] = references[0];
  references[1].Offset = 0x3000ULL;
  view.Header = &header;
  view.References = references;
  view.Clear = &clear;
  assert(AdmissionWin32ValidateReferences(
             &view, 7u, lookup_allocation, &lookup, facts, 2u) ==
         AdmissionWin32TransportOverlap);
}

static void test_context_generation_contract(void) {
  ADMISSION_WIN32_CONTEXT_CREATE create;
  APPLE_AGX_U32 generation = 0xa5a5a5a5u;
  APPLE_AGX_BOOL win32 = APPLE_AGX_TRUE;
  memset(&create, 0, sizeof(create));
  create.Magic = ADMISSION_WIN32_CONTEXT_MAGIC;
  create.Version = ADMISSION_WIN32_CONTEXT_VERSION;
  create.Bytes = sizeof(create);
  create.Generation = 7u;

  assert(AdmissionWin32ContextCreateValidate(
             &create, sizeof(create), APPLE_AGX_FALSE, APPLE_AGX_FALSE,
             &generation, &win32) == AdmissionWin32TransportSuccess);
  assert(generation == 7u);
  assert(win32 == APPLE_AGX_TRUE);

  generation = 99u;
  win32 = APPLE_AGX_TRUE;
  assert(AdmissionWin32ContextCreateValidate(
             NULL, 0u, APPLE_AGX_TRUE, APPLE_AGX_FALSE, &generation,
             &win32) == AdmissionWin32TransportSuccess);
  assert(generation == 0u);
  assert(win32 == APPLE_AGX_FALSE);

  assert(AdmissionWin32ContextCreateValidate(
             NULL, 0u, APPLE_AGX_FALSE, APPLE_AGX_TRUE, &generation,
             &win32) == AdmissionWin32TransportSuccess);
  assert(generation == 0u);
  assert(win32 == APPLE_AGX_FALSE);
  assert(AdmissionWin32ContextCreateValidate(
             NULL, 0u, APPLE_AGX_FALSE, APPLE_AGX_FALSE, &generation,
             &win32) == AdmissionWin32TransportContext);

  create.Generation = 0u;
  assert(AdmissionWin32ContextCreateValidate(
             &create, sizeof(create), APPLE_AGX_FALSE, APPLE_AGX_FALSE,
             &generation, &win32) == AdmissionWin32TransportContext);
  create.Generation = 7u;
  create.Magic ^= 1u;
  assert(AdmissionWin32ContextCreateValidate(
             &create, sizeof(create), APPLE_AGX_FALSE, APPLE_AGX_FALSE,
             &generation, &win32) == AdmissionWin32TransportContext);
  create.Magic ^= 1u;
  create.Reserved = 1u;
  assert(AdmissionWin32ContextCreateValidate(
             &create, sizeof(create), APPLE_AGX_FALSE, APPLE_AGX_FALSE,
             &generation, &win32) == AdmissionWin32TransportContext);

  memset(&create, 0, sizeof(create));
  create.Magic = ADMISSION_WIN32_CONTEXT_MAGIC;
  create.Version = ADMISSION_WIN32_CONTEXT_VERSION;
  create.Bytes = sizeof(create);
  create.Generation = 7u;
  assert(AdmissionWin32ContextCreateValidate(
             &create, sizeof(create), APPLE_AGX_TRUE, APPLE_AGX_FALSE,
             &generation, &win32) == AdmissionWin32TransportContext);
}

static ADMISSION_WIN32_ALLOCATION_CREATE make_class_allocation(void) {
  ADMISSION_WIN32_ALLOCATION_CREATE create;
  memset(&create, 0, sizeof(create));
  create.Magic = ADMISSION_WIN32_ALLOCATION_MAGIC;
  create.Version = ADMISSION_WIN32_ALLOCATION_VERSION;
  create.Bytes = sizeof(create);
  create.ClassId = AgxWin32BufferClassShader;
  create.Flags = AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead;
  assert(AdmissionAllocationDescribe(
      0x8000u, 1u, 1u,
      ADMISSION_WIN32_ALLOCATION_STAGING_CPUVISIBLE,
      ADMISSION_WIN32_ALLOCATION_FORMAT_A8, 1u, &create.Allocation));
  return create;
}

static void test_class_allocation_contract(void) {
  ADMISSION_WIN32_ALLOCATION_CREATE create = make_class_allocation();
  ADMISSION_ALLOCATION_DESCRIPTION description;
  APPLE_AGX_U32 classId = 0u;
  APPLE_AGX_U32 flags = 0u;
  assert(AdmissionWin32AllocationCreateValidate(
      &create, sizeof(create), &description, &classId, &flags) ==
      AdmissionWin32TransportSuccess);
  assert(classId == AgxWin32BufferClassShader);
  assert(flags == (AppleAgxWin32BufferCpuWrite |
                   AppleAgxWin32BufferGpuRead));
  assert(description.Size == 0x8000ULL);

  create = make_class_allocation();
  create.Version++;
  assert(AdmissionWin32AllocationCreateValidate(
      &create, sizeof(create), &description, &classId, &flags) ==
      AdmissionWin32TransportClass);
  create = make_class_allocation();
  create.ClassId = 99u;
  assert(AdmissionWin32AllocationCreateValidate(
      &create, sizeof(create), &description, &classId, &flags) ==
      AdmissionWin32TransportClass);
  create = make_class_allocation();
  create.Flags = AppleAgxWin32BufferGpuRead;
  assert(AdmissionWin32AllocationCreateValidate(
      &create, sizeof(create), &description, &classId, &flags) ==
      AdmissionWin32TransportAccess);
  create = make_class_allocation();
  create.Allocation.Size -= 0x4000ULL;
  assert(AdmissionWin32AllocationCreateValidate(
      &create, sizeof(create), &description, &classId, &flags) ==
      AdmissionWin32TransportClass);
  create = make_class_allocation();
  create.Reserved[0] = 1u;
  assert(AdmissionWin32AllocationCreateValidate(
      &create, sizeof(create), &description, &classId, &flags) ==
      AdmissionWin32TransportClass);
}

static void test_draw_role_class_and_relocation_ownership(void) {
  APPLE_AGX_WIN32_COMMAND_HEADER header;
  APPLE_AGX_WIN32_ALLOCATION_REFERENCE references[5];
  APPLE_AGX_WIN32_DRAW_PAYLOAD draw;
  APPLE_AGX_WIN32_RELOCATION relocations[2];
  APPLE_AGX_WIN32_COMMAND_VIEW view;
  TEST_LOOKUP lookup;
  ADMISSION_WIN32_ALLOCATION_FACT facts[5];
  unsigned index;
  memset(&header, 0, sizeof(header));
  memset(references, 0, sizeof(references));
  memset(&draw, 0, sizeof(draw));
  memset(relocations, 0, sizeof(relocations));
  memset(&view, 0, sizeof(view));
  memset(&lookup, 0, sizeof(lookup));
  lookup.ExpectedOwner = 0x1111ULL;
  lookup.Count = 5u;
  header.Opcode = AppleAgxWin32OpcodeDraw;
  header.Generation = 7u;
  header.ReferenceCount = 5u;
  view.Header = &header;
  view.References = references;
  view.Draw = &draw;
  view.Relocations = relocations;
  references[0] = (APPLE_AGX_WIN32_ALLOCATION_REFERENCE){
      0u, AppleAgxWin32AccessWrite, AppleAgxWin32RoleRenderTarget,
      0u, 0u, 0x1000000ULL};
  references[1] = (APPLE_AGX_WIN32_ALLOCATION_REFERENCE){
      1u, AppleAgxWin32AccessRead | AppleAgxWin32AccessExecute,
      AppleAgxWin32RoleShader, 0u, 0u, 0x4000ULL};
  references[2] = (APPLE_AGX_WIN32_ALLOCATION_REFERENCE){
      2u, AppleAgxWin32AccessRead, AppleAgxWin32RoleEncoder,
      0u, 0u, 0x4000ULL};
  references[3] = (APPLE_AGX_WIN32_ALLOCATION_REFERENCE){
      3u, AppleAgxWin32AccessRead, AppleAgxWin32RoleVertex,
      0u, 0u, 0x4000ULL};
  references[4] = (APPLE_AGX_WIN32_ALLOCATION_REFERENCE){
      4u, AppleAgxWin32AccessRead, AppleAgxWin32RoleEncoder,
      0u, 0u, 0x4000ULL};
  draw.SurfacePitch = 10240u;
  draw.SurfaceHeight = 1600u;
  draw.DestinationReference = 0u;
  draw.RelocationCount = 2u;
  relocations[0].WidthBytes = 8u;
  relocations[0].DestinationReference = 2u;
  relocations[0].TargetReference = 1u;
  relocations[1] = relocations[0];
  relocations[1].DestinationOffset = 8u;
  relocations[1].TargetReference = 3u;
  for (index = 0u; index < lookup.Count; ++index) {
    lookup.Allocations[index].Index = index;
    lookup.Allocations[index].Owner = lookup.ExpectedOwner;
    lookup.Allocations[index].Fact.AllocationToken = 0xa000ULL + index;
    lookup.Allocations[index].Fact.Bytes = 0x1000000ULL;
    lookup.Allocations[index].Fact.SegmentId = 2u;
    lookup.Allocations[index].Fact.Writable = 1u;
    lookup.Allocations[index].Fact.Generation = 7u;
  }
  lookup.Allocations[0].Fact.ClassId = 0u;
  lookup.Allocations[1].Fact.ClassId = AgxWin32BufferClassShader;
  lookup.Allocations[1].Fact.Flags =
      AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead;
  lookup.Allocations[2].Fact.ClassId = AgxWin32BufferClassEncoder;
  lookup.Allocations[2].Fact.Flags =
      AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead;
  lookup.Allocations[3].Fact.ClassId = AgxWin32BufferClassGeneral;
  lookup.Allocations[3].Fact.Flags =
      AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead;
  lookup.Allocations[4].Fact.ClassId = AgxWin32BufferClassEncoder;
  lookup.Allocations[4].Fact.Flags =
      AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead;
  assert(AdmissionWin32ValidateReferences(
      &view, 7u, lookup_allocation, &lookup, facts, 5u) ==
      AdmissionWin32TransportSuccess);

  lookup.Allocations[1].Fact.ClassId = AgxWin32BufferClassGeneral;
  assert(AdmissionWin32ValidateReferences(
      &view, 7u, lookup_allocation, &lookup, facts, 5u) ==
      AdmissionWin32TransportClass);
  lookup.Allocations[1].Fact.ClassId = AgxWin32BufferClassShader;
  lookup.Allocations[2].Fact.Flags = AppleAgxWin32BufferCpuWrite;
  assert(AdmissionWin32ValidateReferences(
      &view, 7u, lookup_allocation, &lookup, facts, 5u) ==
      AdmissionWin32TransportAccess);
  lookup.Allocations[2].Fact.Flags =
      AppleAgxWin32BufferCpuWrite | AppleAgxWin32BufferGpuRead;
  lookup.Allocations[2].Fact.AllocationToken = 0xc000ULL;
  lookup.Allocations[4].Fact.AllocationToken = 0xc000ULL;
  references[2].Offset = 0x1000ULL;
  references[4].Offset = 0x1008ULL;
  relocations[0].DestinationOffset = 8u;
  relocations[1].DestinationReference = 4u;
  relocations[1].DestinationOffset = 0u;
  assert(AdmissionWin32ValidateReferences(
      &view, 7u, lookup_allocation, &lookup, facts, 5u) ==
      AdmissionWin32TransportOverlap);
}

int main(void) {
  test_valid_noncontiguous_index_and_range();
  test_owner_generation_and_access_rejections();
  test_alignment_capacity_and_lookup_rejections();
  test_failed_validation_does_not_publish_partial_facts();
  test_overlapping_writable_ranges_are_rejected();
  test_context_generation_contract();
  test_class_allocation_contract();
  test_draw_role_class_and_relocation_ownership();
  return 0;
}
