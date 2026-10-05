# EXP972 indexed GPUVA lookup

## Evidence and contract

EXP971's hardware ring measured DWM BeginJob at 78.07 ms median and 220.78 ms
during input, with backend submit typically below 3 ms. See
`investigation/analysis/EXP971-latency-verdict.md` and EXP971 ACTUAL in the
experiment ledger. REC-EXP971C returned the Air to ordinary Code28 with no
AppleAgx package. The supervisor's 14:45Z review identifies the graph and
logical envelope linked-list walks as the nearest source-backed cause.

Primary code inspected: `apple_agx_gpuva_g3_graph.c/.h`,
`gpuva_g3_windows.c`, `gpuva_g3_private.h`, and the C graph/G4 submit replay
harnesses. The broader hardware, firmware, and Windows contracts were recorded
in the EXP971 verdict and prior G3/G4 plans; this change does not alter them.
Asahi and m1n1 own GPU/UAT initialization and execution; Mu exposes the
Windows device. The Windows KMD owns the process mapping shadow, validation,
and BeginJob. This experiment changes only lookup metadata within that KMD
contract. It does not change DMA, interrupts, power, recovery, broker commands,
or WDDM interfaces.

## One variable and falsifiable checkpoint

The old `find_edge` did a pointer chase across the whole leaf list for each
16 KiB VA lookup. The RED harness constructed 64 and 8192 leaves and failed
the constant-visit assertion. Indexed table slots and broker TableShadow
buckets now resolve links without a whole-list scan. Insert, replace, clear,
table retirement, and root relocation update the index. Every 16 KiB graph
page and every 4 KiB logical PTE continues to be checked.

EXP972 will retain the EXP971 timing ring and use the same full-owner profile.
The checkpoint is DWM BeginJob below 5 ms with input-driven desktop updates.
If absent, freeze the ring, ETW, UMD, and host log, then use exact package
cleanup in ordinary Code43 and durable Code28. This is a hypothesis until the
hardware timing comparison and physical operator observation are recorded.

The targeted graph and G4 submit replay passed. The whole host suite was run
once and had failures in other harness/toolchain paths; those are listed
in `/tmp/exp972-full-suite.log` on the host. Hardware preregistration must
use the exact committed source and a zero-warning pinned WDK build.
