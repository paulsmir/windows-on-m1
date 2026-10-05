# EXP923 verdict: useful early stack, fullscreen wait still unidentified

The exact SDK process dump was captured and verified, but it caught a transient
GPUVA map during D3D11 device creation. It does not identify the subsequent
fullscreen-creation wait. The native trace later completed that map and imported
both exact fullscreen backbuffers. No native mode/Present was reached, physical
and virtual Present remained zero, and no accepted desktop was demonstrated.

Original boot 2026-10-02T06:09:38.8882220Z reached Code0/CPU8/Stage12. One SDK PID9988
session1 started06:10:37.7962412. Observer9576 was compiled before launch. Snapshot
06:10:48.2701289..48.8736730 captured4threads,83612bytes SHA256
10a258ce544008d4f81327c7a0c5381555afb3d2d5450b10d3b2d7f189b1fbc2.
The planned five-second delay plus identity checks yielded an actual10.47second
capture after process creation. Exact UMD DLL/PDB and public Microsoft symbols show:

```
win32u!NtGdiDdDDIMapGpuVirtualAddress
  d3d11!NDXGI::CDevice::MapGpuVirtualAddressCB
  AppleAgxRenderAdmissionUmd!map_va / AgxWin32GpuvaBind / agx_bo_create
  agx_pool_init / agx_bg_eot_init / AgxWin32AsahiScreenCreate
  native CreateDevice / D3D11CreateDevice / SDK wmain
```

Three worker threads waited in NtWaitForWorkViaWorkerFactory. Matching UMD records
later return from the captured map and show both BGRA87/dimension3/binda8/misc20002/
2560x1600 resource imports with S_OK. Treat the snapshot as early evidence, not a
confirmed memory-map bug. Early ETL194248704bytes SHA256
c0f6fa4832785d418656e68186e764e684d32e728a5414bbd00dd52f914ab36f stopped successfully
and was saved06:10:52.9723557. PID filter yielded610events; its converted UTC is offset
from process receipts, so compare PID/QPC ordering. An unmatched internal thunk
record is not independently proof of a failed Windows status.

The wrapper attempted to hash the SDK's still-open redirected stdout. This failed
before the combined snapshot receipt and left the child running when the wrapper
exited. Dump/observer/PID files and early ETL were independently size/SHA verified.
Only the exact held SDK process9988 was stopped06:12:16 after the dump host gate.
The monitor was stopped explicitly; no unchanged workload rerun occurred.

DCP sameSwap9/seq2 sampled154988 nonzero pixels out of4096000 at+120, avgBGRA6,5,5,9,
hashfe48b716ad5a6b5a; later physical bytes also changed. SourceAddress count1 remained
unchanged, every Present DDI remained zero, TDRcapture0. This corroborates corrupted
physical pixels, not the SDK clear or desktop success. No blind tiling/pitch fix.

Original15file hostgate605dd838c37fd9f4a8d9e0f045be71d2c1e3b26fbf1cfe28b7c02a3af9289b44
preceded ordered restart06:14:55. Normal377/392 recovery reached runtime/8CPU entry
but SSH did not return. One documented SIGINT captured CPU/IRQ and continued guest;
SSH still failed. A subsequent documented SIGTERM snapshot/reboot entered emergency
GPU-hidden385 only after this GPU-visible recovery could not be recovered. No known
Windows stop code, new dump or System1001 was found. Do not invent a bugcheck verdict.
Hidden Code45/visible0/serviceStopped/CPU8 recovery7file hostgate:
e9caa17824edd700ba548bf291801eaf7481c1265fdde04578d4486685b13117.
Guest wallclock initially lagged host and was corrected; CIMBootUTC shifted without
an inferred new physical reboot. Record fresh stage/preflight identities together.

Exact920/oem5 package, binaries, signer and diagnostics were removed under Code45
06:26:47 after evidence gates, followed by ordered restart to normal377/392. Final
normalboot06:27:02.0508070Z checks06:28:49 and06:29:12 prove Code28/one inertAPPL0002,
package/module/service/signer/arm/diagnostics0,CPU8/disks2/USB5/RDPservice/autologon1.
FreeC5569912832/shadow0. Cleanup3file hostgate:
9596dac01ac0fc1394e562271a879dff0fc9b779a6ed1750afaf277af5f3bb89.
Exact stopped task removed06:29:50 after action identity inspection.

Both identical closed ETL paths were verified/fsynced to one internal archive under
/Users/pavel/J313-evidence-archive/2026-10-02/EXP923-completed, then replaced by verified
original-path symlinks.388497408externalbytes reclaimed; archive target must remain.
All experiment evidence is under .local/experiments/EXP923-sdk-creation-stack.

Next discriminator EXP924 snapshots only after two exact frontend records and two
successful resource exits for the owned SDK PID, then two seconds later. It hashes
closed files and closes the held SDK in finally. Same driver920, exact SDK e1d5875c,
firmware, capabilities and resource layout. Implementation009730dc, ledger7c0bca38,
builder AST/PInvokePASS. No deterministic driver defect has yet been selected.
