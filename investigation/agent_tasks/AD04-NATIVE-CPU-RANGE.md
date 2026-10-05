# Native CPU-to-construction emission bridge

`AgxWin32AsahiFindCpuAddress` proves that an exact mapped CPU interval belongs
to a current owner/generation native BO, returns its exact construction address
and offset, and cross-checks that result through the existing construction range
resolver. It enumerates only the existing NativeBo namespace; it does not scan
command bytes or create a second BO registry. `CaptureCpuRange` requires caller
serialization and rejects mismatched CPU/construction pairs before the existing
RetainExact typed reference path.

The real original pool/UMD owner executable proves valid subrange lookup and
capture dedup, wrong CPU range/generation, zero bytes and mismatched supplied
construction address failures. This enables later source-defined VDM/PPP ranges
where a native producer exposes CPU begin/end and construction coordinates.

It does not yet capture a full state/encoder emission, classify initial encoder
intent, or resolve General-backed encoder rollover. Per Tier-A decision, rollover
will remain fail-closed until its separate source-class/link contract exists.
No submit, package, hardware or acceptance claim.

Source029 SHA256 c64e33977fd6d89934755b03d17f9b3dc548aa471977743cc9a4ec43207a7cd9.
Windows x64 execution PASS, executable SHA256
7d449d8df9e354968d049749115f9f32368f5d1e09f56070e7bd1f9be5d62723.
Host relevant suites PASS. Raw logs: evidence/AD04-native-cpu-range/.
