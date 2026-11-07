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

EXP1405 completed: CPU_ONLY_PASS. The actual shared capture function returned
exact exits0and7 and both streams for its controls. The unchanged ARM64 caller
returned exit0, C15_MIXED_OWNED_PASS, five namespace mappings and2152-byte owned
SPIR-V after TLS cleanup; timeoutfalse/inputunchanged. Native interval
2026-10-18T15:52:34.4960547Z--15:52:34.6734865Z. Pre/post Code28/noService/8CPU
and BootUtc2026-10-18T15:39:26.6904720Z remained identical.

Final evidence: /Users/pavel/public_windows/.local/c15-owned-cpu-1405/final-host-manifest.json
SHA342e45f372a8b82e28fb7c79641ae3ffbeeb3dec2f5d322cda020f84456c2d9c.
This proves the limited Windows ARM64 C API/CRT/TLS invocation and exit capture.
It does not prove GPU/DDI/SM6 capabilities, internal allocation failure handling
or general shader conformance. Native SPIR-V semantics are a separate EXP1406.

EXP1406 CPU_ONLY_PASS: the immutable native1405 SPIR-V passed Vulkan1.1
validation, existing1398 class/slot lowering, and existing1401 restricted actualNIR
semantic replay. Allthree process returns were0; 34input hashes/sizes/paths
unchanged before/after. 192lanes/173active/19tail/384words/346stores matched,
three negative variants each produced346mismatches and unsupported/bounds
were refused. Both UAV arrays equal independent1397 expected data. Structural
and semantic postNIR byte-match SHA3129a611189d61e21248ad7ea790a0ab4eac8fc07b1b10d02b8b9b3b174ed67e.

Receipt.local/c15-native-replay-1406/artifact-hashes.json SHA65e64adbbf13aa9738df2f1157a8e4e638c7f2c4579b969ca738a7348527f0ca
covers17files independently verified by coordinator. No new compile, fetch,
source or guest operation. No inference for differing host/native SPIR-V bytes.
This closes fixedshader nativeCPU semantic validation; GPU/driver/DDI/SM6,
internal OOM and general shader conformance remain separate gates.
