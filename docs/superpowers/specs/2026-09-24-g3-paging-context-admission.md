# EXP777B G3 paging context admission

## Evidence and ownership

EXP777B cold boot passed G3 CreateProcess with `NumPasid=1`, built the system GPU VA allocator, then DxgKrnl ETW recorded KMD `STATUS_NOT_SUPPORTED` and "Paging context 0 creation failed" before AddAdapter returned `C0000001`. The KMD owns `DxgkDdiCreateContext`; no AGX queue, page-table DDI, or Mesa path had run at this boundary. Frozen m1n1 full-owner and Mu contracts remain unchanged. Evidence: `.local/experiments/EXP777-addadapter-process/hardware-evidence/bound-cold/EXP777BDxgBoot.etl` SHA256 `613824334172700f1b34007e12258c0fd4272ef564778ac434b8e698bf1e6512`.

Pinned WDK 26100 `d3dkmddi.h:1515-1544` defines `DXGK_CREATECONTEXTFLAGS.VirtualAddressing` as bit 2. [Microsoft Learn's CreateContext flags](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_createcontextflags) says that bit identifies contexts using virtual addressing and `SystemContext` denotes the paging-engine context. [CreateContext DDI](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_createcontext) requires a successful context handle unless memory is exhausted; `STATUS_NOT_SUPPORTED` is not an allowed refusal. Local `ADMISSION_CONTEXT_VALID_FLAGS` accepted only bits 0 and 1. The exact input flags are not yet observed, so bit 2 is the strongest source-backed inference, not a measured value.

## Change and discriminator

In G3 builds, admit bit 2 in the portable context object and KMD callback, preserving the physical profile mask. Record `Flags`, node, engine affinity, private bytes, runtime handle presence, and IRQL before validation. Host test rejects old bit mask (RED) and accepts SystemContext | VirtualAddressing (GREEN). Pinned WDK ARM64 incremental build gates package779. One later hardware run with the exact package must show the input receipt and either successful paging context/first VidMm DDI or a named later failure. Recovery remains the R54 series path while pinned SSH and package identity are certain.
