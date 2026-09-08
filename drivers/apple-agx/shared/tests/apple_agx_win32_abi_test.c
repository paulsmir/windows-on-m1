#include "apple_agx_win32_abi.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

typedef struct _VALID_CLEAR_COMMAND {
  APPLE_AGX_WIN32_COMMAND_HEADER Header;
  APPLE_AGX_WIN32_ALLOCATION_REFERENCE Reference;
  APPLE_AGX_WIN32_CLEAR_PAYLOAD Payload;
} VALID_CLEAR_COMMAND;

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

int main(void) {
  test_literal_valid_clear();
  test_header_and_layout_rejections();
  test_reference_rejections();
  test_clear_payload_rejections();
  test_snapshot_is_immutable_after_producer_mutation();
  return 0;
}
