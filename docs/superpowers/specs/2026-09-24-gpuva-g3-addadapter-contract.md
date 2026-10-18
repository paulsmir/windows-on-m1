# G3 AddAdapter GpuMmu declaration contract — EXP774

Status: offline contract and one planned hardware discriminator. EXP773 reached
StartDevice and successful QAI13/14, then Dxgkrnl Event549 rejected AddAdapter
with `STATUS_INVALID_PARAMETER`; no VidMm process or paging DDI is proven.
The internal AddAdapter validation sequence is not public. This table records
the WDK structures, documented meanings and driver outputs, and labels
relationships that are engineering inferences rather than Microsoft rules.

Pinned primary headers: WDK 26100 `d3dkmddi.h` SHA256
`c13cecb0ce73e7bbdb6bec8586d05eea31932a8c532bec49b3dae4a03054770e`
and `d3dkmdt.h` SHA256
`bbb7b4314f5bc78d71db54b2821007cf82b85deee59d4f258e60d940c52e422e`.
The current KMD is `lifecycle.c`, `callbacks.c`, `memory_windows.c`,
`receipts.c`, and `apple_agx_physical_topology.c`. The hardware, Asahi, m1n1
and Mu ownership contract is in `2026-09-24-gpuva-g3-contract.md`: AGX has
39-bit VA and 16-KiB native leaves; m1n1 owns UAT publication/TLB/slots;
Mu only exposes APPL0002/resources; Windows VidMm owns GPUVA and page-table
allocation. EXP773 armed and final recovery evidence is in the experiment ledger.

| Declaration | Current EXP773 output | Coherent EXP774 output | Primary source |
|---|---|---|---|
| `DXGK_DRIVERCAPS.MemoryManagementCaps` and topology | `VirtualAddressingSupported=1`, `GpuMmuSupported=1`, `IoMmuSupported=0`; one asymmetric execution node; `PagingNode=0` by zero init | Same, explicitly derived from one G3 model. `DedicatedPagingEngine=0`; node 0 serves paging. | WDK `d3dkmddi.h:2255-2316,2407`; [DXGK_VIDMMCAPS](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_vidmmcaps) says VirtualAddressing plus GpuMmu/IoMmu express GPUVA and `PagingNode` is WDDM 1.x only. |
| Every execution node's `DXGK_NODEMETADATA` | Node 0 is 3D, `GpuMmuSupported=0`, `IoMmuSupported=0` after zero init; no other node exists | Node 0 3D/paging declares `GpuMmuSupported=1`, `IoMmuSupported=0` only while G3 is live; physical profiles retain zero | WDK `d3dkmdt.h:2004-2019`; [DXGK_NODEMETADATA](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmdt/ns-d3dkmdt-_dxgk_nodemetadata) defines these booleans for the node's engines. |
| WDDM 3.2 `QUERYMMUCOUNT` / `QUERYMMUS` | Count 0, `DisplayMmuId=0xffff`, no descriptors | One truthful AGX GPU MMU descriptor with 39-bit VA coverage; display MMU remains invalid because DCP scanout uses its separate physical path; preserve caller's descriptor pointer and zero reserved fields | WDK `d3dkmddi.h:11139-11180` defines the count, descriptor `Size` and display ID. Microsoft [QAI enumeration](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ne-d3dkmddi-_dxgk_queryadapterinfotype) names types 45/46 but does not document a minimum count or node/segment mapping. Count ≥1 and `Size=1<<39` are project coherence invariants, **not a proven Dxgkrnl gate**. The structures contain no node or segment link field. |
| `PHYSICALADAPTERCAPS` QAI15 | `STATUS_NOT_SUPPORTED` | Leave unchanged until a documented KMD call requires it; no synthetic handle/default is guessed | WDK `d3dkmddi.h:2163-2223` has `NumExecutionNodes`, `PagingNodeIndex`, `Flags.GpuMmuSupported`; [structure](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_physicaladaptercaps) defines fields. The [QAI enum](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ne-d3dkmddi-_dxgk_queryadapterinfotype) marks QAI15 reserved for system use, while Microsoft MCDM guidance calls it optional. Applicability and Dxgkrnl fallback for this graphics driver are unproven. |
| `GPUMMUCAPS` QAI13 | ReadOnly=1, explicit invalidation=1, address-space idle=1, GPU_PHYSICAL updates, VA39, 3 levels, Leaf64K=8192; DualPte=0 | Same, derived from the coherent model; receipt also records Leaf64K to remove the EXP773 observation gap | WDK `d3dkmddi.h:2113-2161`; [DXGK_GPUMMUCAPS](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_gpummucaps). |
| `PAGETABLELEVELDESC` QAI14 | L0/L1/L2 index 13/11/3, segment2, allocation 128/32/16 KiB, alignment16KiB; all queries status0 | Same; `13+11+3+12=39`, every allocation holds its logical `DXGK_PTE` entries | WDK `d3dkmddi.h:2072-2081`, `d3dukmdt.h` `DXGK_PTE`; [GPU virtual address](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-virtual-address). |
| Segments and paging buffer | Two segments: one aperture id1, one local id2; local `Use64KBPages=1` in QUERYSEGMENT4/5 and 64-KiB slab; paging buffer segment1, 4-KiB buffer/private data | Same; QUERYSEGMENT5 reserved output words zero | WDK `d3dkmddi.h:2720-2763,11050-11135`; [GPU segments](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-segments) requires a single aperture and documents 4/64-KiB memory-segment pages. |
| `WDDMDEVICECAPS`, physical memory and IOMMU caps | WDDMVersion=3.2 matches DRIVERCAPS; PHYSICAL_MEMORY_CAPS highest visible address is `0xffffffffff`; IOMMU_CAPS is zero | Same; no IOMMU isolation is advertised | WDK `d3dkmddi.h` QAI29/34/35 structures; [DXGK_VIDMMCAPS](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_vidmmcaps) says GpuMmu and IoMmu models are exclusive, and [QAI enum](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ne-d3dkmddi-_dxgk_queryadapterinfotype) describes pre-Start physical-memory/IOMMU queries. |
| WDDM 3.2 QAI48 paging-process VA size | `STATUS_NOT_SUPPORTED` | Leave OS fallback in place; allocation-notification size override is not enabled | [Allocation notification](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/allocation-notification) says a failed or zero reply makes Windows choose the size. |

**Atomic contract.** Adapter `VirtualAddressingSupported/GpuMmuSupported`,
the 3D/paging node's `GpuMmuSupported`, one reported AGX MMU, QAI13/14's
VA/table description and the local 64-KiB segment all describe this single
device's GPUVA path. Advertising only the adapter bit makes node metadata and
MMU inventory contradictory; advertising only a node or MMU inventory leaves
VidMm without the adapter opt-in and page-table contract. One shared validator
must reject those split states. PHYSICALADAPTERCAPS is audited separately
because QAI15 is not documented as a mandatory KMD response here.

Ownership and checkpoint: VidMm initializes and calls the GpuMmu DDIs; KMD
answers all declarations, translates PTEs and records first refusal; m1n1
owns hardware roots/TLB/recovery; Mu exposes static resources only. Implement
the model, RED→GREEN validator and QAI/node receipts without changing m1n1,
Mu, signer, 64-KiB profile or boot diagnostics. EXP774 is one staged cold
debug-off/no-KD/no-WPR full-owner boot. The falsifiable checkpoint is natural
Code0/first VidMm DDI or the first AddAdapter refusal with node/QAI45/46
receipts. Collect evidence, disarm, use immutable GPU-hidden exact-package
removal, then verify ordinary GPU-visible Code28. No second EXP774 boot is
justified without a new causal difference.
