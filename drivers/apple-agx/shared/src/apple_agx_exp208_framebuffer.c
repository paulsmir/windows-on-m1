#include "apple_agx_exp208_framebuffer.h"

#define FRAMEBUFFER_NULL ((void *)0)
#define FRAMEBUFFER_TPC_OBJECT 64u
#define FRAMEBUFFER_TILEMAP_OBJECT 65u
#define FRAMEBUFFER_CLUSTER_TILEMAP_OBJECT 67u
#define FRAMEBUFFER_COMMAND_3D_OBJECT 18u
#define FRAMEBUFFER_COMMAND_TA_OBJECT 19u
#define FRAMEBUFFER_PBE_OBJECT 36u
#define FRAMEBUFFER_TPC_OFFSET 0x5d0000u
#define FRAMEBUFFER_TPC_BYTES 0x50000u
#define FRAMEBUFFER_TILEMAP_OFFSET 0x620000u
#define FRAMEBUFFER_TILEMAP_BYTES 0x6400u
#define FRAMEBUFFER_CLUSTER_TILEMAP_OFFSET 0x628000u
#define FRAMEBUFFER_CLUSTER_TILEMAP_BYTES 0x32000u
#define FRAMEBUFFER_SCRATCH_END 0x65a000u
#define FRAMEBUFFER_ADDRESS_MASK 0xfffffffffULL

static APPLE_AGX_U32 FramebufferReadU16(const unsigned char *Address) {
  return (APPLE_AGX_U32)Address[0] |
         ((APPLE_AGX_U32)Address[1] << 8u);
}

static APPLE_AGX_U32 FramebufferReadU32(const unsigned char *Address) {
  return (APPLE_AGX_U32)Address[0] |
         ((APPLE_AGX_U32)Address[1] << 8u) |
         ((APPLE_AGX_U32)Address[2] << 16u) |
         ((APPLE_AGX_U32)Address[3] << 24u);
}

static APPLE_AGX_U64 FramebufferReadU64(const unsigned char *Address) {
  APPLE_AGX_U64 value = 0ULL;
  APPLE_AGX_U32 index;
  for (index = 0u; index < 8u; ++index)
    value |= (APPLE_AGX_U64)Address[index] << (index * 8u);
  return value;
}

static void FramebufferWriteU16(unsigned char *Address,
                                APPLE_AGX_U32 Value) {
  Address[0] = (unsigned char)Value;
  Address[1] = (unsigned char)(Value >> 8u);
}

static void FramebufferWriteU32(unsigned char *Address,
                                APPLE_AGX_U32 Value) {
  APPLE_AGX_U32 index;
  for (index = 0u; index < 4u; ++index)
    Address[index] = (unsigned char)(Value >> (index * 8u));
}

static void FramebufferWriteU64(unsigned char *Address,
                                APPLE_AGX_U64 Value) {
  APPLE_AGX_U32 index;
  for (index = 0u; index < 8u; ++index)
    Address[index] = (unsigned char)(Value >> (index * 8u));
}

static void FramebufferZero(unsigned char *Address, APPLE_AGX_U32 Bytes) {
  APPLE_AGX_U32 index;
  for (index = 0u; index < Bytes; ++index)
    Address[index] = 0u;
}

static APPLE_AGX_BOOL FramebufferObjectMatches(
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *Object,
    const void *Data, APPLE_AGX_U64 GpuVa,
    APPLE_AGX_U64 PhysicalAddress, APPLE_AGX_U32 Size) {
  return Object != FRAMEBUFFER_NULL && Object->Data == Data &&
                 Object->GpuVa == GpuVa &&
                 Object->PhysicalAddress == PhysicalAddress &&
                 Object->Size == Size
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

static APPLE_AGX_BOOL FramebufferCapturedGeometryValid(
    const unsigned char *Command3d, const unsigned char *CommandTa,
    const unsigned char *Pbe) {
  return Command3d != FRAMEBUFFER_NULL &&
                 CommandTa != FRAMEBUFFER_NULL && Pbe != FRAMEBUFFER_NULL &&
                 FramebufferReadU16(Command3d + 0x54u) == 4u &&
                 FramebufferReadU16(Command3d + 0x56u) == 4u &&
                 FramebufferReadU32(Command3d + 0x68u) == 0x3dddb3d9u &&
                 FramebufferReadU32(Command3d + 0x6cu) == 0x3dddb3d9u &&
                 FramebufferReadU64(Command3d + 0x78u) == 1ULL &&
                 FramebufferReadU32(Command3d + 0xb8u) == 16u &&
                 FramebufferReadU32(Command3d + 0xbcu) == 16u &&
                 FramebufferReadU64(Command3d + 0xc8u) == 0x7800fULL &&
                 FramebufferReadU64(Command3d + 0x170u) == 0x14000000ULL &&
                 FramebufferReadU32(Command3d + 0x3d8u) == 0x3dddb3d9u &&
                 FramebufferReadU32(Command3d + 0x3dcu) == 0x3dddb3d9u &&
                 FramebufferReadU16(Command3d + 0x3e8u) == 4u &&
                 FramebufferReadU16(Command3d + 0x3eau) == 4u &&
                 FramebufferReadU32(Command3d + 0x3f0u) == 0u &&
                 FramebufferReadU32(Command3d + 0x6e0u) == 16u &&
                 FramebufferReadU32(Command3d + 0x6e4u) == 16u &&
                 FramebufferReadU64(Command3d + 0x768u) == 0x7800fULL &&
                 FramebufferReadU32(CommandTa + 0x3c4u) == 20u &&
                 FramebufferReadU16(CommandTa + 0x3d0u) == 15u &&
                 FramebufferReadU16(CommandTa + 0x3d2u) == 15u &&
                 FramebufferReadU32(CommandTa + 0x3d4u) == 0u &&
                 FramebufferReadU32(CommandTa + 0x3d8u) == 0x10100cu &&
                 FramebufferReadU32(CommandTa + 0x3dcu) == 0x10100cu &&
                 FramebufferReadU32(CommandTa + 0x3e0u) == 16u &&
                 FramebufferReadU32(CommandTa + 0x3e4u) == 32u &&
                 FramebufferReadU64(CommandTa + 0x46cu) == 0x4000ULL &&
                 FramebufferReadU64(Pbe) == 0x000003c00fc60a22ULL &&
                 (FramebufferReadU64(Pbe + 8u) &
                  ~FRAMEBUFFER_ADDRESS_MASK) == 0x1000000000000000ULL &&
                 FramebufferReadU64(Pbe + 16u) == 0ULL
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

static APPLE_AGX_BOOL FramebufferBoundGeometryValid(
    const unsigned char *Command3d, const unsigned char *CommandTa,
    const unsigned char *Pbe,
    const APPLE_AGX_EXP208_FRAMEBUFFER_BINDING *Binding) {
  APPLE_AGX_U64 pbe1;
  if (Command3d == FRAMEBUFFER_NULL || CommandTa == FRAMEBUFFER_NULL ||
      Pbe == FRAMEBUFFER_NULL || Binding == FRAMEBUFFER_NULL)
    return APPLE_AGX_FALSE;
  pbe1 = FramebufferReadU64(Pbe + 8u);
  return FramebufferReadU16(Command3d + 0x54u) == 16u &&
                 FramebufferReadU16(Command3d + 0x56u) == 20u &&
                 FramebufferReadU32(Command3d + 0x68u) == 0x3a315caeu &&
                 FramebufferReadU32(Command3d + 0x6cu) == 0x3a8de3beu &&
                 FramebufferReadU64(Command3d + 0x78u) == 4000ULL &&
                 FramebufferReadU32(Command3d + 0xb8u) == 2560u &&
                 FramebufferReadU32(Command3d + 0xbcu) == 1600u &&
                 FramebufferReadU64(Command3d + 0xc8u) == 0x31f89ffULL &&
                 FramebufferReadU64(Command3d + 0x170u) == 0x190000000ULL &&
                 FramebufferReadU32(Command3d + 0x3d8u) == 0x3a315caeu &&
                 FramebufferReadU32(Command3d + 0x3dcu) == 0x3a8de3beu &&
                 FramebufferReadU16(Command3d + 0x3e8u) == 16u &&
                 FramebufferReadU16(Command3d + 0x3eau) == 20u &&
                 FramebufferReadU32(Command3d + 0x3f0u) == 0x3104fu &&
                 FramebufferReadU32(Command3d + 0x6e0u) == 2560u &&
                 FramebufferReadU32(Command3d + 0x6e4u) == 1600u &&
                 FramebufferReadU64(Command3d + 0x768u) == 0x31f89ffULL &&
                 FramebufferReadU32(CommandTa + 0x3c4u) == 400u &&
                 FramebufferReadU16(CommandTa + 0x3d0u) == 2559u &&
                 FramebufferReadU16(CommandTa + 0x3d2u) == 1599u &&
                 FramebufferReadU32(CommandTa + 0x3d4u) == 0x3104fu &&
                 FramebufferReadU32(CommandTa + 0x3d8u) == 0x50503cu &&
                 FramebufferReadU32(CommandTa + 0x3dcu) == 0x404030u &&
                 FramebufferReadU32(CommandTa + 0x3e0u) == 320u &&
                 FramebufferReadU32(CommandTa + 0x3e4u) == 640u &&
                 FramebufferReadU64(CommandTa + 0x46cu) == 0x50000ULL &&
                 FramebufferReadU64(Pbe) == Binding->BoundPbe0 &&
                 (pbe1 & ~FRAMEBUFFER_ADDRESS_MASK) ==
                     (Binding->BoundPbe1 & ~FRAMEBUFFER_ADDRESS_MASK) &&
                 FramebufferReadU64(Pbe + 16u) == 0ULL
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

static void FramebufferWriteFullGeometry(
    unsigned char *Command3d, unsigned char *CommandTa,
    unsigned char *Pbe, APPLE_AGX_U64 *BoundPbe0,
    APPLE_AGX_U64 *BoundPbe1) {
  APPLE_AGX_U64 pbe0 = FramebufferReadU64(Pbe);
  APPLE_AGX_U64 pbe1 = FramebufferReadU64(Pbe + 8u);
  APPLE_AGX_U64 geometryMask = (3ULL << 4u) |
                               (0x3fffULL << 24u) |
                               (0x3fffULL << 38u);
  FramebufferWriteU16(Command3d + 0x54u, 16u);
  FramebufferWriteU16(Command3d + 0x56u, 20u);
  FramebufferWriteU32(Command3d + 0x68u, 0x3a315caeu);
  FramebufferWriteU32(Command3d + 0x6cu, 0x3a8de3beu);
  FramebufferWriteU64(Command3d + 0x78u, 4000ULL);
  FramebufferWriteU32(Command3d + 0xb8u, 2560u);
  FramebufferWriteU32(Command3d + 0xbcu, 1600u);
  FramebufferWriteU64(Command3d + 0xc8u, 0x31f89ffULL);
  FramebufferWriteU64(Command3d + 0x170u, 0x190000000ULL);
  FramebufferWriteU32(Command3d + 0x3d8u, 0x3a315caeu);
  FramebufferWriteU32(Command3d + 0x3dcu, 0x3a8de3beu);
  FramebufferWriteU16(Command3d + 0x3e8u, 16u);
  FramebufferWriteU16(Command3d + 0x3eau, 20u);
  FramebufferWriteU32(Command3d + 0x3f0u, 0x3104fu);
  FramebufferWriteU32(Command3d + 0x6e0u, 2560u);
  FramebufferWriteU32(Command3d + 0x6e4u, 1600u);
  FramebufferWriteU64(Command3d + 0x768u, 0x31f89ffULL);

  FramebufferWriteU32(CommandTa + 0x3c4u, 400u);
  FramebufferWriteU16(CommandTa + 0x3d0u, 2559u);
  FramebufferWriteU16(CommandTa + 0x3d2u, 1599u);
  FramebufferWriteU32(CommandTa + 0x3d4u, 0x3104fu);
  FramebufferWriteU32(CommandTa + 0x3d8u, 0x50503cu);
  FramebufferWriteU32(CommandTa + 0x3dcu, 0x404030u);
  FramebufferWriteU32(CommandTa + 0x3e0u, 320u);
  FramebufferWriteU32(CommandTa + 0x3e4u, 640u);
  FramebufferWriteU64(CommandTa + 0x46cu, 0x50000ULL);

  pbe0 &= ~geometryMask;
  pbe0 |= (2559ULL << 24u) | (1599ULL << 38u);
  pbe1 = (pbe1 & FRAMEBUFFER_ADDRESS_MASK) | (10236ULL << 40u);
  FramebufferWriteU64(Pbe, pbe0);
  FramebufferWriteU64(Pbe + 8u, pbe1);
  *BoundPbe0 = pbe0;
  *BoundPbe1 = pbe1;
}

static void FramebufferRestoreCapturedGeometry(
    unsigned char *Command3d, unsigned char *CommandTa,
    unsigned char *Pbe, const APPLE_AGX_EXP208_FRAMEBUFFER_BINDING *Binding) {
  FramebufferWriteU16(Command3d + 0x54u, 4u);
  FramebufferWriteU16(Command3d + 0x56u, 4u);
  FramebufferWriteU32(Command3d + 0x68u, 0x3dddb3d9u);
  FramebufferWriteU32(Command3d + 0x6cu, 0x3dddb3d9u);
  FramebufferWriteU64(Command3d + 0x78u, 1ULL);
  FramebufferWriteU32(Command3d + 0xb8u, 16u);
  FramebufferWriteU32(Command3d + 0xbcu, 16u);
  FramebufferWriteU64(Command3d + 0xc8u, 0x7800fULL);
  FramebufferWriteU64(Command3d + 0x170u, 0x14000000ULL);
  FramebufferWriteU32(Command3d + 0x3d8u, 0x3dddb3d9u);
  FramebufferWriteU32(Command3d + 0x3dcu, 0x3dddb3d9u);
  FramebufferWriteU16(Command3d + 0x3e8u, 4u);
  FramebufferWriteU16(Command3d + 0x3eau, 4u);
  FramebufferWriteU32(Command3d + 0x3f0u, 0u);
  FramebufferWriteU32(Command3d + 0x6e0u, 16u);
  FramebufferWriteU32(Command3d + 0x6e4u, 16u);
  FramebufferWriteU64(Command3d + 0x768u, 0x7800fULL);
  FramebufferWriteU32(CommandTa + 0x3c4u, 20u);
  FramebufferWriteU16(CommandTa + 0x3d0u, 15u);
  FramebufferWriteU16(CommandTa + 0x3d2u, 15u);
  FramebufferWriteU32(CommandTa + 0x3d4u, 0u);
  FramebufferWriteU32(CommandTa + 0x3d8u, 0x10100cu);
  FramebufferWriteU32(CommandTa + 0x3dcu, 0x10100cu);
  FramebufferWriteU32(CommandTa + 0x3e0u, 16u);
  FramebufferWriteU32(CommandTa + 0x3e4u, 32u);
  FramebufferWriteU64(CommandTa + 0x46cu, 0x4000ULL);
  FramebufferWriteU64(Pbe, Binding->OriginalPbe0);
  FramebufferWriteU64(Pbe + 8u, Binding->OriginalPbe1);
}

APPLE_AGX_BOOL AppleAgxExp208FramebufferBind(
    void *ArenaCpuAddress, APPLE_AGX_U64 ArenaGpuAddress,
    APPLE_AGX_U64 ArenaPhysicalAddress, APPLE_AGX_U32 ArenaCapacity,
    APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects,
    APPLE_AGX_U32 ObjectCount,
    APPLE_AGX_EXP208_FRAMEBUFFER_BINDING *Binding) {
  APPLE_AGX_EXP208_FRAMEBUFFER_BINDING candidate;
  const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *layouts;
  unsigned char *arena = (unsigned char *)ArenaCpuAddress;
  unsigned char *command3d;
  unsigned char *commandTa;
  unsigned char *pbe;
  const APPLE_AGX_EXP208_RELOCATION_OBJECT *arenaObject;
  APPLE_AGX_U32 index;

  if (arena == FRAMEBUFFER_NULL || Objects == FRAMEBUFFER_NULL ||
      Binding == FRAMEBUFFER_NULL ||
      ObjectCount < APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT ||
      ArenaCapacity != APPLE_AGX_EXP208_FRAMEBUFFER_BACKEND_BYTES ||
      (ArenaGpuAddress & (APPLE_AGX_RENDER_TEMPLATE_ALIGNMENT - 1u)) != 0ULL ||
      (ArenaPhysicalAddress & 0x3fffULL) != 0ULL ||
      ArenaGpuAddress > ~0ULL - ArenaCapacity ||
      ArenaPhysicalAddress >= (1ULL << 40u) ||
      ArenaPhysicalAddress > (1ULL << 40u) - ArenaCapacity)
    return APPLE_AGX_FALSE;
  arenaObject = &Objects[APPLE_AGX_RENDER_TEMPLATE_ARENA_OBJECT_INDEX];
  if (!FramebufferObjectMatches(
          arenaObject, arena, ArenaGpuAddress, ArenaPhysicalAddress,
          AppleAgxRenderTemplateBytes()))
    return APPLE_AGX_FALSE;
  layouts = AppleAgxRenderTemplateObjectLayouts();
  for (index = 0u; index < AppleAgxRenderTemplateObjectCount(); ++index) {
    if (!FramebufferObjectMatches(
            &Objects[index], arena + layouts[index].ArenaOffset,
            ArenaGpuAddress + layouts[index].ArenaOffset,
            ArenaPhysicalAddress + layouts[index].ArenaOffset,
            layouts[index].Size))
      return APPLE_AGX_FALSE;
  }
  command3d = Objects[FRAMEBUFFER_COMMAND_3D_OBJECT].Data;
  commandTa = Objects[FRAMEBUFFER_COMMAND_TA_OBJECT].Data;
  pbe = Objects[FRAMEBUFFER_PBE_OBJECT].Data + 0x3000u;
  if (!FramebufferCapturedGeometryValid(command3d, commandTa, pbe) ||
      (FramebufferReadU64(pbe + 8u) & FRAMEBUFFER_ADDRESS_MASK) !=
          (0x15001d0000ULL >> 4u))
    return APPLE_AGX_FALSE;

  FramebufferZero((unsigned char *)&candidate,
                  (APPLE_AGX_U32)sizeof(candidate));
  candidate.OriginalTpc = Objects[FRAMEBUFFER_TPC_OBJECT];
  candidate.OriginalTilemap = Objects[FRAMEBUFFER_TILEMAP_OBJECT];
  candidate.OriginalClusterTilemap =
      Objects[FRAMEBUFFER_CLUSTER_TILEMAP_OBJECT];
  candidate.ArenaGpuAddress = ArenaGpuAddress;
  candidate.ArenaPhysicalAddress = ArenaPhysicalAddress;
  candidate.ArenaCpuAddress = ArenaCpuAddress;
  candidate.ArenaCapacity = ArenaCapacity;
  candidate.OriginalPbe0 = FramebufferReadU64(pbe);
  candidate.OriginalPbe1 = FramebufferReadU64(pbe + 8u);
  candidate.BoundPbe0 = 0ULL;
  candidate.BoundPbe1 = 0ULL;
  candidate.Active = APPLE_AGX_TRUE;

  FramebufferZero(arena + FRAMEBUFFER_TPC_OFFSET,
                  FRAMEBUFFER_SCRATCH_END - FRAMEBUFFER_TPC_OFFSET);
  Objects[FRAMEBUFFER_TPC_OBJECT].Data = arena + FRAMEBUFFER_TPC_OFFSET;
  Objects[FRAMEBUFFER_TPC_OBJECT].GpuVa =
      ArenaGpuAddress + FRAMEBUFFER_TPC_OFFSET;
  Objects[FRAMEBUFFER_TPC_OBJECT].PhysicalAddress =
      ArenaPhysicalAddress + FRAMEBUFFER_TPC_OFFSET;
  Objects[FRAMEBUFFER_TPC_OBJECT].Size = FRAMEBUFFER_TPC_BYTES;
  Objects[FRAMEBUFFER_TILEMAP_OBJECT].Data =
      arena + FRAMEBUFFER_TILEMAP_OFFSET;
  Objects[FRAMEBUFFER_TILEMAP_OBJECT].GpuVa =
      ArenaGpuAddress + FRAMEBUFFER_TILEMAP_OFFSET;
  Objects[FRAMEBUFFER_TILEMAP_OBJECT].PhysicalAddress =
      ArenaPhysicalAddress + FRAMEBUFFER_TILEMAP_OFFSET;
  Objects[FRAMEBUFFER_TILEMAP_OBJECT].Size = FRAMEBUFFER_TILEMAP_BYTES;
  Objects[FRAMEBUFFER_CLUSTER_TILEMAP_OBJECT].Data =
      arena + FRAMEBUFFER_CLUSTER_TILEMAP_OFFSET;
  Objects[FRAMEBUFFER_CLUSTER_TILEMAP_OBJECT].GpuVa =
      ArenaGpuAddress + FRAMEBUFFER_CLUSTER_TILEMAP_OFFSET;
  Objects[FRAMEBUFFER_CLUSTER_TILEMAP_OBJECT].PhysicalAddress =
      ArenaPhysicalAddress + FRAMEBUFFER_CLUSTER_TILEMAP_OFFSET;
  Objects[FRAMEBUFFER_CLUSTER_TILEMAP_OBJECT].Size =
      FRAMEBUFFER_CLUSTER_TILEMAP_BYTES;
  FramebufferWriteFullGeometry(command3d, commandTa, pbe,
                               &candidate.BoundPbe0,
                               &candidate.BoundPbe1);
  *Binding = candidate;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxExp208FramebufferCanUnbind(
    const APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects,
    APPLE_AGX_U32 ObjectCount,
    const APPLE_AGX_EXP208_FRAMEBUFFER_BINDING *Binding) {
  const unsigned char *arena;
  const unsigned char *command3d;
  const unsigned char *commandTa;
  const unsigned char *pbe;
  if (Objects == FRAMEBUFFER_NULL || Binding == FRAMEBUFFER_NULL ||
      Binding->Active != APPLE_AGX_TRUE ||
      ObjectCount < APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT ||
      Binding->ArenaCpuAddress == FRAMEBUFFER_NULL ||
      Binding->ArenaCapacity != APPLE_AGX_EXP208_FRAMEBUFFER_BACKEND_BYTES)
    return APPLE_AGX_FALSE;
  arena = (const unsigned char *)Binding->ArenaCpuAddress;
  command3d = Objects[FRAMEBUFFER_COMMAND_3D_OBJECT].Data;
  commandTa = Objects[FRAMEBUFFER_COMMAND_TA_OBJECT].Data;
  pbe = Objects[FRAMEBUFFER_PBE_OBJECT].Data + 0x3000u;
  return FramebufferObjectMatches(
             &Objects[FRAMEBUFFER_TPC_OBJECT],
             arena + FRAMEBUFFER_TPC_OFFSET,
             Binding->ArenaGpuAddress + FRAMEBUFFER_TPC_OFFSET,
             Binding->ArenaPhysicalAddress + FRAMEBUFFER_TPC_OFFSET,
             FRAMEBUFFER_TPC_BYTES) &&
                 FramebufferObjectMatches(
                     &Objects[FRAMEBUFFER_TILEMAP_OBJECT],
                     arena + FRAMEBUFFER_TILEMAP_OFFSET,
                     Binding->ArenaGpuAddress + FRAMEBUFFER_TILEMAP_OFFSET,
                     Binding->ArenaPhysicalAddress + FRAMEBUFFER_TILEMAP_OFFSET,
                     FRAMEBUFFER_TILEMAP_BYTES) &&
                 FramebufferObjectMatches(
                     &Objects[FRAMEBUFFER_CLUSTER_TILEMAP_OBJECT],
                     arena + FRAMEBUFFER_CLUSTER_TILEMAP_OFFSET,
                     Binding->ArenaGpuAddress +
                         FRAMEBUFFER_CLUSTER_TILEMAP_OFFSET,
                     Binding->ArenaPhysicalAddress +
                         FRAMEBUFFER_CLUSTER_TILEMAP_OFFSET,
                     FRAMEBUFFER_CLUSTER_TILEMAP_BYTES) &&
                 FramebufferBoundGeometryValid(
                     command3d, commandTa, pbe, Binding)
             ? APPLE_AGX_TRUE
             : APPLE_AGX_FALSE;
}

APPLE_AGX_BOOL AppleAgxExp208FramebufferUnbind(
    APPLE_AGX_EXP208_RELOCATION_OBJECT *Objects,
    APPLE_AGX_U32 ObjectCount,
    const APPLE_AGX_EXP208_FRAMEBUFFER_BINDING *Binding) {
  unsigned char *arena;
  unsigned char *command3d;
  unsigned char *commandTa;
  unsigned char *pbe;
  if (!AppleAgxExp208FramebufferCanUnbind(
          Objects, ObjectCount, Binding))
    return APPLE_AGX_FALSE;
  arena = (unsigned char *)Binding->ArenaCpuAddress;
  command3d = Objects[FRAMEBUFFER_COMMAND_3D_OBJECT].Data;
  commandTa = Objects[FRAMEBUFFER_COMMAND_TA_OBJECT].Data;
  pbe = Objects[FRAMEBUFFER_PBE_OBJECT].Data + 0x3000u;
  Objects[FRAMEBUFFER_TPC_OBJECT] = Binding->OriginalTpc;
  Objects[FRAMEBUFFER_TILEMAP_OBJECT] = Binding->OriginalTilemap;
  Objects[FRAMEBUFFER_CLUSTER_TILEMAP_OBJECT] =
      Binding->OriginalClusterTilemap;
  FramebufferRestoreCapturedGeometry(command3d, commandTa, pbe, Binding);
  FramebufferZero(arena + FRAMEBUFFER_TPC_OFFSET,
                  FRAMEBUFFER_SCRATCH_END - FRAMEBUFFER_TPC_OFFSET);
  return APPLE_AGX_TRUE;
}

#undef FRAMEBUFFER_NULL
