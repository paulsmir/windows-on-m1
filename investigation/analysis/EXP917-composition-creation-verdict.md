# EXP917: a displayable BGRA buffer is rejected before allocation

The creation-only probe has a conclusive failure boundary. It is not yet proof
that this is the sole cause of the black desktop.

Original boot `2026-10-01T19:15:17.8978310Z`, exact package30.0.916.0,
reached771.723243s Code0/CPU8 with DWM1220 and Explorer5236. Current DCP
swap10/seq2/IOVA102a0000/PA8e0110000 remained zero at120/300/600s.
No prior scheduler crash was observed. Accepted package remains NONE.

## Exact discriminator

After baseline, SDK probe `CompositionSwapchainProbe.exe` SHA
`cd7d043e4b598e6f11006df672ac2e12973aa89cefa49264403e8910fac880d8`
ran once as PID4284 in console session1, on AppleLUID00000000:0003dd7e.
Output0 was attached at2560x1600. D3D11CreateDevice at FL10.0 returnedS_OK.
CreateSwapChainForComposition returned `0x887a0005` (DXGI_ERROR_DEVICE_REMOVED).
Task result/exit8; exact scheduled task removed; output576B SHA
`120c96c440e7a7b538d93eaeb610648b8cbb1d74d5803273ac38fa301f2ad696`
independently verified on the host. No Present, visual binding or mode change.

The same PID's original UMD log shows:

- native interface0x000a0006/version0x177a, NO_REDIRECTION0x087a0004;
- frontend-create: format87, dimension3, usage0, bind0xa8, map0,
  misc0x20002, one mip/layer/sample,2560x1600x1, no primary or initial data;
- g4-create-resource-enter, then reject-primary80070057;
- runtime-set-error80070057, g4-create-resource-exit80070057;
- CreateResource line472 SetError80070057.

The guest remained Code0 after the probe. This is runtime device loss following
an explicit UMD validation failure, not a kernel bugcheck or a proven GPU hang.
Pinned WDK26100 identifies misc0x20002 as SHARED2|DISPLAYABLE_SURFACE20000;
bind0xa8 is SRV8|RT20|PRESENT80. The production validator omits DISPLAYABLE
from its allowed mask. Other predicates of this exact request pass.

[Microsoft's DDI flag definition](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d10umddi/ne-d3d10umddi-d3d10_ddi_resource_misc_flag)
defines this marker starting in Windows10. It describes an existing displayable
surface. Do not confuse it with the separately advertised Windows11 flexible
presentation feature. The existing BGRA descriptor already returns
Linear1/Displayable1/localSegment2, with16-byte pitch alignment and64KiB backing
alignment, and imports the same buffer into Asahi with explicit linear layout.

## Owning correction and limits

Commit `b5291548` accepts this marker for the existing displayable BGRA path,
while rejecting it for the non-displayable RGBA path and preserving all other
resource, flag, format and lifetime checks. No DDI version, capability,
NO_REDIRECTION, firmware, scheduler or allocation layout change.

The actual production validator replay fails before the fix at the exact
captured tuple. After the fix it passes under ASan/UBSan, produces the identical
storage descriptor as the already accepted unmarked BGRA request, and rejects
unsupported flags/shapes/formats. The existing BLT source eligibility test also
passes. A pinned-WDK native contract case checks the same tuple and enum values.
EXP918 package source `d5c685596bb3073b758d43385177561dee50b5be` is built to
validate this correction using the same post-baseline probe.

This narrows and supersedes the **next-action recommendation** to start a broad
modern-UMD rewrite from EXP916. The documented completeness gap still exists,
but this observed, deterministic rejection can be fixed independently. The
captured DWM1220 log does not show this exact reject-primary call; do not claim
its causality for the entire desktop before hardware verification.

## Evidence and rollback

Root artifacts `.local/experiments/EXP917-composition-create/`:
- `composition-create-host-gate.json`, `composition-create.out`;
- `probe-umd-receipt.out` and final `hardware-evidence-original/umd.log`;
- 11 complete periodic snapshots /170 frozen files, host gate
  `e6d91d0e82870d2eb0ce348debbe2c8b02f4c615143533343cd2473b9540c6c6`;
- original collector16 files including manifest, host gate
  `0c516d385238ebdc53ba5388509beafbefd74e283c5f2d4e5ddfc06fcb140592`;
- ordinary Code43 recovery15 files including manifest, host gate
  `449c38ea7e56b4a199f0f6d4d99131aeff7e01fb7a01c24c7fceb4a52f228a53`;
- `displayable-RED.log`, `displayable-GREEN.log`, and full-suite exact-ID comparison.

All original and recovery host gates preceded cleanup. Original ordered restart
completed without forced signal. Ordinary EXP377/392 recovery boot19:38:36.353964Z
reached Code43/Start2/Stopped/armnull/CPU8. Owned diagnostics were removed.
Only two exact SHA/size-verified guest ETL duplicates were deleted after keeping
host copies, restoring4534366208 free bytes/shadows0 before exact package cleanup.
Final clean recovery receipts follow when complete.

Final ordinary recovery boot `2026-10-01T19:49:24.5391140Z` passed checks
at19:50:07 and19:50:59: one APPL0002 Code28, no package/module/service/signer/
arm/diagnostics, CPU8/disks2/USB5, SSH and RDP service, autologon1.
Free C4550606848 bytes, ShadowCount0. Three cleanup receipts passed independent
host size/SHA verification, host gate
`50f00685bf0d9ca327da421671f9e744f84175e33cae6c2c335dd6545b678e9c`.
No hidden boot or forced reset. EXP917 is closed; EXP918 not staged at closure.
