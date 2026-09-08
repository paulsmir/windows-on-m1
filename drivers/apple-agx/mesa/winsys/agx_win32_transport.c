#include "agx_win32_transport.h"

#include <string.h>

#define AGX_WIN32_BUFFER_FLAGS_ALLOWED 0x1fu
#define AGX_WIN32_CPU_ACCESS_ALLOWED                                    \
  ((APPLE_AGX_U32)AppleAgxWin32BufferCpuRead |                         \
   (APPLE_AGX_U32)AppleAgxWin32BufferCpuWrite)

typedef struct _AGX_WIN32_CLEAR_COMMAND {
  APPLE_AGX_WIN32_COMMAND_HEADER Header;
  APPLE_AGX_WIN32_ALLOCATION_REFERENCE Reference;
  APPLE_AGX_WIN32_CLEAR_PAYLOAD Clear;
} AGX_WIN32_CLEAR_COMMAND;

APPLE_AGX_WIN32_ABI_RESULT AgxWin32TransportBuildClear(
    const AGX_WIN32_CLEAR_REQUEST *Request, void *CommandBuffer,
    APPLE_AGX_U32 CommandCapacity, APPLE_AGX_U32 *CommandBytes) {
  AGX_WIN32_CLEAR_COMMAND command;
  APPLE_AGX_WIN32_COMMAND_VIEW view;
  APPLE_AGX_WIN32_ABI_RESULT result;
  if (Request == NULL || CommandBuffer == NULL || CommandBytes == NULL ||
      CommandCapacity < sizeof(command) ||
      Request->AllocationIndex == ~(APPLE_AGX_U32)0u)
    return AppleAgxWin32AbiArgument;
  if (Request->Generation == 0u)
    return AppleAgxWin32AbiStaleGeneration;
  if (Request->AllocationBytes == 0ULL ||
      Request->AllocationOffset >
          ~(APPLE_AGX_U64)0ULL - Request->AllocationBytes)
    return AppleAgxWin32AbiRange;

  memset(&command, 0, sizeof(command));
  command.Header.Magic = APPLE_AGX_WIN32_COMMAND_MAGIC;
  command.Header.Version = APPLE_AGX_WIN32_COMMAND_VERSION;
  command.Header.HeaderBytes = sizeof(command.Header);
  command.Header.TotalBytes = sizeof(command);
  command.Header.Opcode = AppleAgxWin32OpcodeClear;
  command.Header.Generation = Request->Generation;
  command.Header.ReferenceCount = 1u;
  command.Header.ReferencesOffset = sizeof(command.Header);
  command.Header.PayloadOffset =
      sizeof(command.Header) + sizeof(command.Reference);
  command.Header.PayloadBytes = sizeof(command.Clear);
  command.Reference.AllocationIndex = Request->AllocationIndex;
  command.Reference.Access = AppleAgxWin32AccessWrite;
  command.Reference.Role = AppleAgxWin32RoleRenderTarget;
  command.Reference.Offset = Request->AllocationOffset;
  command.Reference.Bytes = Request->AllocationBytes;
  command.Clear.StructBytes = sizeof(command.Clear);
  command.Clear.Format = Request->Format;
  command.Clear.Color = Request->Color;
  command.Clear.SurfaceWidth = Request->SurfaceWidth;
  command.Clear.SurfaceHeight = Request->SurfaceHeight;
  command.Clear.SurfacePitch = Request->SurfacePitch;
  command.Clear.Left = Request->Left;
  command.Clear.Top = Request->Top;
  command.Clear.Right = Request->Right;
  command.Clear.Bottom = Request->Bottom;
  command.Clear.DestinationReference = 0u;
  command.Header.ContentHash =
      AppleAgxWin32CommandHash(&command, sizeof(command));
  result = AppleAgxWin32CommandValidate(
      &command, sizeof(command), Request->Generation,
      Request->AllocationIndex + 1u, &view);
  if (result != AppleAgxWin32AbiSuccess)
    return result;
  memcpy(CommandBuffer, &command, sizeof(command));
  *CommandBytes = sizeof(command);
  return AppleAgxWin32AbiSuccess;
}

APPLE_AGX_WIN32_ABI_RESULT AgxWin32TransportBuildDraw(
    const AGX_WIN32_DRAW_REQUEST *Request, void *CommandBuffer,
    APPLE_AGX_U32 CommandCapacity, APPLE_AGX_U32 *CommandBytes) {
  APPLE_AGX_U64 storage[
      APPLE_AGX_WIN32_COMMAND_MAX_BYTES / sizeof(APPLE_AGX_U64)];
  unsigned char *bytes = (unsigned char *)storage;
  APPLE_AGX_WIN32_COMMAND_HEADER *header;
  APPLE_AGX_WIN32_DRAW_PAYLOAD draw;
  APPLE_AGX_WIN32_COMMAND_VIEW view;
  APPLE_AGX_WIN32_ABI_RESULT result;
  APPLE_AGX_U32 referenceBytes;
  APPLE_AGX_U32 relocationBytes;
  APPLE_AGX_U32 payloadBytes;
  APPLE_AGX_U32 totalBytes;
  if (Request == NULL || CommandBuffer == NULL || CommandBytes == NULL ||
      Request->Generation == 0u || Request->AllocationCount == 0u ||
      Request->References == NULL || Request->Relocations == NULL ||
      Request->ReferenceCount == 0u ||
      Request->ReferenceCount > APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES ||
      Request->RelocationCount == 0u ||
      Request->RelocationCount > APPLE_AGX_WIN32_COMMAND_MAX_RELOCATIONS)
    return AppleAgxWin32AbiArgument;
  referenceBytes = Request->ReferenceCount *
      (APPLE_AGX_U32)sizeof(APPLE_AGX_WIN32_ALLOCATION_REFERENCE);
  relocationBytes = Request->RelocationCount *
      (APPLE_AGX_U32)sizeof(APPLE_AGX_WIN32_RELOCATION);
  payloadBytes = (APPLE_AGX_U32)sizeof(draw) + relocationBytes;
  totalBytes = (APPLE_AGX_U32)sizeof(*header) + referenceBytes + payloadBytes;
  if (totalBytes > APPLE_AGX_WIN32_COMMAND_MAX_BYTES ||
      CommandCapacity < totalBytes)
    return AppleAgxWin32AbiArgument;
  memset(storage, 0, sizeof(storage));
  header = (APPLE_AGX_WIN32_COMMAND_HEADER *)bytes;
  header->Magic = APPLE_AGX_WIN32_COMMAND_MAGIC;
  header->Version = APPLE_AGX_WIN32_COMMAND_VERSION;
  header->HeaderBytes = sizeof(*header);
  header->TotalBytes = totalBytes;
  header->Opcode = AppleAgxWin32OpcodeDraw;
  header->Generation = Request->Generation;
  header->ReferenceCount = Request->ReferenceCount;
  header->ReferencesOffset = sizeof(*header);
  header->PayloadOffset = sizeof(*header) + referenceBytes;
  header->PayloadBytes = payloadBytes;
  memcpy(bytes + header->ReferencesOffset, Request->References,
         referenceBytes);
  draw = Request->Draw;
  draw.StructBytes = sizeof(draw);
  draw.RelocationsOffset = sizeof(draw);
  draw.RelocationCount = Request->RelocationCount;
  memcpy(bytes + header->PayloadOffset, &draw, sizeof(draw));
  memcpy(bytes + header->PayloadOffset + sizeof(draw), Request->Relocations,
         relocationBytes);
  header->ContentHash = AppleAgxWin32CommandHash(bytes, totalBytes);
  result = AppleAgxWin32CommandValidate(
      bytes, totalBytes, Request->Generation, Request->AllocationCount, &view);
  if (result != AppleAgxWin32AbiSuccess)
    return result;
  memcpy(CommandBuffer, bytes, totalBytes);
  *CommandBytes = totalBytes;
  return AppleAgxWin32AbiSuccess;
}

AGX_WIN32_WINSYS_RESULT AgxWin32WinsysInitialize(
    AGX_WIN32_WINSYS *Winsys, void *Context, APPLE_AGX_U32 Generation,
    const AGX_WIN32_WINSYS_OPERATIONS *Operations) {
  AGX_WIN32_WINSYS initialized;
  if (Winsys == NULL || Context == NULL || Generation == 0u ||
      Operations == NULL || Operations->CreateBuffer == NULL ||
      Operations->MapBuffer == NULL || Operations->UnmapBuffer == NULL ||
      Operations->DestroyBuffer == NULL || Operations->SubmitClear == NULL ||
      Operations->WaitFence == NULL || Operations->RetireFence == NULL)
    return AgxWin32WinsysArgument;
  memset(&initialized, 0, sizeof(initialized));
  initialized.Context = Context;
  initialized.Generation = Generation;
  initialized.Operations = *Operations;
  *Winsys = initialized;
  return AgxWin32WinsysSuccess;
}

AGX_WIN32_WINSYS_RESULT AgxWin32WinsysCreateBuffer(
    AGX_WIN32_WINSYS *Winsys, APPLE_AGX_U64 Bytes, APPLE_AGX_U32 Flags,
    AGX_WIN32_BUFFER *Buffer) {
  AGX_WIN32_BUFFER created;
  APPLE_AGX_U64 token = 0ULL;
  if (Winsys == NULL || Winsys->Generation == 0u || Buffer == NULL ||
      Bytes == 0ULL || Flags == 0u ||
      (Flags & ~AGX_WIN32_BUFFER_FLAGS_ALLOWED) != 0u)
    return AgxWin32WinsysArgument;
  if (!Winsys->Operations.CreateBuffer(Winsys->Context, Bytes, Flags, &token) ||
      token == 0ULL)
    return AgxWin32WinsysCallback;
  memset(&created, 0, sizeof(created));
  created.Token = token;
  created.Bytes = Bytes;
  created.Generation = Winsys->Generation;
  created.Flags = Flags;
  *Buffer = created;
  return AgxWin32WinsysSuccess;
}

AGX_WIN32_WINSYS_RESULT AgxWin32WinsysMapBuffer(
    AGX_WIN32_WINSYS *Winsys, AGX_WIN32_BUFFER *Buffer,
    APPLE_AGX_U64 Offset, APPLE_AGX_U64 Bytes, APPLE_AGX_U32 Access,
    void **Address) {
  void *mapped = NULL;
  if (Winsys == NULL || Buffer == NULL || Address == NULL ||
      Winsys->Generation == 0u || Buffer->Token == 0ULL)
    return AgxWin32WinsysArgument;
  if (Buffer->Generation != Winsys->Generation)
    return AgxWin32WinsysStaleGeneration;
  if (Buffer->Mapped)
    return AgxWin32WinsysState;
  if (Bytes == 0ULL || Offset > Buffer->Bytes || Bytes > Buffer->Bytes - Offset)
    return AgxWin32WinsysRange;
  if (Access == 0u || (Access & ~AGX_WIN32_CPU_ACCESS_ALLOWED) != 0u ||
      (Access & Buffer->Flags) != Access)
    return AgxWin32WinsysAccess;
  if (!Winsys->Operations.MapBuffer(Winsys->Context, Buffer->Token, Offset,
                                    Bytes, Access, &mapped) ||
      mapped == NULL)
    return AgxWin32WinsysCallback;
  Buffer->Mapped = APPLE_AGX_TRUE;
  *Address = mapped;
  return AgxWin32WinsysSuccess;
}

AGX_WIN32_WINSYS_RESULT AgxWin32WinsysUnmapBuffer(
    AGX_WIN32_WINSYS *Winsys, AGX_WIN32_BUFFER *Buffer) {
  if (Winsys == NULL || Buffer == NULL || Buffer->Token == 0ULL)
    return AgxWin32WinsysArgument;
  if (Buffer->Generation != Winsys->Generation)
    return AgxWin32WinsysStaleGeneration;
  if (!Buffer->Mapped)
    return AgxWin32WinsysState;
  if (!Winsys->Operations.UnmapBuffer(Winsys->Context, Buffer->Token))
    return AgxWin32WinsysCallback;
  Buffer->Mapped = APPLE_AGX_FALSE;
  return AgxWin32WinsysSuccess;
}

AGX_WIN32_WINSYS_RESULT AgxWin32WinsysDestroyBuffer(
    AGX_WIN32_WINSYS *Winsys, AGX_WIN32_BUFFER *Buffer) {
  if (Winsys == NULL || Buffer == NULL || Buffer->Token == 0ULL)
    return AgxWin32WinsysArgument;
  if (Buffer->Generation != Winsys->Generation)
    return AgxWin32WinsysStaleGeneration;
  if (Buffer->Mapped)
    return AgxWin32WinsysState;
  if (!Winsys->Operations.DestroyBuffer(Winsys->Context, Buffer->Token))
    return AgxWin32WinsysCallback;
  memset(Buffer, 0, sizeof(*Buffer));
  return AgxWin32WinsysSuccess;
}

AGX_WIN32_WINSYS_RESULT AgxWin32WinsysSubmitClear(
    AGX_WIN32_WINSYS *Winsys, const AGX_WIN32_CLEAR_REQUEST *Request,
    APPLE_AGX_U32 *Fence) {
  APPLE_AGX_U32 fence = 0u;
  if (Winsys == NULL || Request == NULL || Fence == NULL ||
      Winsys->Generation == 0u)
    return AgxWin32WinsysArgument;
  if (Request->Generation != Winsys->Generation)
    return AgxWin32WinsysStaleGeneration;
  if (!Winsys->Operations.SubmitClear(Winsys->Context, Request, &fence) ||
      fence == 0u)
    return AgxWin32WinsysCallback;
  *Fence = fence;
  return AgxWin32WinsysSuccess;
}

AGX_WIN32_WINSYS_RESULT AgxWin32WinsysWaitFence(
    AGX_WIN32_WINSYS *Winsys, APPLE_AGX_U32 Fence,
    APPLE_AGX_U32 TimeoutMs) {
  if (Winsys == NULL || Winsys->Generation == 0u || Fence == 0u)
    return AgxWin32WinsysArgument;
  return Winsys->Operations.WaitFence(Winsys->Context, Fence, TimeoutMs)
             ? AgxWin32WinsysSuccess
             : AgxWin32WinsysCallback;
}

AGX_WIN32_WINSYS_RESULT AgxWin32WinsysRetireFence(
    AGX_WIN32_WINSYS *Winsys, APPLE_AGX_U32 Fence) {
  if (Winsys == NULL || Winsys->Generation == 0u || Fence == 0u)
    return AgxWin32WinsysArgument;
  return Winsys->Operations.RetireFence(Winsys->Context, Fence)
             ? AgxWin32WinsysSuccess
             : AgxWin32WinsysCallback;
}
