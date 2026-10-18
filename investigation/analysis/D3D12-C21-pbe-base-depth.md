# C21C: true 3D PBE depth at selected mip

WINDOWS CONTRACT: C21C writes a bounded committed R32_UINT volume through a
layered UAV. The selected mip has its own dimensions, and every selected Z plane
must receive the shader's expected value. EXP1430 creates the resources and
completes fences; mip0 passes, mip1 has 32 bad values starting at byte9216,
corresponding to the last two of four selected Z planes.

AGX/ASAHI CONTRACT: inspected pinned Mesa
`src/mesa/state_tracker/st_atom_image.c:112`,
`src/gallium/drivers/asahi/agx_state.c:1130`, and
`src/asahi/vulkan/hk_image_view.c:391`. The layered Gallium image view correctly
uses `last_layer = u_minify(depth0, level)-1`. Native PBE independently carries a
mip level, base width/height, and depth. Honeykrisp encodes the base image depth
for a true 3D view, while Gallium currently encodes the selected layer count.
The paired read descriptor already carries base depth.

TRANSLATION: preserve the owner image view and fix the native PBE projection.
After target normalization, use `tex->base.depth0` only for PIPE_TEXTURE_3D.
Normalized single-plane 2D and forced 2D-array views retain the selected layer
count. Initialization, allocation, addressing, runtime submission and recovery
ownership are unchanged. No m1n1, Mu, ACPI, KMD, Windows DDI or caps change is
justified by this descriptor-field mismatch.

WHAT IS STILL UNKNOWN: whether the corrected descriptor removes EXP1430's actual
mip1 GPU data failure. CPU tests can establish the descriptor mapping but cannot
establish GPU execution. EXP1436 will repeat C21C with its unchanged preceding
probe prefix, inspect exact data, and run the normal independent regressions.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A true 3D selected mip must not
have its depth reduced twice. Execute the actual pinned native read and PBE
packers with real Gallium view input data, check both mip0/mip1 and both formats,
and preserve the 18 non-3D controls plus forced-array and single-plane views.
No C++ owner execution or complete resource ABI claim is made by this fixture.

WHY THIS HYPOTHESIS: (1) only the final two mip1 Z planes fail after successful
allocation and fences; (2) the actual PBE packs depth4 and level1 whereas the read
half and Honeykrisp encode base8 and level1; (3) the owner view matches the primary
Gallium selected-range contract, placing the correction in the native packer.

Checkpoint: C21C exact data and padding pass for both mips; inspect C21A/B/D,
C14A and the remaining controls. Diagnostic-only probe order moves unchanged
C21E last so that its known incomplete execution does not hide C22/C7 results;
all 54 logical checks remain. That movement is not a shader/compiler fix.
Recovery: preserve the established GPU-visible FactoryV2 wrapper, collect all
receipts, and remove the exact EXP1436 package through the sole recovery
controller to RECOVERY_DONE/Code28/PackageAbsent/noService/8CPU.

Offline result: exact seven-command native gate returned 0/0/0/1/0/0/0.
The original PBE failed two mip1 base-depth checks, while the corrected actual
packer passed all 33 checks and retained all 18 non-3D controls. All 100 direct
and 6958 header pins remained unchanged; sanitizer stderr was empty.
Artifact manifest `.local/c21-pbe-base-depth-retry3-prep/artifact-hashes.json`
SHA-256 `4b894663688846f6f27652744644860c8b18c501fe1f25eeb17934236f5edd30`.
The current repository projector and native fixture also passed the combined
31-test PBE/ABI/owner gate in 1.088 seconds. Hardware remains required for C21C.
