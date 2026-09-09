#include "apple_agx_win32_abi.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

typedef struct _VALID_CLEAR_COMMAND {
  APPLE_AGX_WIN32_COMMAND_HEADER Header;
  APPLE_AGX_WIN32_ALLOCATION_REFERENCE Reference;
  APPLE_AGX_WIN32_CLEAR_PAYLOAD Payload;
} VALID_CLEAR_COMMAND;

#define DRAW_REFERENCE_COUNT 9u
#define DRAW_RELOCATION_COUNT 7u
typedef struct _VALID_DRAW_COMMAND {
  APPLE_AGX_WIN32_COMMAND_HEADER Header;
  APPLE_AGX_WIN32_ALLOCATION_REFERENCE References[DRAW_REFERENCE_COUNT];
  APPLE_AGX_WIN32_DRAW_PAYLOAD Payload;
  APPLE_AGX_WIN32_RELOCATION Relocations[DRAW_RELOCATION_COUNT];
} VALID_DRAW_COMMAND;

static void seal(VALID_CLEAR_COMMAND *command) {
  command->Header.ContentHash = 0ULL;
  command->Header.ContentHash =
      AppleAgxWin32CommandHash(command, (APPLE_AGX_U32)sizeof(*command));
}

static VALID_CLEAR_COMMAND valid_command(void) {
  VALID_CLEAR_COMMAND command;
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
  command.Header.PayloadBytes = sizeof(command.Payload);
  command.Reference.AllocationIndex = 1u;
  command.Reference.Access = AppleAgxWin32AccessWrite;
  command.Reference.Role = AppleAgxWin32RoleRenderTarget;
  command.Reference.Offset = 0x10000ULL;
  command.Reference.Bytes = 0x40000ULL;
  command.Payload.StructBytes = sizeof(command.Payload);
  command.Payload.Format = AppleAgxWin32FormatBgra8Unorm;
  command.Payload.Color = 0xff3366ccu;
  command.Payload.SurfaceWidth = 256u;
  command.Payload.SurfaceHeight = 256u;
  command.Payload.SurfacePitch = 1024u;
  command.Payload.Left = 16u;
  command.Payload.Top = 32u;
  command.Payload.Right = 240u;
  command.Payload.Bottom = 224u;
  command.Payload.DestinationReference = 0u;
  seal(&command);
  return command;
}

static APPLE_AGX_WIN32_ABI_RESULT validate(VALID_CLEAR_COMMAND *command) {
  APPLE_AGX_WIN32_COMMAND_VIEW view;
  return AppleAgxWin32CommandValidate(command, sizeof(*command), 7u, 2u,
                                      &view);
}

static void test_literal_valid_clear(void) {
  VALID_CLEAR_COMMAND command = valid_command();
  APPLE_AGX_WIN32_COMMAND_VIEW view;
  assert(sizeof(command.Header) == 48u);
  assert(sizeof(command.Reference) == 32u);
  assert(sizeof(command.Payload) == 48u);
  assert(sizeof(command) == 128u);
  assert(offsetof(APPLE_AGX_WIN32_COMMAND_HEADER, ContentHash) == 40u);
  assert(AppleAgxWin32CommandValidate(&command, sizeof(command), 7u, 2u,
                                      &view) == AppleAgxWin32AbiSuccess);
  assert(view.Header == &command.Header);
  assert(view.References == &command.Reference);
  assert(view.Clear == &command.Payload);
}

static void test_header_and_layout_rejections(void) {
  VALID_CLEAR_COMMAND command;
  APPLE_AGX_WIN32_COMMAND_VIEW view;

#define REJECT(field, value, expected)                                        \
  do {                                                                        \
    command = valid_command();                                                \
    command.field = (value);                                                  \
    seal(&command);                                                           \
    assert(validate(&command) == (expected));                                 \
  } while (0)

  REJECT(Header.Magic, 0u, AppleAgxWin32AbiMagic);
  REJECT(Header.Version, 2u, AppleAgxWin32AbiVersion);
  REJECT(Header.HeaderBytes, 40u, AppleAgxWin32AbiLayout);
  REJECT(Header.TotalBytes, 127u, AppleAgxWin32AbiLayout);
  REJECT(Header.Flags, 1u, AppleAgxWin32AbiFlags);
  REJECT(Header.Generation, 6u, AppleAgxWin32AbiStaleGeneration);
  REJECT(Header.ReferenceCount, 0u, AppleAgxWin32AbiReferenceCount);
  REJECT(Header.ReferenceCount, 17u, AppleAgxWin32AbiReferenceCount);
  REJECT(Header.ReferencesOffset, 52u, AppleAgxWin32AbiLayout);
  REJECT(Header.PayloadOffset, 84u, AppleAgxWin32AbiLayout);
  REJECT(Header.PayloadBytes, 40u, AppleAgxWin32AbiLayout);
  REJECT(Header.Opcode, 99u, AppleAgxWin32AbiOpcode);
#undef REJECT

  command = valid_command();
  command.Header.ContentHash ^= 1ULL;
  assert(validate(&command) == AppleAgxWin32AbiHash);
  assert(AppleAgxWin32CommandValidate(&command, 4097u, 7u, 2u, &view) ==
         AppleAgxWin32AbiArgument);
}

static void test_reference_rejections(void) {
  VALID_CLEAR_COMMAND command;

#define REJECT_REF(field, value, expected)                                    \
  do {                                                                        \
    command = valid_command();                                                \
    command.Reference.field = (value);                                        \
    seal(&command);                                                           \
    assert(validate(&command) == (expected));                                 \
  } while (0)

  REJECT_REF(AllocationIndex, 2u, AppleAgxWin32AbiAllocationIndex);
  REJECT_REF(Access, AppleAgxWin32AccessRead, AppleAgxWin32AbiAccess);
  REJECT_REF(Access, 0x80u, AppleAgxWin32AbiAccess);
  REJECT_REF(Role, AppleAgxWin32RoleTexture, AppleAgxWin32AbiRole);
  REJECT_REF(Reserved, 1u, AppleAgxWin32AbiReserved);
  REJECT_REF(Bytes, 0ULL, AppleAgxWin32AbiRange);
  REJECT_REF(Offset, ~0ULL - 15ULL, AppleAgxWin32AbiRange);
#undef REJECT_REF
}

static void test_clear_payload_rejections(void) {
  VALID_CLEAR_COMMAND command;

#define REJECT_CLEAR(field, value, expected)                                  \
  do {                                                                        \
    command = valid_command();                                                \
    command.Payload.field = (value);                                          \
    seal(&command);                                                           \
    assert(validate(&command) == (expected));                                 \
  } while (0)

  REJECT_CLEAR(StructBytes, 40u, AppleAgxWin32AbiPayload);
  REJECT_CLEAR(Format, 0u, AppleAgxWin32AbiPayload);
  REJECT_CLEAR(SurfaceWidth, 0u, AppleAgxWin32AbiPayload);
  REJECT_CLEAR(SurfaceHeight, 0u, AppleAgxWin32AbiPayload);
  REJECT_CLEAR(SurfacePitch, 1023u, AppleAgxWin32AbiPayload);
  REJECT_CLEAR(Left, 240u, AppleAgxWin32AbiPayload);
  REJECT_CLEAR(Right, 257u, AppleAgxWin32AbiPayload);
  REJECT_CLEAR(Top, 224u, AppleAgxWin32AbiPayload);
  REJECT_CLEAR(Bottom, 257u, AppleAgxWin32AbiPayload);
  REJECT_CLEAR(DestinationReference, 1u, AppleAgxWin32AbiPayload);
  REJECT_CLEAR(Reserved, 1u, AppleAgxWin32AbiReserved);
#undef REJECT_CLEAR
}

static void test_snapshot_is_immutable_after_producer_mutation(void) {
  VALID_CLEAR_COMMAND producer = valid_command();
  VALID_CLEAR_COMMAND snapshot;
  APPLE_AGX_WIN32_COMMAND_VIEW view;
  unsigned long long expected_hash = producer.Header.ContentHash;
  memcpy(&snapshot, &producer, sizeof(snapshot));
  producer.Payload.Color = 0u;
  producer.Reference.Offset = 0u;
  assert(AppleAgxWin32CommandValidate(&snapshot, sizeof(snapshot), 7u, 2u,
                                      &view) == AppleAgxWin32AbiSuccess);
  assert(view.Header->ContentHash == expected_hash);
  assert(view.Clear->Color == 0xff3366ccu);
  assert(view.References[0].Offset == 0x10000ULL);
}

static void seal_draw(VALID_DRAW_COMMAND *command) {
  command->Header.ContentHash = 0ULL;
  command->Header.ContentHash = AppleAgxWin32CommandHash(
      command, (APPLE_AGX_U32)sizeof(*command));
}

static void draw_reference(VALID_DRAW_COMMAND *command, unsigned index,
                           unsigned role, unsigned access,
                           unsigned allocation, unsigned long long bytes) {
  command->References[index].AllocationIndex = allocation;
  command->References[index].Role = role;
  command->References[index].Access = access;
  command->References[index].Bytes = bytes;
}

static VALID_DRAW_COMMAND valid_draw(void) {
  VALID_DRAW_COMMAND command;
  unsigned index;
  memset(&command, 0, sizeof(command));
  command.Header.Magic = APPLE_AGX_WIN32_COMMAND_MAGIC;
  command.Header.Version = APPLE_AGX_WIN32_COMMAND_VERSION;
  command.Header.HeaderBytes = sizeof(command.Header);
  command.Header.TotalBytes = sizeof(command);
  command.Header.Opcode = AppleAgxWin32OpcodeDraw;
  command.Header.Generation = 7u;
  command.Header.ReferenceCount = DRAW_REFERENCE_COUNT;
  command.Header.ReferencesOffset = sizeof(command.Header);
  command.Header.PayloadOffset =
      sizeof(command.Header) + sizeof(command.References);
  command.Header.PayloadBytes =
      sizeof(command.Payload) + sizeof(command.Relocations);
  draw_reference(&command, 0u, AppleAgxWin32RoleRenderTarget,
                 AppleAgxWin32AccessWrite, 0u, 0x1000000ULL);
  draw_reference(&command, 1u, AppleAgxWin32RoleVertex,
                 AppleAgxWin32AccessRead, 1u, 0x4000ULL);
  draw_reference(&command, 2u, AppleAgxWin32RoleShader,
                 AppleAgxWin32AccessRead | AppleAgxWin32AccessExecute,
                 2u, 0x4000ULL);
  draw_reference(&command, 3u, AppleAgxWin32RoleShader,
                 AppleAgxWin32AccessRead | AppleAgxWin32AccessExecute,
                 3u, 0x4000ULL);
  draw_reference(&command, 4u, AppleAgxWin32RoleUscPipeline,
                 AppleAgxWin32AccessRead, 4u, 0x4000ULL);
  draw_reference(&command, 5u, AppleAgxWin32RoleDescriptor,
                 AppleAgxWin32AccessRead, 5u, 0x4000ULL);
  draw_reference(&command, 6u, AppleAgxWin32RoleScissor,
                 AppleAgxWin32AccessRead, 6u, 0x4000ULL);
  draw_reference(&command, 7u, AppleAgxWin32RoleDepthBias,
                 AppleAgxWin32AccessRead, 7u, 0x4000ULL);
  draw_reference(&command, 8u, AppleAgxWin32RoleEncoder,
                 AppleAgxWin32AccessRead, 8u, 0x8000ULL);
  command.Payload.StructBytes = sizeof(command.Payload);
  command.Payload.Format = AppleAgxWin32FormatBgra8Unorm;
  command.Payload.SurfaceWidth = 2560u;
  command.Payload.SurfaceHeight = 1600u;
  command.Payload.SurfacePitch = 10240u;
  command.Payload.Topology = AppleAgxWin32TopologyTriangleList;
  command.Payload.VertexCount = 3u;
  command.Payload.InstanceCount = 1u;
  command.Payload.DestinationReference = 0u;
  command.Payload.VertexReference = 1u;
  command.Payload.IndexReference = APPLE_AGX_WIN32_OPTIONAL_REFERENCE;
  command.Payload.ConstantReference = APPLE_AGX_WIN32_OPTIONAL_REFERENCE;
  command.Payload.TextureReference = APPLE_AGX_WIN32_OPTIONAL_REFERENCE;
  command.Payload.VertexShaderReference = 2u;
  command.Payload.FragmentShaderReference = 3u;
  command.Payload.VertexRodataReference = APPLE_AGX_WIN32_OPTIONAL_REFERENCE;
  command.Payload.FragmentRodataReference = APPLE_AGX_WIN32_OPTIONAL_REFERENCE;
  command.Payload.UscPipelineReference = 4u;
  command.Payload.DescriptorReference = 5u;
  command.Payload.ScissorReference = 6u;
  command.Payload.DepthBiasReference = 7u;
  command.Payload.EncoderReference = 8u;
  command.Payload.RelocationsOffset = sizeof(command.Payload);
  command.Payload.RelocationCount = DRAW_RELOCATION_COUNT;
  for (index = 0u; index < DRAW_RELOCATION_COUNT; ++index) {
    command.Relocations[index].WidthBytes = 8u;
    command.Relocations[index].DestinationOffset = index * 8u;
  }
  command.Relocations[0].Kind = AppleAgxWin32RelocationEncoderAddress;
  command.Relocations[0].DestinationReference = 8u;
  command.Relocations[0].TargetReference = 0u;
  command.Relocations[1].Kind = AppleAgxWin32RelocationEncoderAddress;
  command.Relocations[1].DestinationReference = 8u;
  command.Relocations[1].TargetReference = 1u;
  command.Relocations[2].Kind = AppleAgxWin32RelocationVdmPipelineOffset32;
  command.Relocations[2].WidthBytes = 4u;
  command.Relocations[2].DestinationReference = 8u;
  command.Relocations[2].TargetReference = 4u;
  command.Relocations[3].Kind = AppleAgxWin32RelocationUscShaderOffset32;
  command.Relocations[3].WidthBytes = 6u;
  command.Relocations[3].DestinationReference = 4u;
  command.Relocations[3].TargetReference = 2u;
  command.Relocations[4].Kind = AppleAgxWin32RelocationUscShaderOffset32;
  command.Relocations[4].WidthBytes = 6u;
  command.Relocations[4].DestinationReference = 4u;
  command.Relocations[4].TargetReference = 3u;
  command.Relocations[5].Kind = AppleAgxWin32RelocationUscBufferAddress40;
  command.Relocations[5].DestinationReference = 4u;
  command.Relocations[5].TargetReference = 5u;
  command.Relocations[6].Kind = AppleAgxWin32RelocationPppStateAddress40;
  command.Relocations[6].DestinationReference = 8u;
  command.Relocations[6].TargetReference = 8u;
  command.Relocations[6].TargetOffset = 0x100u;
  seal_draw(&command);
  return command;
}

static APPLE_AGX_WIN32_ABI_RESULT validate_draw(
    VALID_DRAW_COMMAND *command, APPLE_AGX_WIN32_COMMAND_VIEW *view) {
  return AppleAgxWin32CommandValidate(
      command, sizeof(*command), 7u, DRAW_REFERENCE_COUNT, view);
}

static void test_valid_draw_graph(void) {
  VALID_DRAW_COMMAND command = valid_draw();
  APPLE_AGX_WIN32_COMMAND_VIEW view;
  assert(sizeof(APPLE_AGX_WIN32_DRAW_PAYLOAD) == 128u);
  assert(sizeof(APPLE_AGX_WIN32_RELOCATION) == 40u);
  assert(validate_draw(&command, &view) == AppleAgxWin32AbiSuccess);
  assert(view.Draw == &command.Payload);
  assert(view.Relocations == command.Relocations);
  assert(view.Clear == NULL);
}

static void test_draw_expected_foreground_contract(void) {
  VALID_DRAW_COMMAND command = valid_draw();
  APPLE_AGX_WIN32_COMMAND_VIEW view;
  command.Payload.Flags = APPLE_AGX_WIN32_DRAW_FLAG_EXPECTED_FOREGROUND;
  command.Payload.ExpectedForegroundColor = 0x80808080u;
  seal_draw(&command);
  assert(validate_draw(&command, &view) == AppleAgxWin32AbiSuccess);
  assert(view.Draw->ExpectedForegroundColor == 0x80808080u);

  command = valid_draw();
  command.Payload.Flags = APPLE_AGX_WIN32_DRAW_FLAG_EXPECTED_FOREGROUND;
  seal_draw(&command);
  assert(validate_draw(&command, &view) == AppleAgxWin32AbiPayload);
  command = valid_draw();
  command.Payload.ExpectedForegroundColor = 0x80808080u;
  seal_draw(&command);
  assert(validate_draw(&command, &view) == AppleAgxWin32AbiFlags);
  command = valid_draw();
  command.Payload.Flags = APPLE_AGX_WIN32_DRAW_FLAG_EXPECTED_FOREGROUND;
  command.Payload.ExpectedForegroundColor = 0xa5a5a5a5u;
  seal_draw(&command);
  assert(validate_draw(&command, &view) == AppleAgxWin32AbiPayload);
}

static void test_vertex_descriptor_relocation(void) {
  VALID_DRAW_COMMAND command = valid_draw();
  APPLE_AGX_WIN32_COMMAND_VIEW view;
  command.Relocations[0].Kind = AppleAgxWin32RelocationDescriptorAddress;
  command.Relocations[0].DestinationReference = 5u;
  command.Relocations[0].TargetReference = 1u;
  command.Relocations[0].DestinationOffset = 0u;
  command.Relocations[0].TargetOffset = 0u;
  seal_draw(&command);
  assert(validate_draw(&command, &view) == AppleAgxWin32AbiSuccess);
}

static void test_draw_graph_rejections(void) {
  VALID_DRAW_COMMAND command;
  APPLE_AGX_WIN32_COMMAND_VIEW view;

#define REJECT_DRAW(field, value, expected)                                  \
  do {                                                                       \
    command = valid_draw();                                                  \
    command.field = (value);                                                 \
    seal_draw(&command);                                                     \
    assert(validate_draw(&command, &view) == (expected));                    \
  } while (0)

  REJECT_DRAW(Payload.StructBytes, 120u, AppleAgxWin32AbiPayload);
  REJECT_DRAW(Payload.SurfacePitch, 10239u, AppleAgxWin32AbiPayload);
  REJECT_DRAW(Payload.VertexCount, 4u, AppleAgxWin32AbiPayload);
  REJECT_DRAW(Payload.InstanceCount, 0u, AppleAgxWin32AbiPayload);
  REJECT_DRAW(Payload.IndexReference, 1u, AppleAgxWin32AbiRole);
  REJECT_DRAW(Payload.VertexShaderReference, 1u, AppleAgxWin32AbiRole);
  REJECT_DRAW(Payload.Flags, 2u, AppleAgxWin32AbiFlags);
  REJECT_DRAW(Payload.Reserved[3], 1u, AppleAgxWin32AbiReserved);
  REJECT_DRAW(Payload.RelocationCount, 0u, AppleAgxWin32AbiRelocation);
  REJECT_DRAW(Payload.RelocationsOffset, 120u, AppleAgxWin32AbiLayout);
  REJECT_DRAW(Relocations[0].Kind, 99u, AppleAgxWin32AbiRelocation);
  REJECT_DRAW(Relocations[0].WidthBytes, 4u, AppleAgxWin32AbiRelocation);
  REJECT_DRAW(Relocations[0].Reserved, 1u, AppleAgxWin32AbiReserved);
  REJECT_DRAW(Relocations[0].DestinationReference, 4u,
              AppleAgxWin32AbiRelocation);
  REJECT_DRAW(Relocations[0].TargetReference, 8u,
              AppleAgxWin32AbiRelocation);
  REJECT_DRAW(Relocations[0].DestinationOffset, 0x8000ULL,
              AppleAgxWin32AbiRange);
  REJECT_DRAW(Relocations[0].TargetOffset, 0x1000000ULL,
              AppleAgxWin32AbiRange);
  REJECT_DRAW(Relocations[0].AddressFlags, 1ULL,
              AppleAgxWin32AbiRelocation);
  REJECT_DRAW(Relocations[6].TargetReference, 0u,
              AppleAgxWin32AbiRelocation);
  REJECT_DRAW(Relocations[6].DestinationOffset, 51u,
              AppleAgxWin32AbiRange);
  REJECT_DRAW(Relocations[6].TargetOffset, 0x101u,
              AppleAgxWin32AbiRange);
  REJECT_DRAW(Relocations[5].DestinationOffset, 42u,
              AppleAgxWin32AbiRange);
  command = valid_draw();
  command.Relocations[1].DestinationOffset =
      command.Relocations[0].DestinationOffset;
  seal_draw(&command);
  assert(validate_draw(&command, &view) == AppleAgxWin32AbiRelocation);
  command = valid_draw();
  command.References[8].Role = AppleAgxWin32RoleDescriptor;
  seal_draw(&command);
  assert(validate_draw(&command, &view) == AppleAgxWin32AbiRole);
  command = valid_draw();
  command.References[8].Access = AppleAgxWin32AccessWrite;
  seal_draw(&command);
  assert(validate_draw(&command, &view) == AppleAgxWin32AbiAccess);
  command = valid_draw();
  command.Header.ReferenceCount++;
  seal_draw(&command);
  assert(validate_draw(&command, &view) != AppleAgxWin32AbiSuccess);
  command = valid_draw();
  command.Payload.FragmentShaderReference = 2u;
  command.Relocations[4].TargetReference = 2u;
  seal_draw(&command);
  assert(validate_draw(&command, &view) == AppleAgxWin32AbiReachability);
#undef REJECT_DRAW
}

int main(void) {
  test_literal_valid_clear();
  test_header_and_layout_rejections();
  test_reference_rejections();
  test_clear_payload_rejections();
  test_snapshot_is_immutable_after_producer_mutation();
  test_valid_draw_graph();
  test_draw_expected_foreground_contract();
  test_vertex_descriptor_relocation();
  test_draw_graph_rejections();
  return 0;
}
