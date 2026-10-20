# C15 Windows ARM64 ordinary-new checkpoint (EXP1734–1741)

This is a CPU-only dependency checkpoint for the pinned dxil-spirv composite and
current project owned converter. It does not link CORE_0010 into the D3D12 UMD,
advertise SM6, install an AppleAgx package, or execute an AGX shader.

## Sources and contract

- Project API and ownership: `drivers/apple-agx/mesa/dxil/agx_dxil_owned.{h,c}`
  and `agx_dxil_checked.cpp`. `AGX_DXIL_OWNED_REPORTED_ERROR` is status 3;
  the public result is zeroed on entry and `agx_dxil_owned_free` zeroes it.
- Pinned external source and license review:
  `investigation/analysis/D3D12-C15-dxil-license-review.md`; Windows native
  build contract and 83-TU/nine-library review:
  `.local/c15-profile-native1542-result/root-native-review1542.json`.
- Actual source-bound host allocation-category evidence:
  `D3D12-C15-ordinary-new-sweep.md` and
  `D3D12-C15-persistent-backing-sweep.md`.
- J313 ordinary GPU-visible recovery baseline: EXP1681. The EXP1737–1741
  guest actions only stage and execute a CPU EXE in a separate user directory.
  KMD, package, firmware, power, interrupts, DMA and recovery ownership do not
  change.

## Boundary and evidence

EXP1734 checked the retained Windows builder's three project objects and nine
static libraries against the reviewed EXP1542 hashes. EXP1735 built a fresh
test-only ARM64 `/MD` EXE using those exact inputs. EXP1736 independently
checked PE AA64, the ten CRT/system imports, compiler/link arguments, source
dependencies and hashes. The test object defines scalar `new` and `new[]`, but
the link map retains a scalar `new` symbol only; array interposition is open.

EXP1737's first input converted, but its PowerShell runner read a null exit
code and skipped the second input. EXP1738 corrected only the runner. Both
inputs then returned owned SPIR-V, 980 bytes/one key and 2152 bytes/five keys,
with 14 and 15 observed ordinary-new events respectively, exit 0 and empty
stderr. EXP1739 independently checked copied receipts and streams. Most
nearest link-map symbols were in `dxil-converter` or `spirv-module`; one PC was
near CRT `__chkstk`. Nearest-symbol attribution is approximate, so the count
does not imply every allocation was compiler-owned.

EXP1740 built and independently checked another test-only EXE. It first ran
the exact input successfully, then kept a `std::bad_alloc` fault armed for the
first calibrated ordinary-new PC during a second job. EXP1741 executed that
EXE in the ordinary J313 Windows ARM64 guest. For each of the two fixtures,
the fault occurred once at the first attempted allocation, the converter
returned status 3 with a bytezero public result, and a third job on the **same
thread** returned status 0 and the original 980/2152-byte owned output.
Both child processes exited 0, stderr was empty, and SSH still answered
afterward. Exact receipts are
`.local/c15-windows-new1735/guest1741/receipt1741.json` SHA-256
`2811f430166d42fb73ec70aef9aa52b05ec5241b292aa827d98a9ed92df1bab6`.

## Verdict and next discriminator

The Windows ARM64 `/MD` first observed scalar-new failure crosses the reviewed
external facade and project owned-result boundary correctly for the two pinned
DXIL inputs. This is not general recoverable OOM: later observed allocation
positions, unseen shapes, custom 64-KiB backing on Windows, array-new,
unobserved CRT/EH paths and all GPU/DDI/caps behavior remain open.

Next use one test-only EXE with per-child calibration to inject each observed
Windows ordinary-new call position separately. Each child must require status
3, a bytezero failed result and a successful next job in the same thread.
Keep GPU hardware out of this CPU-only boundary; after this finite sweep,
return to the next distinct C15 integration gate rather than repeating the
same allocation hypothesis without new evidence.
