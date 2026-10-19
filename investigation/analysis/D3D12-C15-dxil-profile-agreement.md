WINDOWS CONTRACT: initial owned compute admission is shaderModel6.0 and DXIL1.0, both header and actual module must agree; generic framer remains semantic-neutral. No publicSM6/caps/DDI change.
AGX/ASAHI CONTRACT: no GPU owner changes at this boundary; existing actual owned SPIR-V/NIR semantics remain controls.
TRANSLATION: new original typed selected-CAPI metadata query, checked exception facade and owned admission before resource conversion; never trust header alone or infer actual model from compute stage.
WHAT IS STILL UNKNOWN: actual old acceptance of realcs6.1module with masked framing and new refusal; source-only hypothesis until actual DXC/old-new converter evidence. Hardware not used to guess deterministic metadata policy.

# C15 next boundary: actual DXIL header/module profile agreement

SOURCE ONLY, Root review pending. EXP1532fixture/host,1533nativebuild and1534CPU are RESERVED BY ROOT, not executed or approved here. No Windows DDI/caps/SM6/publicmodel/driverpackage/hardware change is proposed. The new boundary is independent of active1530 and all blocked compiler tasks.

## Evidence and actual narrow defect candidate

Current agx_dxil_convert_owned restricts framing header to kind5/model6.0, copies only its bitcode and calls checkedparse. checkedstage asks only for compute; it cannot establish module modelmajor/minor or DXILversion. Public selecteddxil_spirv_c.h has stage getters but no profile/version metadata getter. Converter::Impl::get_shader_model at3191 reads actual dx.shaderModel and major/minor; it is an internal C++ method, not exportedCAPI. Consequently a numeric PROGRAMheader6.0 can conceal a different actual modulemodel. This is SOURCE evidence, not an observed functionalRED yet. The generic parser intentionally reports headerDXILversion without semantic validation; change admission, NOT genericframing or its trailing-byte/version regression semantics.

## Primary Microsoft contract and profile policy

All Microsoft primary source uses DirectXShaderCompiler194bb75bea2becb42810d72a16195089c1908e10:

* docs/DXIL.rst versioning61–113: shader model and DXILversion are separate named module metadata; dx.version occurs once. Earlier inspected SHA c79fe2a8917c16da8141d960615b9e32c60c9678a123ba3c9819e41aab03b162.
* include/dxc/DxilContainer/DxilContainer.h99–112 and547–559: PROGRAMheader encodeskind+model; bitcodeheader separately storesDXILversion. SHA f9975fb71c62f1aa37cba989adc83f7dcbb06d48e36a62a0b2ebf29c26eaa397.
* include/dxc/DXIL/DxilConstants.h32–42: packedDXILversion uses major shifted8 plusminor; correspondingdecoders select8bitcomponents. This independently proves1.0 encodes0x100; it is NOT inferred from the old fixture.
* lib/DXIL/DxilShaderModel.cpp299–311: GetDxilVersion choosesmajor1 and minor0 forSM6.0, minor1 forSM6.1; currentSMtable602–603 listscompute6.0/6.1. This establishes the selected profile pair. It is not a claim that everybackward-compatibleDXILversion combination is invalid universally.

Primary URLs (source reading only, no Microsoftimplementation adoption):
https://github.com/microsoft/DirectXShaderCompiler/blob/194bb75bea2becb42810d72a16195089c1908e10/docs/DXIL.rst
https://github.com/microsoft/DirectXShaderCompiler/blob/194bb75bea2becb42810d72a16195089c1908e10/include/dxc/DxilContainer/DxilContainer.h
https://github.com/microsoft/DirectXShaderCompiler/blob/194bb75bea2becb42810d72a16195089c1908e10/include/dxc/DXIL/DxilConstants.h
https://github.com/microsoft/DirectXShaderCompiler/blob/194bb75bea2becb42810d72a16195089c1908e10/lib/DXIL/DxilShaderModel.cpp

Initial ownedprofile remains conservative compute6.0/DXIL1.0 only. After completeframing, require headerkind5/model6.0/DXIL0x100 and actual module namecs/model6.0/dx.version1.0. A later supported profile can be designed independently; no publicSM6conformance follows from this agreement check. This does not replace DXILsemantic validation, resource/capability/wave admission or publicDDIreadability contract.

## Narrow original typed query and accessor

Add one originalCAPI compute-profile query in existing dxil_spirv_c.cpp, where actual dxil_spv_parsed_blob_s owns LLVMBCParser bc. Output is four uint32 scalars: shadermajor/minor and DXILmajor/minor. No opaque casts, private-layout reconstruction, borrowed strings/module pointers or new parser. Clearoutput before metadata access; localcandidate publishlast on success. Wrongstage/profile returns reportedunsupported; missing/incompatible metadata returns reportedvalidation/error. ExistingAPI remains unchanged.

Builtin ownership is LLVMBC. Actual MDNode::getOperand returns a reference after dereferencing its storedpointer (metadata.cpp65–68); actual module.cppNODE stores get_metadata results, which may be null. The public MDNode API currently has no nullablepointer accessor. Therefore add only `const MDOperand *getOperandOrNull(unsigned) const` in metadata.hpp/.cpp, boundschecking and returning the storedpointer. No field/vtable/layout/allocator/BCABI changes; old getOperand behavior stays unchanged. Query checks namednode count/nodeoperand count/null/type/i32 scalar before reading. This is defensive ordinary API behavior; fixture scope is ONLY actual validated DXC modules, no malformed metadata synthesis, fuzzing or fault sweeps.

Expose query through one new original checkednoexceptfacade function (20usedcalls, old19unchanged). Put actual module check in ownedadapter after checkedparse/stage, BEFORE entry/resource scan/convertercreate. Profiledisagreement=>UNSUPPORTED/empty result. Clear wrapper output on any reportederror; no genericOOMrecovery claim. All job/TLS lifetime/cleanup remains currentactualcode. Resource maps and existing nominal384word semantics remain unchanged.

External selectedMIT licenses retain fullnotices; no copied externalbody in Git. Private candidate delta must be original edits to exactly CAPIheader/cpp+bcmetadataheader/cpp against existing accepted1421 composite pin, with all other bytes identical. Fresh83TUs/9archives required since metadata.hpp reaches the graph; no target/dependency change/newlibrary. Existing `/EHsc-`/hostexceptions/MD policies remain. Project delta is checkedheader/cpp+owned.c and originaltestfiles only. Runtime/GPU linking is NOT adopted in this checkpoint.

## Deterministic fixture and expected causal result

Existing authored HLSL fixture is copied byte-identically as profile_agreement.hlsl:64threads, one rawUAV, x<64, store3*x+7. fixture-commands-proposed.json pins existingSDKDXC1.8.2502.11 and its two adjacentDLLs; exact two compile-only calls cs_6_0/cs_6_1 keep validation enabled. Root must BEFORE+pre/post tools/sourcehashes, fresh root, owned60sprocess+rawcapture; no SDKredistribution. No existing actualcs6.1binary was identified in the bounded source pass, so new outputhashes remain PENDING until actual compile.

Use actual container bytes with existing genericframer to extract original PROGRAM; keep all LLVMbitcode bytes unchanged. Controls: realcs6.0header/module succeeds; unmodified realcs6.1header is unsupported underexisting6.0profile. Causalnegative: take the actualcs6.1 PROGRAM, replace ONLY numeric ProgramVersion withcompute6.0 and headerDXILversion with0x100, assert everybitcodebyte and all otherheaderbytes unchanged. Oldadapter may accept (hypothesis to observe); new query must report module6.1/1.1 and refuse. Never call this a RED until actualoldsemantic result is collected. Also mutate ONLY cs6.0headerDXILversion to1.1 as an independent header/module version mismatch; actualLLVM remains the exactsame validmodule. No mock metadata or inventedbitcode.

After each rejectedjob, next validjob samecallerthread succeeds; outputempty checks are semanticfields, not paddingtricks. Keep current realowned43/old79/384word/wrapper controls; no decoder/resourcemap policy changes. Verify inputimmutable, mapcount1/UAV0 slot0, workgroup64,1,1 and ownedSPIRV/nonempty nominal. Fullconverter fixture and actual query getter both checked on realmodules; no fakecompiler. Originalprofileresults remain scalarCPU observations, no shaderexecution.

## Sequence and scope estimate

1532 begins with two boundedactualDXC outputs/data extraction, then Root-qualified hostold/new83graph/9libs and actualadapterfixture (no productionlink). Compilation scope is same83TUtargets+newqueryfacade and threeexistingprojectunits; no newthirdparty source/license graph. Source preparation is small4externalfile+3projectfile delta; initial portablefixture is oneoriginal Ccaller. Native1533 then compiles fresh83/9 with existing effectiveEH/CRT/header gates and original Ccaller link. CPU1534 only after1530RECOVERY_DONE/noPackage/normalinertguards and its own BEFORE, no GPU/D3D/driver APIs. Actual public DXILinput/DDIprivatecapacity/caps/rootprovider admission remains OPEN and is not re-investigated by this step. No hardwareprobe needed to distinguish metadataagreement; a deterministic CPUresult closes this boundary.
