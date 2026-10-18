  {
    APPLE_AGX_RENDER_MANAGER_STATE a={0}, b={0}, foreign={0};
    APPLE_AGX_RENDER_MANAGER_KEY ka={0}, kb={0};
    unsigned char pointers[96], event_control[176], info[188], control[64];
    APPLE_AGX_U32 counter;
    ka.Owner=17;ka.Generation=4;ka.RootIpa=0x30000000ULL;
    ka.Backing[0]=(APPLE_AGX_G4_PROCESS_RANGE){0x3100000ULL,0x10000u,0u};
    ka.Backing[1]=(APPLE_AGX_G4_PROCESS_RANGE){0x3120000ULL,0x10000u,0u};
    ka.Backing[2]=(APPLE_AGX_G4_PROCESS_RANGE){0x3200000ULL,0x400000u,0u};
    kb=ka;kb.Owner=23;kb.Generation=9;kb.RootIpa=0x40000000ULL;
    kb.Backing[0].Va=0x6100000ULL;kb.Backing[1].Va=0x6120000ULL;
    kb.Backing[2].Va=0x6200000ULL;
#define SOURCE(k) do { source_objects[41].GpuVa=(k).Backing[0].Va; \
    source_objects[42].GpuVa=(k).Backing[1].Va;source_objects[43].GpuVa=(k).Backing[2].Va; } while(0)
#define BUILD(m,k,first,init,f) AppleAgxRenderSharedMemoryBuildManagedG4Job( \
    &owner,&(m),&(k),(f),(first),arena,APPLE_AGX_EXP208_ARENA_BYTES,source_objects, \
    APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,0x1500800000ULL,(init), \
    &bindings,&staged_job,active_objects,&active_job)
    SOURCE(ka);
    assert(AppleAgxRenderManagerNeedsBind(&owner,&ka));
    assert(BUILD(a,ka,APPLE_AGX_TRUE,APPLE_AGX_TRUE,80u));
    assert(read_u64(active_objects[1].Data+28u)==ka.Backing[0].Va);
    assert(read_u32(active_objects[21].Data)==1u);
    memset(active_objects[24].Data,0x5a,sizeof(pointers));
    memcpy(pointers,active_objects[24].Data,sizeof(pointers));
    memcpy(event_control,active_objects[11].Data,sizeof(event_control));
    /* Emulate firmware mutation; A's state must survive B's ownership. */
    active_objects[1].Data[0]=77u;
    active_objects[20].Data[4]=11u;
    memcpy(info,active_objects[1].Data,sizeof(info));
    memcpy(control,active_objects[20].Data,sizeof(control));
    assert(!BUILD(b,kb,APPLE_AGX_FALSE,APPLE_AGX_TRUE,81u));
    assert(!AppleAgxRenderManagerSave(&owner,&a,81u,active_objects));
    assert(!AppleAgxRenderManagerSave(&owner,&foreign,80u,active_objects));
    assert(AppleAgxRenderManagerSave(&owner,&a,80u,active_objects));
    assert(AppleAgxRenderManagerSave(&owner,&a,80u,active_objects));
    SOURCE(kb);
    assert(!BUILD(b,kb,APPLE_AGX_FALSE,APPLE_AGX_FALSE,81u));
    assert(BUILD(b,kb,APPLE_AGX_FALSE,APPLE_AGX_TRUE,81u));
    assert(read_u64(active_objects[1].Data+28u)==kb.Backing[0].Va);
    assert(read_u64(active_objects[1].Data+56u)==kb.Backing[1].Va);
    assert(memcmp(pointers,active_objects[24].Data,sizeof(pointers))==0);
    assert(memcmp(event_control,active_objects[11].Data,sizeof(event_control))==0);
    assert(read_u32(active_objects[21].Data)==1u);
    assert(AppleAgxRenderManagerSave(&owner,&b,81u,active_objects));
    SOURCE(ka);
    assert(BUILD(a,ka,APPLE_AGX_FALSE,APPLE_AGX_TRUE,82u));
    assert(memcmp(info,active_objects[1].Data,sizeof(info))==0);
    assert(memcmp(control,active_objects[20].Data,sizeof(control))==0);
    assert(read_u32(active_objects[21].Data)==2u);
    assert(AppleAgxRenderManagerSave(&owner,&a,82u,active_objects));
    /* Same manager, new scene: Scene/user_buffer changes, Info stays owned. */
    source_objects[72].GpuVa=0x7200000ULL;
    active_objects[13].Data[0]=0xffu;
    assert(!AppleAgxRenderManagerNeedsBind(&owner,&ka));
    assert(BUILD(a,ka,APPLE_AGX_FALSE,APPLE_AGX_FALSE,83u));
    assert(read_u64(active_objects[13].Data+24u)==source_objects[72].GpuVa);
    assert(active_objects[13].Data[0]==source_objects[13].Data[0]);
    assert(read_u32(active_objects[21].Data)==3u);
    assert(AppleAgxRenderManagerSave(&owner,&a,83u,active_objects));
    /* Identical VAs under another owner/root still need InitBM. */
    kb.Backing[0]=ka.Backing[0];kb.Backing[1]=ka.Backing[1];kb.Backing[2]=ka.Backing[2];
    assert(AppleAgxRenderManagerNeedsBind(&owner,&kb));
    assert(BUILD(b,kb,APPLE_AGX_FALSE,APPLE_AGX_TRUE,84u));
    assert(read_u32(active_objects[21].Data)==1u); /* new backing */
    assert(AppleAgxRenderManagerSave(&owner,&b,84u,active_objects));
    ++kb.Generation;
    assert(AppleAgxRenderManagerNeedsBind(&owner,&kb));
    assert(BUILD(b,kb,APPLE_AGX_FALSE,APPLE_AGX_TRUE,85u));
    assert(read_u32(active_objects[21].Data)==1u);
    assert(AppleAgxRenderManagerSave(&owner,&b,85u,active_objects));
    counter=read_u32(active_objects[21].Data);
    kb.RootIpa+=0x4000ULL;
    assert(AppleAgxRenderManagerNeedsBind(&owner,&kb));
    assert(BUILD(b,kb,APPLE_AGX_FALSE,APPLE_AGX_TRUE,86u));
    assert(read_u32(active_objects[21].Data)==counter+1u);
    assert(AppleAgxRenderManagerSave(&owner,&b,86u,active_objects));
    assert(memcmp(pointers,active_objects[24].Data,sizeof(pointers))==0);
    assert(memcmp(event_control,active_objects[11].Data,sizeof(event_control))==0);
#undef BUILD
#undef SOURCE
  }
