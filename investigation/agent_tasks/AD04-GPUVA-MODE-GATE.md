# AD04 GPUVA prerequisite gate — 2026-09-12

Input HEAD b95c54ea7ef339fe3b4e7cb73ed69a28754474dd. Source-only inspection;
no hardware, build, new agent or production-code change. Reviewed KMD files
match architect base f90402c without diff. Compiler milestone remains accepted.

User approves Windows/VidMm-owned binding separately from residency, but explicitly
requires STOP if production still uses physical/patch-list mode.

## Source verdict: PHYSICAL/PATCH-LIST

- driver.c67–69 wires Patch/SubmitCommand/BuildPagingBuffer.
- callbacks.c343–360 zeroes ContextInfo and supplies allocation/patch lists;
  no NoPatchingRequired virtual-context opt-in.
- callbacks.c433–436: SubmitCommandVirtual and CreateProcess use FAIL2;
  macro at4–9 returns STATUS_NOT_SUPPORTED. Registration is not implementation.
- lifecycle.c386–429: DRIVERCAPS zeroed, then selected fields assigned;
  WDDM3.0 version does not itself opt into GPUVA/GpuMmu/IoMmu.
- render_dynamic_windows.c117–128 resolves allocation-list PhysicalAddress through
  AdmissionMemoryRuntimeResolveLocal to internal GpuVirtualAddress. That internal
  AGX/UAT address is not evidence of a VidMm-owned persistent process GPUVA.

Microsoft primary reference (FULL GRAPHICS):
https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-virtual-memory-in-wddm-2-0
Physical engines retain allocation/patch lists; virtual mode and its mapping/
residency obligations require explicit support. Firmware/UAT hardware capabilities
alone do not establish the Windows-facing contract.

## Gate action

Stopped before AGX_WIN32_GPU_BINDING implementation, real BO integration or its
requested lifetime tests. No fake persistent GPUVA, PA export, caps adjustment,
firmware ownership transfer or binding-as-residency assumption was introduced.

Architectural choice now required:
1. Separately design a supported Windows GPUVA migration, including process/VA
   ownership, mapping/paging fences and independent residency, reconciled with
   retained-root/private-table ownership. No commitment to feasibility yet.
2. Retain the proven physical/patch-list engine and revise the native Asahi
   integration around explicit relocations instead of persistent VidMm GPUVA.

Preference: if VidMm-owned stable GPUVA is the required target, authorize option1
as a source-first architectural design phase, not as a small binding patch.
Do not silently choose either route. CPU map/unmap is distinct from GPU binding
unmap; this distinction must remain explicit in the eventual lifetime contract.
