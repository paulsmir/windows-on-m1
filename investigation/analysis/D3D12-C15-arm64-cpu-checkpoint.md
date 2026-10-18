# Windows ARM64 owned converter CPU checkpoint

EXP1400 executed the exact1402 native caller after1390 durable recovery. Its
stdout reports C15_MIXED_OWNED_PASS and wrote2152-byte SPIR-V plus allfive
namespace mappings. Input hashes remained unchanged. Pre/post oneCode28,
noService,8CPU andBootUtc2026-10-18T15:39:26.6904720Z matched. NoGPU/DDI/profile/package
operation occurred.

The wrapper reported CPU_ONLY_FAIL because Process.ExitCode was NULL after
Start-Process/WaitForExit. No timeout was observed. This does not establish a
native caller failure or a captured zero exit; preserve both facts separately.
The stronger cause of the missing exit observation is not asserted.

Evidence: /Users/pavel/public_windows/.local/c15-owned-cpu-1400/final-host-manifest.json
SHA6ef6b41825a96f771225d0ecaaa8add43b28513ac35167ac37a4cd42b1ce1608. Native SPIR-V SHA1a5bef149143573bf7fcd8ca2d07772e054f8b61f6958363f8f3ce22e408c07f
differs from host1398 SPIR-V24e3df26; semantic equivalence of this native artifact
is not yet claimed.

EXP1405 is a fresh CPU-only retry with the same21 distribution files/EXE/fixture.
One shared explicit.NET ProcessStartInfo/Process function reads stdout/stderr
asynchronously, captures nonNULL ExitCode beforeDispose and enforces30s timeout.
The same function is checked with cmd.exe exits0and7 before the unchanged caller.
All1400local/remote evidence is preserved; no retry uses its existing out folder.
