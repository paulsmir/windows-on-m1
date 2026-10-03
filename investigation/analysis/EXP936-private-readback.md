# EXP936 ordinary texture control

WHY THIS HYPOTHESIS: Fullframe and 1x1 readback of sharedsource2432,704 bothzero; cross-process open notnecessary. Direct shared import wraps a linear externally-ownedGPU allocation and cannot CPUmap. Ordinary native textures use Asahi allocation/layout. Removing onlySHARED distinguishes these source paths with unchanged clear/copy/map.

WINDOWS CONTRACT: RENDER_ONLY. RemoveD3D11_RESOURCE_MISC_SHARED fromDEFAULT BGRA2560x1600 RT|SRV creation. Same clear/event/fullstagingcopy/map checks. No presentation/capabilitycontractchange.

AGX/ASAHI CONTRACT: inspected AgxWin32AsahiImportLinearColor32 and agx_pipe.c resource modifier selection/transfer_map. SharedDirectsource islinear; ordinarynative defaults tiled uncompressed undercurrentprofile and owns canonical+staging backing. Same936m1n1/Mu/DART/IRQ/powercontract. This control distinguishes allocation/layout families; it does not isolate oneofthoseinternaldifferences byitself.

TRANSLATION: --local-private uses sameproducer/readback withMiscFlags0. If healthy, focussharedlinearimport andclear/sampler descriptor; ifsamecorruption, focuscommontranslation/store. Optional --local-private-upload built for latercounterfactual, no rununtil separatelypreregistered basedonresult.

WHAT IS STILL UNKNOWN: Whether corruption requires sharedDirectlinear allocation.25sbound/exactoriginal936boot/packagegates/recovery377392 unchanged. Hardwareonly diagnostic, noartificialRED.
