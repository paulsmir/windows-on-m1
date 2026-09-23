#include <assert.h>
#include "../include/apple_agx_gpuva_b1_graph.h"
int main(void){
 APPLE_AGX_GPUVA_B1_PAGE pages[APPLE_AGX_GPUVA_B1_GRAPH_PAGES];
 APPLE_AGX_U32 count=0, shared=0, output=0, aliases=0;
 const APPLE_AGX_RENDER_TEMPLATE_OBJECT_LAYOUT *layout=AppleAgxRenderTemplateObjectLayouts();
 assert(!AppleAgxGpuvaB1Graph(0x20001000,0x40000000,pages,302,&count));
 assert(!AppleAgxGpuvaB1Graph(0x20000000,0x40000000,pages,301,&count));
 assert(AppleAgxGpuvaB1Graph(0x20000000,0x40000000,pages,302,&count));
 assert(count==302);
 for(unsigned i=0;i<count;i++){
  assert(pages[i].ObjectIndex>=36 && pages[i].ObjectIndex<75);
  assert((pages[i].GpuVa&0x3fff)==0 && (pages[i].GuestIpa&0x3fff)==0);
  if(pages[i].Shared) shared++; else {output++;assert(pages[i].ObjectIndex==40);assert(pages[i].GpuVa==0x15001d0000ULL);assert(pages[i].GuestIpa==0x40000000ULL);}
  if(pages[i].GpuVa>=0x1100010000ULL && pages[i].GpuVa<0x1100060000ULL)aliases++;
 }
 assert(shared==301 && output==1 && aliases==17);
 assert(layout[73].OriginalGpuVa==0x1100020000ULL && layout[74].OriginalGpuVa==0x1100010000ULL);
 const APPLE_AGX_EXP208_RELOCATION *reloc=AppleAgxRenderTemplateRelocations();
 unsigned output_refs=0;
 for(unsigned i=0;i<AppleAgxRenderTemplateRelocationCount();i++)
  if(reloc[i].TargetObject==40u){
   assert(reloc[i].AddressSpace==AppleAgxExp208RelocationGpuVa);
   output_refs++;
  }
 assert(output_refs==1u);
 return 0;
}
