# C77 shared content: offline evidence and implementation plan

Goal: a fenced upload into a shared linear Texture2D must be visible through
an already-opened allocation in another process. Scope: UMD copy publication
and truthful open receipts; no hardware, package, firmware, KMD, or caps change.

## Evidence and contract

- EXP1915 ARM64 handoff `tool-d3d12-on-umd.log:17568-17572`: the 37x29
  RGBA8 producer uses the presentation allocation, bind 0x103. This already
  imports linear storage, not a compressed or twiddled BO.
- Same trace `:17589-17599`: copy escape operation 1 downloads 0x1280 bytes;
  the CPU texture copy succeeds; owner fences are 0/0 and owner submit reports
  zero copies. Operation 0 uploads only later, just before destruction.
  The observed 4274 byte mismatches equal the nonzero bytes in the pass-0
  pattern (4292 total). This is consistent with reading untouched zero storage.
- `agx_d3d12_windows.cpp:1376-1388,1451-1463`: both create and open adopt the
  runtime allocation and import the same serialized linear pitch/extent.
  `agx_win32_asahi_scene.c:119-140` constructs an uncompressed linear layout.
- `agx_d3d12_windows.cpp:2201-2225`: uncompressed uploads use texture_map and
  texture_unmap. Mesa `agx_pipe.c:1011-1026,1031-1069` keeps a BO map cached;
  transfer unmap is not BO unmap. Windows `umd_gpuva_windows.c:1216-1250`
  downloads/publishes a Direct shadow at native map boundaries, and
  `agx_win32_asahi_bo.c:218-224` unmaps the BO during destruction.
  `agx_d3d12_windows.cpp:3516-3531` flushes batches, but this CPU-only upload
  produced none. A queue fence alone cannot publish a private CPU shadow.
- WDK 26100 `d3d12umddi.h:484-492,1829-1830,3148`: D3D12 opens allocation
  handles plus resource/allocation private data through OpenHeapAndResource.
  The D3D10 table's pfnOpenResource is a different interface
  (`d3d10umddi.h:1712`). `agx_d3d12_windows.cpp:1448` directly calls
  AdmissionUmdOpenResource, which logs presentation-open at `umd.c:872`.
  Thus that receipt is an internal helper, not proof of runtime DDI routing.
  The probe matches d3d12-open-* but the owner emits
  measure-d3d12-shared-import (`agx_d3d12_probe.c:945`, owner `:1471`).
- Mesa `agx_pipe.c:461-467` chooses linear shared resources; the local import
  explicitly selects linear. No external implementation code is copied.

Ownership: D3D12 owner translates the copy and owns staging lifetime; Mesa
records/submits the GPU copy; existing winsys/KMD own GPUVA, residency and DMA.
Interrupts, initialization, power and recovery remain with existing KMD,
m1n1 and Mu contracts. This defect is above those layers; inspecting or
changing live state is prohibited by the offline handoff.

## Implementation and verification plan

1. Add an executable RED regression combining decoded shared private data
   with production CopyTexture/staged_upload/staged_download: two writes,
   distinct creator/opener views of one backing allocation, padded rows and
   subrect preservation. A CPU shadow must not count as published content.
   Cover allocation/map failures without recording a GPU copy.
2. Route one-slice shared Texture2D uploads through existing staged_upload;
   retain compressed uploads and other resource paths. GREEN targeted tests.
3. Add a DDI-level d3d12-open-heap-resource receipt and recognize the existing
   owner shared-import receipt in the child trace. Test real parsing behavior.
4. Run shared-open/probe, native-owner, ABI and volume tests, then the full
   run_against_baseline.py gate. Resolve local fixture prerequisites without
   adding environment failures to known_failures.txt. Small commits; final
   handoff REPORT.md carries results, commits and remaining unknowns.

## WHAT IS STILL UNKNOWN / hardware checkpoint for integration owner

Offline tests cannot prove VidMm shared physical identity or GPU coherence.
Rerun C76A on the final integrated package on ARM64/x64/x86: both writes must
read back with zero mismatches, the second after producer resource release.
Expect a D3D12 DDI open and shared-import receipt plus staged-upload and nonzero
owner fence progress before each producer completion. Retain the child trace
and KMD open evidence if it fails. Use the integration owner's existing exact
package cleanup/recovery contract; this offline task performs no hardware run.

## Offline result

The regression ran the real producer description/serialization, private-data
decoder, CopyTexture dispatch, staged upload and staged download, with Gallium
storage callbacks in place of hardware. Before the fix the 37x29 case reported
`mismatches=4274 pending=0`; after the fix all nine format/width combinations
passed both writes, subrect/row-padding preservation, and failed-map/allocation
checks under ASan/UBSan. This proves the owner translation contract, not GPU
execution or cross-process physical aliasing.

The final targeted command ran 54 tests successfully. Full baseline:

```
ran 392 tests in 169 modules; 8 failing, 8 known, 0 NEW, 0 FIXED
BASELINE_OK
```

The probe actually opens the child first (`agx_d3d12_probe.c:768,785,886`),
then publishes each fenced producer write. The parent's completion-before-read
ordering is correct, but no native GPU work existed to publish its CPU shadow.
New receipts distinguish the DDI, successful/failed owner import attempt and
internal legacy helper. No private-data ABI or creation layout change is needed.
