#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../include/apple_agx_render_template_vm_slot.h"
int main(void)
{
  const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *l=AppleAgxRenderTemplateObjectLayouts();
  APPLE_AGX_RENDER_TEMPLATE_ROOTS roots;
  unsigned size=AppleAgxRenderTemplateBytes();
  unsigned char *arena=calloc(1,size), *before=calloc(1,size);
  static const unsigned object[]={16,18,19,15,15,17,17};
  static const unsigned offset[]={4,12,12,0x4c,0x284,0x34,0x22c};
  assert(arena && before && AppleAgxRenderTemplateMaterialize(arena,size,&roots));
  memcpy(before,arena,size);
  assert(!AppleAgxRenderTemplateSelectVmSlot(arena,size,0));
  assert(!AppleAgxRenderTemplateSelectVmSlot(arena,size,63));
  assert(!AppleAgxRenderTemplateSelectVmSlot(arena,size-1,1));
  assert(AppleAgxRenderTemplateSelectVmSlot(arena,size,1));
  for(unsigned i=0;i<7;i++){
    unsigned p=l[object[i]].ArenaOffset+offset[i];
    assert(before[p]==63 && arena[p]==1 && arena[p+1]==0 && arena[p+2]==0 && arena[p+3]==0);
    arena[p]=63;
  }
  assert(memcmp(arena,before,size)==0); /* no unrelated 63 was patched */
  for(unsigned choice=0;choice<2;choice++){
    unsigned slot=choice ? 62u : 17u;
    assert(AppleAgxRenderTemplateSelectVmSlot(arena,size,slot));
    for(unsigned i=0;i<7;i++){
      unsigned p=l[object[i]].ArenaOffset+offset[i];
      assert(arena[p]==slot);
      arena[p]=63;
    }
    assert(memcmp(arena,before,size)==0);
  }
  assert(AppleAgxRenderTemplateSelectVmSlot(arena,size,2));
  assert(!AppleAgxRenderTemplateSelectVmSlot(arena,size,2)); /* no double patch */
  free(arena);free(before);return 0;
}
