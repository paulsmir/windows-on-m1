# EXP937: correct G13 RunFragment header scalar offsets

WHY THIS HYPOTHESIS:
1. EXP936 repeats exactly204800 zero pixels in large clear and large GPU blit; a CPU-filled source pixel at2432,704 survives 1x1copy. Shared import and GPUclear are not necessary causes. A common large GPU job geometry defect is now causally closer than presentation policy.
2. Actual936 native-render capture (SHA76ebf705f77cf8ecb460d5f4e89408cc5071c61aa983b88772dd0fd6ad9104b3) is2560x1600, utile32x32, sampleSize8, samples1; header input contains correct merge factors. Its source submission has one written target. No nativeinputdimension guess.
3. Current g4 builder writes mergeX/Y/tilecount at0x60/64/70. Current m1n1 G13/V13_5 Construct reflection proves0x68/6c/78; Asahi fw/fragment.rs independently specifies intervening unknown U64 fields. Materializing our actualEXP208 template shows originalmerge1.732051/16 remains at0x68/6c andtilecount1 at0x78, while the oldpatch corrupts unknownfields0x60/70. Actualconstructor ASan/UBSan regression fails onstaleheader merge beforefix.

WINDOWS CONTRACT: FULL GRAPHICS driver, RENDER_ONLY checkpoint. D3D11 CopyResource must preserve all sourcepixels, identicaldimensions/compatibleformat/unmapped resources; MapREAD must expose completed result. Microsoft CopyResource/Map/UpdateSubresource/CopySubresourceRegion documentation already inspected andlinked inEXP936 plans. No WDDM capabilities/DDIs/admission/scheduler changes.

AGX/ASAHI CONTRACT: Read Asahi77cb8f24 drivers/gpu/drm/asahi/fw/fragment.rs RunFragment andqueue/render.rs construction; m1n1_windows/proxyclient/m1n1/fw/agx/{cmdqueue,microsequence}.py andagx/render.py. Executed currentConstruct offsetof withV13_5/G13. The firmware header has unknownU64 at0x58 and0x60, mergeF32pair0x68/6c, unknownU64 at0x70, tileCountU64 at0x78. The separateembedded JobParameters2 values alreadyupdatecorrectly. MuR143/fullm1n1bdcf measured936launch/ADT/power/UAT/interruptownership unchanged; this is hostKMD serialization of documentedfirmware fields, not newhardwareinitialization.

TRANSLATION: Correct only the three erroneous store offsets in AppleAgxG4PatchRenderScalars. The values already derivefromnative Asahi2560x1600input; retain them and preserveunknown fields. UMD owns commandgeneration, KMD owns serialization/queue submission/fence; m1n1 owns existingbroker/UAT/runtime, Mu existingACPI/bootexposure. No registervalue or newDMA mapping isinvented; no external code copied.

ATOMIC CONTRACT: One structure-layout correction: mergeX F32, mergeY F32 andtileCountU64 were all shifted8bytes early in the sameRunFragment header. The corrected fields must agree with existing embeddedgeometry and must notoverwrite interveningunknownU64. Sources: AsahiRunFragment fieldorder/constructor andm1n1WorkCommand3D offsets. No unrelated capability/scheduler fields bundled.

WHAT IS STILL UNKNOWN: Whether this confirmed serialization defect causes the observedspatialcorruption. Hardwaremustdiscriminate sameCPUuploadfullreadback on937 vs936: expected0wrong of4096000, failifanywrong/APIerror/TDR. Ifpasses, sameAGXwindowedpresent andphysicalcheck; no stabilitywaituntilcorrectimage. Ifpixelsstillwrong, collect immediately andstop thishypothesis.

Additional source finding retained separately: AuxFBInfo width/height remain16x16 fromtemplate at0xb8/bc and0x6e0/e4. This is an independent omittedgeometry projection; NOT changed in937, no successassumption. Do not silentlybundleit with offset correction. It is a possible nexttarget onlyaftercurrentdiscriminator.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Actualproductionconstructor onactualmaterializedtemplate must replaceheadermerge/tilecount, preserveunknown0x60/70, andkeep embeddedcopies consistent. Oldcodefails deterministically, fixedcodepasses underASan/UBSan. Existingbuilder/broker/submit/template suites andpinnedWDK/nativeclosure/sign/hash/PDB gates beforehardware.

Recovery: freeze936originalevidence andstopitsETL beforeorderednormal377/392 recovery; exact936/oem5 cleanup then durablyproveCode28/oneGPU-visibledevnode/no package/service/signer/module. Preserveimmutable377/392 recovery andexact937manifest/hash; installonly937 aftergates. Shortoriginal936testevidencealreadyhostverified. No physicalactionuntilbothcontrolplaneschecked.
