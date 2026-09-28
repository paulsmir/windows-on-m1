
  /* R152 offline probe: two legal address placements, no trace whitelist. */
  {
    const APPLE_AGX_U64 first_page=0x3100000ULL, second_page=0x6100000ULL;
    const APPLE_AGX_U64 first_block=0x3120000ULL, second_block=0x6120000ULL;
    unsigned char queue_before[96];
    source_objects[41].GpuVa=first_page;
    source_objects[42].GpuVa=first_block;
    assert(AppleAgxRenderSharedMemoryBuildActiveG4Job(
        &owner,arena,APPLE_AGX_EXP208_ARENA_BYTES,source_objects,
        APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,0x1500800000ULL,
        APPLE_AGX_TRUE,&bindings,&staged_job,active_objects,&active_job));
    assert(read_u64(active_objects[1].Data+28u)==first_page);
    assert(read_u64(active_objects[1].Data+56u)==first_block);
    memset(active_objects[24].Data,0x5a,sizeof(queue_before));
    memcpy(queue_before,active_objects[24].Data,sizeof(queue_before));
    source_objects[41].GpuVa=second_page;
    source_objects[42].GpuVa=second_block;
    assert(AppleAgxRenderSharedMemoryBuildActiveG4Job(
        &owner,arena,APPLE_AGX_EXP208_ARENA_BYTES,source_objects,
        APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,0x1500800000ULL,
        APPLE_AGX_FALSE,&bindings,&staged_job,active_objects,&active_job));
    printf("second manager: page=%llx expected=%llx block=%llx expected=%llx queue_preserved=%u\n",
        (unsigned long long)read_u64(active_objects[1].Data+28u),
        (unsigned long long)second_page,
        (unsigned long long)read_u64(active_objects[1].Data+56u),
        (unsigned long long)second_block,
        (unsigned)(memcmp(queue_before,active_objects[24].Data,sizeof(queue_before))==0));
    fflush(stdout);
    if (getenv("R152_REQUIRE_CURRENT")) {
      assert(read_u64(active_objects[1].Data+28u)==second_page);
      assert(read_u64(active_objects[1].Data+56u)==second_block);
    }
    assert(read_u64(active_objects[1].Data+28u)==first_page);
    assert(read_u64(active_objects[1].Data+56u)==first_block);
    assert(memcmp(queue_before,active_objects[24].Data,sizeof(queue_before))==0);
    /* A naive InitBM retry also initializes persistent queue-owned bytes. */
    assert(AppleAgxRenderSharedMemoryBuildActiveG4Job(
        &owner,arena,APPLE_AGX_EXP208_ARENA_BYTES,source_objects,
        APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,0x1500800000ULL,
        APPLE_AGX_TRUE,&bindings,&staged_job,active_objects,&active_job));
    printf("naive InitBM: page=%llx queue_preserved=%u\n",
        (unsigned long long)read_u64(active_objects[1].Data+28u),
        (unsigned)(memcmp(queue_before,active_objects[24].Data,sizeof(queue_before))==0));
    assert(read_u64(active_objects[1].Data+28u)==second_page);
    assert(memcmp(queue_before,active_objects[24].Data,sizeof(queue_before))!=0);
  }
