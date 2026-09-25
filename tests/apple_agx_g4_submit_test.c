#include "apple_agx_g4_submit.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

typedef struct {
  APPLE_AGX_G4_PRIVATE_HEADER header;
  APPLE_AGX_G4_NATIVE_HEADER attachments;
  APPLE_AGX_G4_ATTACHMENT attachment;
  APPLE_AGX_G4_NATIVE_HEADER render;
  APPLE_AGX_G4_NATIVE_RENDER render_payload;
} PACKET;

typedef struct {
  APPLE_AGX_G4_PRIVATE_HEADER_V2 header;
  APPLE_AGX_G4_NATIVE_HEADER render;
  APPLE_AGX_G4_NATIVE_RENDER render_payload;
} PACKET_V2;

static int mapped(void *opaque, unsigned long long va,
                  unsigned int bytes, int write) {
  unsigned *calls = opaque;
  ++*calls;
  if (write)
    return (va == 0x30000ULL && bytes == 0x4000u) ||
      (va >= 0x100000ULL && va < 0x190000ULL &&
       (va & 0xffffULL) == 0 && bytes == 0x10000u);
  return (va == 0x20000ULL && bytes ==
      sizeof(PACKET) - sizeof(APPLE_AGX_G4_PRIVATE_HEADER)) ||
      (va == 0x40000ULL && bytes == 1u) ||
      (va == 0x50000ULL && bytes == sizeof(PACKET_V2) -
          sizeof(APPLE_AGX_G4_PRIVATE_HEADER_V2));
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
  packet.render_payload.VdmCtrlStreamBase = 0x40000ULL;
  packet.render_payload.WidthPx = 2560u;
  packet.render_payload.HeightPx = 1600u;
  packet.render_payload.Layers = 1u;
  packet.render_payload.UtileWidthPx = 32u;
  packet.render_payload.UtileHeightPx = 32u;
  packet.render_payload.Samples = 1u;
  packet.render_payload.SampleSizeBytes = 4u;
  assert(parse(&packet, &calls, &view) == AppleAgxG4ParseOk);
  assert(calls == 3u && view.RenderBytes == 240u &&
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
  bad = packet; bad.render_payload.WidthPx = 0u;
  assert(parse(&bad, &calls, &view) == AppleAgxG4ParseInvalid);
  bad = packet; bad.render_payload.Flags = 0x80000000u;
  assert(parse(&bad, &calls, &view) == AppleAgxG4ParseInvalid);
  bad = packet; bad.render_payload.VdmCtrlStreamBase = 0x50000ULL;
  assert(parse(&bad, &calls, &view) == AppleAgxG4ParseUnmapped);
  bad = packet; bad.render_payload.TimestampsVertex.StartHandle = 1u;
  assert(parse(&bad, &calls, &view) == AppleAgxG4ParseUnsupported);
  assert(AppleAgxG4ParseSubmit(&packet, sizeof(packet) - 1u,
      sizeof(packet), 0x20000ULL, packet.header.CommandBytes,
      mapped, &calls, &view) == AppleAgxG4ParseInvalid);
  assert(AppleAgxG4ParseSubmit(&packet, sizeof(packet), sizeof(packet),
      (1ULL << 39) - 1u, packet.header.CommandBytes,
      mapped, &calls, &view) == AppleAgxG4ParseInvalid);
  {
    PACKET_V2 native = {0}, changed;
    native.header.Base.Magic = APPLE_AGX_G4_PRIVATE_MAGIC;
    native.header.Base.Version = APPLE_AGX_G4_PRIVATE_VERSION_PROCESS_VA;
    native.header.Base.HeaderBytes = sizeof(native.header);
    native.header.Base.CommandBytes = sizeof(native) - sizeof(native.header);
    native.header.Base.CommandVa = 0x50000ULL;
    native.render.Type = APPLE_AGX_G4_RENDER;
    native.render.Size = sizeof(native.render_payload);
    native.render_payload = packet.render_payload;
    for (unsigned i = 0; i < APPLE_AGX_G4_PROCESS_RANGE_COUNT; ++i) {
      native.header.Process[i].Va = 0x100000ULL + i * 0x10000ULL;
      native.header.Process[i].Bytes = 0x10000u;
    }
    assert(AppleAgxG4ParseSubmit(&native, sizeof(native), sizeof(native),
        0x50000ULL, native.header.Base.CommandBytes, mapped, &calls,
        &view) == AppleAgxG4ParseOk);
    assert(view.Process[0].Va == 0x100000ULL &&
           view.Process[8].Va == 0x180000ULL);
    changed = native;
    changed.header.Process[1].Va = changed.header.Process[0].Va;
    assert(AppleAgxG4ParseSubmit(&changed, sizeof(changed), sizeof(changed),
        0x50000ULL, changed.header.Base.CommandBytes, mapped, &calls,
        &view) == AppleAgxG4ParseInvalid);
    changed = native;
    changed.header.Process[2].Bytes = 0x4000u;
    assert(AppleAgxG4ParseSubmit(&changed, sizeof(changed), sizeof(changed),
        0x50000ULL, changed.header.Base.CommandBytes, mapped, &calls,
        &view) == AppleAgxG4ParseInvalid);
    changed = native;
    changed.header.Process[3].Va += 0x4000ULL;
    assert(AppleAgxG4ParseSubmit(&changed, sizeof(changed), sizeof(changed),
        0x50000ULL, changed.header.Base.CommandBytes, mapped, &calls,
        &view) == AppleAgxG4ParseInvalid);
    changed = native;
    changed.header.Process[4].Va = 0x190000ULL;
    assert(AppleAgxG4ParseSubmit(&changed, sizeof(changed), sizeof(changed),
        0x50000ULL, changed.header.Base.CommandBytes, mapped, &calls,
        &view) == AppleAgxG4ParseUnmapped);
    changed = native;
    changed.header.Process[5].Reserved = 1u;
    assert(AppleAgxG4ParseSubmit(&changed, sizeof(changed), sizeof(changed),
        0x50000ULL, changed.header.Base.CommandBytes, mapped, &calls,
        &view) == AppleAgxG4ParseInvalid);
  }
  return 0;
}
