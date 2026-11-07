# SM5.x nBody engine workload for first-release D3D12 diagnosis

Status: source and builder feasibility only. This is a Microsoft simulation
sample, **not a commercial game or release acceptance by itself**. Portal 2
launching is currently blocked before graphics by Steam state in EXP1782/1784.

## Primary sources and license

- Official Microsoft DirectX Graphics Samples commit
  `68907e31e836116f903e37e0b576dc2653cd9293`, parent of the project's
  2026-02-07 SM5-to-SM6 conversion. Exact `LICENSE` at that commit is MIT;
  retain its notice with any test binary/source snapshot. The external source
  stays in ignored `.local/experiments/EXP1785-nbody-sm5-source` and is not
  copied into tracked driver code.
- At that commit `D3D12nBodyGravity.cpp` calls `D3DCompileFromFile` for
  `vs_5_0`, `gs_5_0`, `ps_5_0`, `cs_5_0` and requests FL11_0. It has a
  continuous graphics plus async-compute/fence loop. Current `master` uses
  DXC/SM6 and is excluded from this checkpoint.
- The x64 VS16 project references pinned `Microsoft.Direct3D.D3D12` 1.618.3
  and `WinPixEventRuntime` 1.0.240308001 packages. Microsoft's PIX package is
  MIT; the Agility SDK package has its own redistributable terms. These are
  external **lab application** dependencies, not driver/package dependencies;
  do not distribute them in the AppleAgx signed driver package. The first
  builder pass will only establish whether the exact project and packages
  compile. No J313 run is authorized from a compile error or an unreviewed PE.

## WINDOWS CONTRACT:

A D3D12 application can create FL11_0 device, SM5 DXBC shaders, graphics and
compute queues, resources, barriers and fences. The exact sample requests
some newer feature queries and an Agility runtime; their result on this driver
is unknown. A successful build alone proves neither device admission nor
rendering. An x64 executable on Windows ARM64 is a separate runtime path whose
UMD selection must be measured rather than assumed.

## AGX/ASAHI CONTRACT:

EXP1765 qualified 63 focused non-SM6 D3D12 steps on J313, including compute,
graphics and presentation, with same-source FactoryV2 recovery. Asahi AGX
execution and KMD queue topology are unchanged for this workload. The sample's
async compute and cross-queue fences may expose a new unsupported contract.

## TRANSLATION:

External x64 nBody app -> D3D12Core SM5 DXBC -> exact AppleAgx UMD/KMD -> AGX,
only when module/UMD/KMD receipts prove selection. Pre-build: exact Git commit,
MIT notice, 22 source-file SHA manifest and source ZIP. Build with pinned builder
MSBuild, project NuGet dependencies and explicit x64/Release toolset/SDK; freeze
EXE, shaders and any app-local D3D12 runtime before a separate hardware BEFORE.

## WHAT IS STILL UNKNOWN:

Whether NuGet restore/build works on the builder, whether Agility runtime 1.618.3
can negotiate the current fail-closed driver, whether x64 emulation selects the
correct UMD, and whether async compute/fence/geometry render succeeds. If the
build requires an unbounded dependency repair or SM6, stop after this one
feasibility pass. A later hardware run would be a new falsifiable discriminator,
not a substitute for a real game's release gate.
