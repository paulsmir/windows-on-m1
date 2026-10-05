# EXP936 CPU-written source, single-pixel copy

WHY THIS HYPOTHESIS: PrivateCPUupload succeeds butfullcopy givesidentical5percentzero. EarlierGPUclearedsource pixel2432,704 returnedzero via1x1copy. Comparethe same1x1copy onCPU-writtensource to distinguishsourceclear/store fromcoordinate sampling. No unchangedstimulus.

WINDOWS CONTRACT: RENDER_ONLY. ExistingDEFAULT privateBGRA UpdateSubresource, eventcomplete, boundedCopySubresourceRegionbox andMapREAD. ExactMicrosoftcontracts inupload/pointplans.

AGX/ASAHI CONTRACT: Native tiledCPUupload viaail_tile andcanonicalupload; 1x1nativeblit andCPUdownload; nofirmware/driverchange, same936measuredboot.

TRANSLATION: CombineexistingCPUupload and1x1readback flags only. Green point despitefullcopyzeros supportslargeGPUrender destination/store fault; zero leaves sampling orcopy/readback. Results mustbe comparedwithsame-operation controls, notphysicaldesktopclaim.

WHAT IS STILL UNKNOWN: DoesknownsourcepixelremaincorrectwhenlargeGPUrender outputisremoved?25sbound, immutable377/392 recovery; pinnedSDK build/hash/PEbeforehardware. Diagnosticonly, noartificialRED.
