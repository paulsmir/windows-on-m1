#include "apple_agx_g4_submit.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct {
  APPLE_AGX_G4_PRIVATE_HEADER header;
  APPLE_AGX_G4_NATIVE_HEADER attachments;
  APPLE_AGX_G4_ATTACHMENT attachment;
  APPLE_AGX_G4_NATIVE_HEADER render;
  unsigned char render_payload[240];
} PACKET;

static int mapped(void *opaque, unsigned long long va,
                  unsigned int bytes, int write) {
  unsigned *calls = opaque;
  ++*calls;
  if (write)
    return va == 0x30000ULL && bytes == 0x4000u;
  return va == 0x20000ULL && bytes ==
      sizeof(PACKET) - sizeof(APPLE_AGX_G4_PRIVATE_HEADER);
}

static APPLE_AGX_G4_PARSE_RESULT parse(PACKET *packet, unsigned *calls,
                                      APPLE_AGX_G4_SUBMIT_VIEW *view) {
  return AppleAgxG4ParseSubmit(packet, sizeof(*packet), sizeof(*packet),
      0x20000ULL, sizeof(*packet) - sizeof(packet->header),
      mapped, calls, view);
}

int main(void) {
  PACKET packet = {0}, bad;
  APPLE_AGX_G4_SUBMIT_VIEW view = {0};
  unsigned calls = 0;
  packet.header.Magic = APPLE_AGX_G4_PRIVATE_MAGIC;
  packet.header.Version = 1u;
  packet.header.HeaderBytes = sizeof(packet.header);
  packet.header.CommandBytes = sizeof(packet) - sizeof(packet.header);
  packet.header.CommandVa = 0x20000ULL;
  packet.attachments.Type = APPLE_AGX_G4_FRAGMENT_ATTACHMENTS;
  packet.attachments.Size = sizeof(packet.attachment);
  packet.attachments.VdmBarrier = 0xffffu;
  packet.attachments.CdmBarrier = 0xffffu;
  packet.attachment.Pointer = 0x30000ULL;
  packet.attachment.Size = 0x4000ULL;
  packet.render.Type = APPLE_AGX_G4_RENDER;
  packet.render.Size = sizeof(packet.render_payload);
  assert(parse(&packet, &calls, &view) == AppleAgxG4ParseOk);
  assert(calls == 2u && view.RenderBytes == 240u &&
         view.AttachmentCount == 1u && view.CommandVa == 0x20000ULL);

  bad = packet; bad.header.CommandVa = 0x24000ULL;
  assert(parse(&bad, &calls, &view) == AppleAgxG4ParseInvalid);
  bad = packet; bad.header.Magic = 0u;
  assert(parse(&bad, &calls, &view) == AppleAgxG4ParseInvalid);
  bad = packet; bad.header.CommandBytes--;
  assert(parse(&bad, &calls, &view) == AppleAgxG4ParseInvalid);
  bad = packet; bad.header.Reserved = 1u;
  assert(parse(&bad, &calls, &view) == AppleAgxG4ParseInvalid);
  bad = packet; bad.attachments.Size++;
  assert(parse(&bad, &calls, &view) == AppleAgxG4ParseInvalid);
  bad = packet; bad.render.Type = APPLE_AGX_G4_COMPUTE;
  assert(parse(&bad, &calls, &view) == AppleAgxG4ParseUnsupported);
  bad = packet; bad.attachment.Pointer = 0x34000ULL;
  assert(parse(&bad, &calls, &view) == AppleAgxG4ParseUnmapped);
  bad = packet; bad.attachment.Size = 0xffffffffULL;
  assert(parse(&bad, &calls, &view) == AppleAgxG4ParseUnmapped);
  bad = packet; bad.attachment.Pad = 1u;
  assert(parse(&bad, &calls, &view) == AppleAgxG4ParseInvalid);
  assert(AppleAgxG4ParseSubmit(&packet, sizeof(packet) - 1u,
      sizeof(packet), 0x20000ULL, packet.header.CommandBytes,
      mapped, &calls, &view) == AppleAgxG4ParseInvalid);
  assert(AppleAgxG4ParseSubmit(&packet, sizeof(packet), sizeof(packet),
      (1ULL << 39) - 1u, packet.header.CommandBytes,
      mapped, &calls, &view) == AppleAgxG4ParseInvalid);
  return 0;
}
