#define main graph_existing_test_main
#include "../drivers/apple-agx/shared/tests/apple_agx_initdata_memory_test.c"
#undef main
#include <stdio.h>
static void emit(const void *p,unsigned bytes) {
    unsigned char length[4];unsigned i;
    for(i=0;i<4;i++)length[i]=(unsigned char)(bytes>>(i*8));
    assert(fwrite(length,1,4,stdout)==4);
    assert(fwrite(p,1,bytes,stdout)==bytes);
}
int main(void) {
    FAKE_MEMORY fake;APPLE_AGX_MEMORY_IO io;APPLE_AGX_INITDATA_MEMORY_GRAPH graph;
    APPLE_AGX_CONFIG_SNAPSHOT snapshot=physical_snapshot();
    init_fixture(&fake,&io,&graph);
    assert(AppleAgxInitdataMemoryPrepareBroker(&graph,&io,&snapshot)==0);
    emit(graph.RegionBMemory.Objects[AppleAgxRegionBMemoryStatsTa].CpuAddress,J313_AGX_G2_REGIONB_STATS_TA_SIZE);
    emit(graph.RegionBMemory.Objects[AppleAgxRegionBMemoryStats3d].CpuAddress,J313_AGX_G2_REGIONB_STATS_3D_SIZE);
    emit(graph.DataObjects[AppleAgxInitdataMemoryRegionB].CpuAddress,J313_AGX_G2_INITDATA_REGION_B_SIZE);
    emit(graph.DataObjects[AppleAgxInitdataMemoryRegionC].CpuAddress,J313_AGX_G2_INITDATA_REGION_C_SIZE);
    assert(AppleAgxInitdataMemoryDestroy(&graph)==0);
    return 0;
}
