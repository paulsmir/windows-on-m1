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

static void test_v3_general_ppp_reference_class(void) {
  APPLE_AGX_WIN32_COMMAND_HEADER header = {0};
  APPLE_AGX_WIN32_ALLOCATION_REFERENCE reference = {0};
  APPLE_AGX_WIN32_DRAW_PAYLOAD draw = {0};
  APPLE_AGX_WIN32_RELOCATION relocation = {0};
  APPLE_AGX_WIN32_COMMAND_VIEW view = {&header, &reference, NULL, &draw, &relocation, NULL};
  TEST_LOOKUP lookup = make_lookup();
  ADMISSION_WIN32_ALLOCATION_FACT facts[1];
  header.Opcode = AppleAgxWin32OpcodeDraw;
  header.Version = APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_USC;
  header.Generation = 7u;
  header.ReferenceCount = 1u;
  reference.AllocationIndex = 1u;
  reference.Access = AppleAgxWin32AccessRead;
  reference.Role = AppleAgxWin32RolePppState;
  reference.Bytes = 4u;
  lookup.Count = 1u;
  lookup.Allocations[0] = lookup.Allocations[1];
  lookup.Allocations[0].Index = 1u;
  lookup.Allocations[0].Fact.ClassId = AgxWin32BufferClassGeneral;
  lookup.Allocations[0].Fact.Flags = AppleAgxWin32BufferCpuWrite |
                                     AppleAgxWin32BufferGpuRead;
  assert(AdmissionWin32ValidateReferences(&view,7u,lookup_allocation,&lookup,
                                          facts,1u)==AdmissionWin32TransportSuccess);
  lookup.Allocations[0].Fact.ClassId = AgxWin32BufferClassEncoder;
  assert(AdmissionWin32ValidateReferences(&view,7u,lookup_allocation,&lookup,
                                          facts,1u)==AdmissionWin32TransportClass);
  lookup.Allocations[0].Fact.ClassId = AgxWin32BufferClassGeneral;
  reference.Access = AppleAgxWin32AccessWrite;
  assert(AdmissionWin32ValidateReferences(&view,7u,lookup_allocation,&lookup,
                                          facts,1u)==AdmissionWin32TransportClass);
  reference.Access = AppleAgxWin32AccessExecute;
  assert(AdmissionWin32ValidateReferences(&view,7u,lookup_allocation,&lookup,
                                          facts,1u)==AdmissionWin32TransportClass);
  reference.Access = AppleAgxWin32AccessRead;
  header.Version = APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_PIPELINES;
  assert(AdmissionWin32ValidateReferences(&view,7u,lookup_allocation,&lookup,
                                          facts,1u)==AdmissionWin32TransportClass);
}

static void test_v4_exact_shader_source_span(void) {
  APPLE_AGX_WIN32_COMMAND_HEADER header = {0};
  APPLE_AGX_WIN32_ALLOCATION_REFERENCE reference = {0};
  APPLE_AGX_WIN32_DRAW_PAYLOAD draw = {0};
  APPLE_AGX_WIN32_RELOCATION relocation = {0};
  APPLE_AGX_WIN32_COMMAND_VIEW view = {&header,&reference,NULL,&draw,&relocation,NULL};
  TEST_LOOKUP lookup = make_lookup();
  ADMISSION_WIN32_ALLOCATION_FACT facts[1];
  header.Opcode=AppleAgxWin32OpcodeDraw;
  header.Version=APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH;
  header.Generation=7u; header.ReferenceCount=1u;
  reference.AllocationIndex=1u;
  reference.Access=AppleAgxWin32AccessRead|AppleAgxWin32AccessExecute;
  reference.Role=AppleAgxWin32RoleShader; reference.Bytes=50u;
  lookup.Count=1u; lookup.Allocations[0]=lookup.Allocations[1];
  lookup.Allocations[0].Index=1u;
  lookup.Allocations[0].Fact.ClassId=AgxWin32BufferClassShader;
  lookup.Allocations[0].Fact.Flags=AppleAgxWin32BufferGpuRead;
  assert(AdmissionWin32ValidateReferences(&view,7u,lookup_allocation,&lookup,
      facts,1u)==AdmissionWin32TransportSuccess);
  reference.Bytes=34u;
  assert(AdmissionWin32ValidateReferences(&view,7u,lookup_allocation,&lookup,
      facts,1u)==AdmissionWin32TransportSuccess);
  reference.Bytes=51u;
  assert(AdmissionWin32ValidateReferences(&view,7u,lookup_allocation,&lookup,
      facts,1u)==AdmissionWin32TransportAlignment);
  reference.Bytes=50u; header.Version=APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_USC;
  assert(AdmissionWin32ValidateReferences(&view,7u,lookup_allocation,&lookup,
      facts,1u)==AdmissionWin32TransportAlignment);
  header.Version=APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_BATCH;
  reference.Offset=2u;
  assert(AdmissionWin32ValidateReferences(&view,7u,lookup_allocation,&lookup,
      facts,1u)==AdmissionWin32TransportAlignment);
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
  assert(!AdmissionWin32AllocationUsesGpuVa(0u));
  assert(AdmissionWin32AllocationUsesGpuVa(AgxWin32BufferClassGeneral));
  assert(AdmissionWin32AllocationUsesGpuVa(AgxWin32BufferClassShader));
  assert(AdmissionWin32AllocationUsesGpuVa(AgxWin32BufferClassEncoder));
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

/* Catches treating native batch.pool source bytes as Encoder-class bytes. The
 * source remains read-only; this does not authorize a General destination. */
static void test_v3_general_table_sources_are_narrowly_accepted(void) {
  static const APPLE_AGX_U32 roles[] = {
      AppleAgxWin32RoleDescriptor, AppleAgxWin32RoleScissor,
      AppleAgxWin32RoleDepthBias};
  for (unsigned role_index = 0u; role_index < 3u; ++role_index) {
    APPLE_AGX_WIN32_COMMAND_HEADER header = {0};
    APPLE_AGX_WIN32_ALLOCATION_REFERENCE references[2] = {{0}};
    APPLE_AGX_WIN32_DRAW_PAYLOAD draw = {0};
    APPLE_AGX_WIN32_RELOCATION relocation = {0};
    APPLE_AGX_WIN32_COMMAND_VIEW view = {0};
    ADMISSION_WIN32_ALLOCATION_FACT facts[2] = {{0}};
    TEST_LOOKUP lookup = {0};
    header.Opcode = AppleAgxWin32OpcodeDraw;
    header.Version = APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_USC;
    header.Generation = 7u;
    header.ReferenceCount = 2u;
    view.Header = &header;
    view.References = references;
    view.Draw = &draw;
    view.Relocations = &relocation;
    references[0] = (APPLE_AGX_WIN32_ALLOCATION_REFERENCE){
        0u, AppleAgxWin32AccessWrite, AppleAgxWin32RoleRenderTarget,
        0u, 0u, 0x4000ULL};
    references[1] = (APPLE_AGX_WIN32_ALLOCATION_REFERENCE){
        1u, AppleAgxWin32AccessRead, roles[role_index],
        0u, 0x4000ULL, 0x4000ULL};
    lookup.ExpectedOwner = 0x1111ULL;
    lookup.Count = 2u;
    for (unsigned i = 0u; i < 2u; ++i) {
      lookup.Allocations[i].Index = i;
      lookup.Allocations[i].Owner = lookup.ExpectedOwner;
      lookup.Allocations[i].Fact.AllocationToken = 0xa100ULL + i;
      lookup.Allocations[i].Fact.Bytes = 0x10000ULL;
      lookup.Allocations[i].Fact.Generation = 7u;
      lookup.Allocations[i].Fact.Writable = 1u;
      lookup.Allocations[i].Fact.Flags = AppleAgxWin32BufferCpuWrite |
                                          AppleAgxWin32BufferGpuRead |
                                          AppleAgxWin32BufferGpuWrite;
    }
    lookup.Allocations[0].Fact.ClassId = 0u;
    lookup.Allocations[1].Fact.ClassId = AgxWin32BufferClassGeneral;
    ADMISSION_WIN32_TRANSPORT_RESULT result = AdmissionWin32ValidateReferences(
        &view, 7u, lookup_allocation, &lookup, facts, 2u);
    assert(result == AdmissionWin32TransportSuccess);
    /* Legacy command versions and non-read source uses keep failing closed. */
    for (APPLE_AGX_U16 version = APPLE_AGX_WIN32_COMMAND_VERSION;
         version <= APPLE_AGX_WIN32_COMMAND_VERSION_TEXTURED_BATCH; ++version) {
      if (version == APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_USC ||
          APPLE_AGX_WIN32_COMMAND_IS_NATIVE(version))
        continue;
      header.Version = version;
      assert(AdmissionWin32ValidateReferences(
          &view, 7u, lookup_allocation, &lookup, facts, 2u) ==
          AdmissionWin32TransportClass);
    }
    header.Version = APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_USC;
    lookup.Allocations[1].Fact.ClassId = AgxWin32BufferClassEncoder;
    assert(AdmissionWin32ValidateReferences(
        &view, 7u, lookup_allocation, &lookup, facts, 2u) ==
        AdmissionWin32TransportSuccess);
    lookup.Allocations[1].Fact.ClassId = AgxWin32BufferClassGeneral;
    references[1].Access = AppleAgxWin32AccessRead | AppleAgxWin32AccessWrite;
    assert(AdmissionWin32ValidateReferences(
        &view, 7u, lookup_allocation, &lookup, facts, 2u) ==
        AdmissionWin32TransportClass);
    references[1].Access = AppleAgxWin32AccessRead;
    lookup.Allocations[1].Fact.Flags = AppleAgxWin32BufferCpuWrite;
    assert(AdmissionWin32ValidateReferences(
        &view, 7u, lookup_allocation, &lookup, facts, 2u) ==
        AdmissionWin32TransportAccess);
    lookup.Allocations[1].Fact.Flags = AppleAgxWin32BufferCpuWrite |
                                          AppleAgxWin32BufferGpuRead;
    references[1].Role = AppleAgxWin32RoleUscPipeline;
    assert(AdmissionWin32ValidateReferences(
        &view, 7u, lookup_allocation, &lookup, facts, 2u) ==
        AdmissionWin32TransportClass);
    references[1].Role = AppleAgxWin32RoleShader;
    references[1].Access = AppleAgxWin32AccessRead | AppleAgxWin32AccessExecute;
    assert(AdmissionWin32ValidateReferences(
        &view, 7u, lookup_allocation, &lookup, facts, 2u) ==
        AdmissionWin32TransportClass);
  }
}

/* Native USC records are packed 8+4+6+4+8+8 = 38 bytes. Compiler rodata is
 * 16-bit data whose final range may be two bytes; neither needs fake padding. */
static void test_v3_exact_native_source_spans_are_not_word_rounded(void) {
  APPLE_AGX_WIN32_COMMAND_HEADER header = {0};
  APPLE_AGX_WIN32_ALLOCATION_REFERENCE references[4] = {{0}};
  APPLE_AGX_WIN32_DRAW_PAYLOAD draw = {0};
  APPLE_AGX_WIN32_RELOCATION relocation = {0};
  APPLE_AGX_WIN32_COMMAND_VIEW view = {0};
  ADMISSION_WIN32_ALLOCATION_FACT facts[4] = {{0}};
  TEST_LOOKUP lookup = {0};
  header.Opcode = AppleAgxWin32OpcodeDraw;
  header.Version = APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_USC;
  header.Generation = 7u;
  header.ReferenceCount = 4u;
  view.Header = &header; view.References = references;
  view.Draw = &draw; view.Relocations = &relocation;
  references[0] = (APPLE_AGX_WIN32_ALLOCATION_REFERENCE){
      0u, AppleAgxWin32AccessWrite, AppleAgxWin32RoleRenderTarget,
      0u, 0u, 0x4000ULL};
  references[1] = (APPLE_AGX_WIN32_ALLOCATION_REFERENCE){
      1u, AppleAgxWin32AccessRead, AppleAgxWin32RoleUscPipeline,
      0u, 0x40ULL, 38ULL};
  references[2] = (APPLE_AGX_WIN32_ALLOCATION_REFERENCE){
      2u, AppleAgxWin32AccessRead, AppleAgxWin32RoleShaderRodata,
      0u, 4ULL, 2ULL};
  references[3] = (APPLE_AGX_WIN32_ALLOCATION_REFERENCE){
      3u, AppleAgxWin32AccessRead, AppleAgxWin32RoleConstant,
      0u, 8ULL, 6ULL};
  lookup.ExpectedOwner = 0x1111ULL; lookup.Count = 4u;
  for (unsigned i = 0u; i < 4u; ++i) {
    lookup.Allocations[i].Index = i;
    lookup.Allocations[i].Owner = lookup.ExpectedOwner;
    lookup.Allocations[i].Fact.AllocationToken = 0xa200ULL + i;
    lookup.Allocations[i].Fact.Bytes = 0x10000ULL;
    lookup.Allocations[i].Fact.Generation = 7u;
    lookup.Allocations[i].Fact.Writable = 1u;
    lookup.Allocations[i].Fact.Flags = AppleAgxWin32BufferCpuWrite |
                                        AppleAgxWin32BufferGpuRead;
  }
  lookup.Allocations[0].Fact.ClassId = 0u;
  lookup.Allocations[1].Fact.ClassId = AgxWin32BufferClassEncoder;
  lookup.Allocations[2].Fact.ClassId = AgxWin32BufferClassShader;
  lookup.Allocations[3].Fact.ClassId = AgxWin32BufferClassGeneral;
  assert(AdmissionWin32ValidateReferences(
      &view, 7u, lookup_allocation, &lookup, facts, 4u) ==
      AdmissionWin32TransportSuccess);
  header.Version = APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_PIPELINES;
  assert(AdmissionWin32ValidateReferences(
      &view, 7u, lookup_allocation, &lookup, facts, 4u) ==
      AdmissionWin32TransportAlignment);
  header.Version = APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_USC;
  references[1].Offset = 0x42ULL;
  assert(AdmissionWin32ValidateReferences(
      &view, 7u, lookup_allocation, &lookup, facts, 4u) ==
      AdmissionWin32TransportAlignment);
  references[1].Offset = 0x40ULL;
  references[1].Role = AppleAgxWin32RoleDescriptor;
  assert(AdmissionWin32ValidateReferences(
      &view, 7u, lookup_allocation, &lookup, facts, 4u) ==
      AdmissionWin32TransportAlignment);
  references[1].Role = AppleAgxWin32RoleUscPipeline;
  references[1].Access = AppleAgxWin32AccessRead | AppleAgxWin32AccessWrite;
  assert(AdmissionWin32ValidateReferences(
      &view, 7u, lookup_allocation, &lookup, facts, 4u) ==
      AdmissionWin32TransportAlignment);
}


/* agx_flush_render writes a 5-byte stop plus 64 zero bytes, without rounding
 * the source range. Only read-only v3 Encoder sources may end mid-word. */
static void test_v3_encoder_native_termination_span(void) {
  APPLE_AGX_WIN32_COMMAND_HEADER header = {0};
  APPLE_AGX_WIN32_ALLOCATION_REFERENCE reference = {0};
  APPLE_AGX_WIN32_DRAW_PAYLOAD draw = {0};
  APPLE_AGX_WIN32_RELOCATION relocation = {0};
  APPLE_AGX_WIN32_COMMAND_VIEW view = {&header,&reference,NULL,&draw,&relocation,NULL};
  ADMISSION_WIN32_ALLOCATION_FACT fact;
  TEST_LOOKUP lookup = make_lookup();
  header.Opcode=AppleAgxWin32OpcodeDraw;
  header.Generation=7; header.ReferenceCount=1;
  header.Version=APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_USC;
  reference.AllocationIndex=1; reference.Access=AppleAgxWin32AccessRead;
  reference.Role=AppleAgxWin32RoleEncoder; reference.Offset=64;
  reference.Bytes=32+69;
  lookup.Count=1; lookup.Allocations[0]=lookup.Allocations[1];
  lookup.Allocations[0].Index=1;
  lookup.Allocations[0].Fact.ClassId=AgxWin32BufferClassEncoder;
  lookup.Allocations[0].Fact.Flags=AppleAgxWin32BufferCpuWrite|AppleAgxWin32BufferGpuRead;
  assert(AdmissionWin32ValidateReferences(&view,7,lookup_allocation,&lookup,&fact,1)==AdmissionWin32TransportSuccess);
  for(unsigned version=1;version<=2;++version) {
    header.Version=(APPLE_AGX_U16)version;
    assert(AdmissionWin32ValidateReferences(&view,7,lookup_allocation,&lookup,&fact,1)==AdmissionWin32TransportAlignment);
  }
  header.Version=APPLE_AGX_WIN32_COMMAND_VERSION_NATIVE_USC;
  for(unsigned length=1;length<=3;++length) {
    reference.Bytes=length;
    assert(AdmissionWin32ValidateReferences(&view,7,lookup_allocation,&lookup,&fact,1)==AdmissionWin32TransportAlignment);
  }
  reference.Bytes=70;
  assert(AdmissionWin32ValidateReferences(&view,7,lookup_allocation,&lookup,&fact,1)==AdmissionWin32TransportAlignment);
  reference.Bytes=32+69;
  draw.EncoderReference=1;
  assert(AdmissionWin32ValidateReferences(&view,7,lookup_allocation,&lookup,&fact,1)==AdmissionWin32TransportAlignment);
  draw.EncoderReference=0;
  {
    APPLE_AGX_WIN32_ALLOCATION_REFERENCE refs[2]={reference,reference};
    ADMISSION_WIN32_ALLOCATION_FACT pair[2];
    refs[1].Offset=256;
    header.ReferenceCount=2; view.References=refs; draw.EncoderReference=1;
    assert(AdmissionWin32ValidateReferences(&view,7,lookup_allocation,&lookup,pair,2)==AdmissionWin32TransportAlignment);
    refs[0].Bytes=4;
    assert(AdmissionWin32ValidateReferences(&view,7,lookup_allocation,&lookup,pair,2)==AdmissionWin32TransportSuccess);
    header.ReferenceCount=1; view.References=&reference; draw.EncoderReference=0;
  }
  reference.Offset=65;
  assert(AdmissionWin32ValidateReferences(&view,7,lookup_allocation,&lookup,&fact,1)==AdmissionWin32TransportAlignment);
  reference.Offset=64; reference.Access=AppleAgxWin32AccessRead|AppleAgxWin32AccessWrite;
  assert(AdmissionWin32ValidateReferences(&view,7,lookup_allocation,&lookup,&fact,1)==AdmissionWin32TransportAlignment);
  reference.Access=AppleAgxWin32AccessRead;
  lookup.Allocations[0].Fact.ClassId=AgxWin32BufferClassGeneral;
  assert(AdmissionWin32ValidateReferences(&view,7,lookup_allocation,&lookup,&fact,1)==AdmissionWin32TransportClass);
}

int main(void) {
  test_v3_encoder_native_termination_span();
  test_valid_noncontiguous_index_and_range();
  test_owner_generation_and_access_rejections();
  test_alignment_capacity_and_lookup_rejections();
  test_v3_general_ppp_reference_class();
  test_v4_exact_shader_source_span();
  test_failed_validation_does_not_publish_partial_facts();
  test_overlapping_writable_ranges_are_rejected();
  test_context_generation_contract();
  test_class_allocation_contract();
  test_draw_role_class_and_relocation_ownership();
  test_v3_general_table_sources_are_narrowly_accepted();
  test_v3_exact_native_source_spans_are_not_word_rounded();
  return 0;
}
