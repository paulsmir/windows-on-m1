# G3 system-process admission after EXP776

EXP776 cold-boot DxgKrnl ETW records `STATUS_INVALID_PARAMETER` from the KMD,
then the diagnostic "Failed to create KMD process handle for system process"
0.008 ms later, before AddAdapter Event549. StartDevice was complete, the
driver's 3D node and segments were reported, and the DDI registration includes
`DxgkDdiCreateProcess`. This narrows the failure to creation of dxgkrnl's
system KMD process handle; the exact guard input was not recorded in EXP776.

Pinned WDK26100 `d3dkmddi.h:5150-5205` defines `SystemProcess`, the input
`NumPasid`/`pPasid` array, and the output `hKmdProcess`. [Microsoft Learn on
`DXGKARG_CREATEPROCESS`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_createprocess)
defines `NumPasid` as the array count with one identifier per physical GPU;
[the DDI contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_createprocess)
requires the KMD to return a process object handle on success. Neither makes
a nonzero PASID count an unsupported operation. Our GPU MMU process graph uses
its own root and does not read the PASID array. The old `Args->NumPasid != 0`
precondition was therefore a driver-owned admission veto. Other preconditions
(non-null adapter/arguments, started adapter, PASSIVE_LEVEL) remain. A new
input receipt records flags, PASID count/array presence and started/IRQL state
before any allocation so the next run can verify this inference directly.

The host regression first rejected the old guard, then accepted the corrected
path; the pinned WDK build is the compile gate. The next falsifiable hardware
checkpoint is a hash-pinned package update inside the R54 full-owner series,
G3 arm set once, and one cold reboot. Expect system-process CreateProcess to
return a KMD handle or expose its next exact failure before any GPU job. The
immutable hidden/ordinary recovery remains available if SSH or package
identity is lost.
