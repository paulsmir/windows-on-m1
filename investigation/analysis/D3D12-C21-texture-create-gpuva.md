# Texture creation follows native resource ownership

WINDOWS CONTRACT: GetGPUVirtualAddress returns zero for all textures; it is useful
for buffers. Microsoft primary: https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12resource-getgpuvirtualaddress
Existing pinned WDK26100 CreateHeapAndResource does not make buffer GPUVA a texture
creation success condition. This is resource admission in the existing FULL GRAPHICS
0003 opt-in profile, with no caps change.

AGX/ASAHI CONTRACT: resource_create_with_modifiers returns a native resource or NULL.
Actual project AgxWin32AsahiBufferGpuAddress explicitly returns zero before accessing
BO/map for non-PIPE_BUFFER. The existing Texture2D constructor accepts this zero.
ResourceIdentity/OwnerBufferAllocation use the native resource/BO. Copy and view
paths retain native resource handles; texture heaps bypass buffer RegisterHeap.
No native helper semantics, physical mappings, firmware or KMD allocation change.

TRANSLATION: retain a successfully created native texture and its zero public
buffer GPUVA. Remove only the erroneous zero-VA rejection in 1D/3D constructors.
NULL factory results remain E_OUTOFMEMORY. All layout, format and size checks stay.
The native owner owns resource creation/lifetime; KMD still owns native allocation,
GPUVA maps and submission. Recovery stays the exact GPU-visible controller.

EXP1426 evidence: all five C21 public allocation results now correct, then native
Create DDI, owner and final allocation S_OK followed by runtime 8007000e. Four
allocations are131072, D65536; ordinary map E_PENDING also occurs for valid resources.
Probe creates volume first; auxiliary buffer failure would report E_FAIL. Source939c
therefore identifies the false owner predicate after successful native allocation.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? An actual nonbuffer helper returns
zero and a valid texture must still be retained. Old tests returned a fictitious
positive VA for every target, hiding the failure. The new test composes actual
current scene helper and constructors, with explicitly controlled query/factory/
buffer-map boundaries. These are not native allocation or layout implementations.

Under BEFORE6e3fd9e2 exact4argv returned0,1,0,0: RED11checks3failures; GREEN11zero.
NULL factory failure remains OOM, refused layout does not allocate, actual helper
never reaches map for textures, and buffer-positive/null controls remain intact.
ASan/UBSan stderr empty; all16local+12external pins unchanged. Manifest
c3c46d581c75edef2c2d161ae458c14734e323c059e7ed9c0b4fb4e8068c65fd.
Durable CURRENT-source focused44 passed10.827s. Full1592/215.739s retains exact119
baseline failures/errors, no additions/removals,10skips; not whole-suite GREEN.

WHAT IS STILL UNKNOWN: actual public C21 Create/copy/SRV/UAV/plane RTV behavior after
removing the false admission failure. EXP1430 frozen5279bc4fcdfd04b4c3218da832a0f3e9c5e47bab
is base939c plus only3D guard removal; original54 probe, no C23/C15. Exact Windows
three-profile syntax PASS; package693sources/PDB/ARM64stack13824/WOW6544 PASS.
C23 package1427 remains unlaunched; its identical1D predicate is repaired in main,
but next C23 hardware must follow the1430 verdict under a new source/package ID.
Collect every checkpoint and regressions, then exact service-disabled GPU-visible
rollback to Code28/PackageAbsent/noService/8CPU. No GPU success claimed here.
