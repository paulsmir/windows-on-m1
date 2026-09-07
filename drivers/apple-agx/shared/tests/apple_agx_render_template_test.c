#include "apple_agx_render_template.h"
#include "apple_agx_render_template_rebase.h"
#include "apple_agx_exp208_adapter.h"

#include <assert.h>
#include <stdlib.h>

static APPLE_AGX_U64 fnv1a64(const unsigned char *bytes,
                             APPLE_AGX_U32 byteCount) {
  APPLE_AGX_U64 hash = 0xcbf29ce484222325ULL;
  APPLE_AGX_U32 index;

  for (index = 0u; index < byteCount; ++index) {
    hash ^= bytes[index];
    hash *= 0x100000001b3ULL;
  }
  return hash;
}

int main(void) {
  APPLE_AGX_U32 bytes = AppleAgxRenderTemplateBytes();
  unsigned char *arena = (unsigned char *)malloc(bytes);
  APPLE_AGX_RENDER_TEMPLATE_ROOTS roots;
  APPLE_AGX_EXP208_RELOCATION_OBJECT objects[76];
  const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *layouts;
  const APPLE_AGX_EXP208_RELOCATION *relocations;
  APPLE_AGX_EXP208_JOB_PARAMETERS parameters;
  APPLE_AGX_BACKEND_JOB_IMAGE job;
  APPLE_AGX_U32 index;
  APPLE_AGX_U32 pageRelocations = 0u;

  assert(bytes == 6094848u);
  assert(arena != NULL);
  assert(!AppleAgxRenderTemplateMaterialize(arena, bytes - 1u, &roots));
  assert(AppleAgxRenderTemplateMaterialize(arena, bytes, &roots));
  assert(roots.Ta[0] == 0x1503880000ULL);
  assert(roots.Ta[1] == 0x1503898000ULL);
  assert(roots.D3[0] == 0x1503870000ULL);
  assert(roots.D3[1] == 0x1503890000ULL);
  assert(fnv1a64(arena, bytes) == 0x031b74bcc073c5e2ULL);

  assert(AppleAgxRenderTemplateObjectCount() == 75u);
  assert(AppleAgxRenderTemplateRuntimeObjectCount() == 76u);
  assert(AppleAgxRenderTemplateArenaObjectIndex() == 75u);
  assert(AppleAgxRenderTemplateRelocationCount() == 207u);
  layouts = AppleAgxRenderTemplateObjectLayouts();
  relocations = AppleAgxRenderTemplateRelocations();
  assert(layouts != NULL);
  assert(relocations != NULL);
  assert(layouts[0].OriginalIndex == 0u);
  assert(layouts[0].Name != NULL);
  assert(layouts[0].Name[0] == 'G');
  assert(layouts[0].ContextId == 0u);
  assert(layouts[0].OriginalGpuVa == 0xffffffa0006affc0ULL);
  assert(layouts[0].OriginalPhysicalAddress == 0x80ee97fc0ULL);
  assert(layouts[0].ArenaOffset == 0u);
  assert(layouts[0].PackedGpuVa == APPLE_AGX_RENDER_TEMPLATE_GPU_BASE);
  assert(layouts[0].Size == 0x40u);
  assert(layouts[72].OriginalIndex == 72u);
  assert(layouts[72].OriginalGpuVa == 0x1600333f80ULL);
  assert(layouts[72].ArenaOffset == 0x580000u);
  assert(layouts[72].PackedGpuVa == 0x1503d80000ULL);
  assert(layouts[72].Size == 0x80u);
  assert(layouts[73].OriginalGpuVa == 0x1100020000ULL);
  assert(layouts[73].ArenaOffset == 0x588000u);
  assert(layouts[73].Size == 0x40000u);
  assert(layouts[74].OriginalGpuVa == 0x1100010000ULL);
  assert(layouts[74].ArenaOffset == 0x5c8000u);
  assert(layouts[74].Size == 0x4000u);
  for (index = 0u; index < 75u; ++index) {
    assert(layouts[index].OriginalIndex == index);
    assert(layouts[index].Name != NULL);
    assert(layouts[index].ArenaOffset % APPLE_AGX_RENDER_TEMPLATE_ALIGNMENT ==
           0u);
    assert(layouts[index].PackedGpuVa ==
           APPLE_AGX_RENDER_TEMPLATE_GPU_BASE + layouts[index].ArenaOffset);
    assert(layouts[index].Size != 0u);
    assert(layouts[index].ArenaOffset <= bytes - layouts[index].Size);
    if (index != 0u)
      assert(layouts[index - 1u].ArenaOffset + layouts[index - 1u].Size <=
             layouts[index].ArenaOffset);
  }
  assert(relocations[0].SourceObject == 1u);
  assert(relocations[0].SourceOffset == 28u);
  assert(relocations[0].TargetObject == 41u);
  assert(relocations[0].TargetOffset == 0u);
  assert(relocations[0].AddressSpace == AppleAgxExp208RelocationGpuVa);
  assert(relocations[0].Encoding == AppleAgxExp208RelocationExactU64);
  assert(relocations[206].SourceObject == 63u);
  assert(relocations[206].SourceOffset == 164u);
  assert(relocations[206].TargetObject == 41u);
  for (index = 0u; index < 207u; ++index) {
    APPLE_AGX_U32 width =
        relocations[index].Encoding ==
                AppleAgxExp208RelocationGpuVaPage32kU32
            ? 4u
            : 8u;
    assert(relocations[index].SourceObject < 75u);
    assert(relocations[index].TargetObject < 75u);
    assert(layouts[relocations[index].SourceObject].Size >= width);
    assert(relocations[index].SourceOffset <=
           layouts[relocations[index].SourceObject].Size - width);
    assert(relocations[index].TargetOffset <
           layouts[relocations[index].TargetObject].Size);
    assert(relocations[index].AddressSpace ==
               AppleAgxExp208RelocationGpuVa ||
           relocations[index].AddressSpace ==
               AppleAgxExp208RelocationPhysical);
    assert(relocations[index].Encoding <=
           AppleAgxExp208RelocationGpuVaPage32kU32);
    if (relocations[index].SourceObject == 41u &&
        relocations[index].Encoding ==
            AppleAgxExp208RelocationGpuVaPage32kU32) {
      assert(relocations[index].SourceOffset == pageRelocations * 4u);
      assert(relocations[index].TargetObject ==
             43u + pageRelocations / 4u);
      assert(relocations[index].TargetOffset ==
             (pageRelocations % 4u) * 0x8000u);
      ++pageRelocations;
    }
  }
  assert(pageRelocations == 64u);

  assert(!AppleAgxRenderTemplateBuildRelocationObjects(
      arena, bytes - 1u, 0x900000000ULL, objects, 76u));
  assert(!AppleAgxRenderTemplateBuildRelocationObjects(
      arena, bytes, 0x900000000ULL, objects, 75u));
  assert(!AppleAgxRenderTemplateBuildRelocationObjects(
      arena, bytes, ~(APPLE_AGX_U64)0 - bytes + 2u, objects, 76u));
  assert(AppleAgxRenderTemplateBuildRelocationObjects(
      arena, bytes, 0x900000000ULL, objects, 76u));
  assert(objects[0].Data == arena);
  assert(objects[0].GpuVa == APPLE_AGX_RENDER_TEMPLATE_GPU_BASE);
  assert(objects[0].PhysicalAddress == 0x900000000ULL);
  assert(objects[0].Size == 0x40u);
  assert(objects[72].Data == arena + 0x580000u);
  assert(objects[72].GpuVa == 0x1503d80000ULL);
  assert(objects[72].PhysicalAddress == 0x900580000ULL);
  assert(objects[72].Size == 0x80u);
  assert(objects[73].Data == arena + 0x588000u);
  assert(objects[73].GpuVa == 0x1503d88000ULL);
  assert(objects[73].Size == 0x40000u);
  assert(objects[74].Data == arena + 0x5c8000u);
  assert(objects[74].GpuVa == 0x1503dc8000ULL);
  assert(objects[74].Size == 0x4000u);
  assert(objects[75].Data == arena);
  assert(objects[75].GpuVa == APPLE_AGX_RENDER_TEMPLATE_GPU_BASE);
  assert(objects[75].PhysicalAddress == 0x900000000ULL);
  assert(objects[75].Size == bytes);
  parameters.ArenaGpuAddress = APPLE_AGX_RENDER_TEMPLATE_GPU_BASE;
  parameters.ArenaBytes = bytes;
  parameters.TaEvent = 1u;
  parameters.D3Event = 2u;
  parameters.TaExpectedStamp = 3u;
  parameters.D3ExpectedStamp = 4u;
  parameters.TaExpectedDonePointer = 5u;
  parameters.D3ExpectedDonePointer = 6u;
  assert(AppleAgxExp208BuildJob(
      &parameters, objects, AppleAgxRenderTemplateRuntimeObjectCount(),
      relocations, AppleAgxRenderTemplateRelocationCount(), &job));
  assert(job.TaWorkAddressCount == APPLE_AGX_RENDER_TEMPLATE_ROOT_COUNT);
  assert(job.D3WorkAddressCount == APPLE_AGX_RENDER_TEMPLATE_ROOT_COUNT);
  assert(job.TaWorkAddresses[0] == roots.Ta[0]);
  assert(job.TaWorkAddresses[1] == roots.Ta[1]);
  assert(job.D3WorkAddresses[0] == roots.D3[0]);
  assert(job.D3WorkAddresses[1] == roots.D3[1]);

  assert(AppleAgxRenderTemplateMaterialize(arena, bytes, &roots));
  assert(AppleAgxRenderTemplateBuildRelocationObjectsRebased(
      arena, bytes, 0x9d0800000ULL, 0x1500800000ULL,
      0x1500000000ULL, 0x01000000ULL, objects, 76u, &roots));
  parameters.ArenaGpuAddress = 0x1500800000ULL;
  assert(AppleAgxExp208BuildJob(
      &parameters, objects, AppleAgxRenderTemplateRuntimeObjectCount(),
      relocations, AppleAgxRenderTemplateRelocationCount(), &job));
  assert(job.TaWorkAddresses[0] == 0x1500880000ULL);
  assert(job.TaWorkAddresses[1] == 0x1500898000ULL);
  assert(job.D3WorkAddresses[0] == 0x1500870000ULL);
  assert(job.D3WorkAddresses[1] == 0x1500890000ULL);
  parameters.ArenaGpuAddress = 0x1500880000ULL;
  assert(!AppleAgxExp208BuildJob(
      &parameters, objects, AppleAgxRenderTemplateRuntimeObjectCount(),
      relocations, AppleAgxRenderTemplateRelocationCount(), &job));
  free(arena);
  return 0;
}
