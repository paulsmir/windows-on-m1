# EXP971 latency verdict and EXP972 decision

EXP971 used package 30.0.971.0 from source `399e47eeef2cc149223287f391c266444a53bfa6`.
The guest reached Code0, CPU8, SSH, DWM 1232 and Explorer 5144. The operator
moved the mouse and pressed Win+D at 14:31Z, without an immediate visible
panel update. The package was removed through exact ordinary Code43 cleanup;
the next ordinary boot passed durable Code28/PackageAbsent at 14:52:56Z.

The 64-job KMD ring (QPC 24 MHz) places DWM's median 78.07 ms of a
138.14 ms job inside `AdmissionGpuvaG3BeginJob`; Explorer spent median
30.90 ms of 39.67 ms there. During the input window, five DWM jobs had
median BeginJob 220.78 ms and total 267.01 ms. Backend submit was typically
below 3 ms. Independent 30-second DxgKrnl ETW had 51 render packets with
median DMA duration 40.30 ms versus 6659 paging packets at median 0.2055 ms.
The ETW window did not overlap the ring's last 64 fences, and a subsequent
8-second joint window contained no DMA packets; exact per-fence ETW join is
inconclusive. Raw TA-start values stayed constant; they cannot establish
firmware execution time. Evidence paths and hashes are in EXP971 ACTUAL and
EXP971A–E entries of `investigation/EXPERIMENTS.md`.

## Source-backed causal decision

`gpuva_g3_windows.c` calls upload rehash, G4 envelope validation,
`AdmissionG3OutputMatchesLocal`, and graph lease/JOB_BEGIN inside BeginJob.
The same file validates a full output every 16 KiB and scans parent and
TableShadow lists for every 4 KiB of a CPU envelope.
`apple_agx_gpuva_g3_graph.c` implements `GraphTranslateVa` and
`GraphInspectRangeAccess` by `find_edge` linear scans of `Parents` and
`Leaves` for each page. With thousands of mapped leaves and pages, this
is quadratic pointer chasing. Claude's 14:45Z supervisor review selected
these current primary source sites as the strongest latency cause. It remains
a source-supported hypothesis until an indexed candidate reduces measured
BeginJob time on hardware.

EXP972 changes the owning shared G3 graph and KMD envelope lookup only:
index each table's child edges by its 2048-entry page index, update that index
after acknowledged broker stores, clear it on deletion/retirement, and use it
for translation and range validation. Cache the selected logical TableShadow
across a contiguous 32 MiB envelope span instead of rescanning it for each
4 KiB page. Preserve every page's validity, writable, and physical continuity
check; do not admit by test-specific size, offset, content, or bind pattern.
Keep the EXP971 timing ring in the candidate. Deterministic host RED→GREEN
must show that lookup visit count does not grow with leaf count, and existing
map/remap/revoke/reuse and G3 replay tests must remain green. The smallest
hardware checkpoint is a DWM/Explorer BeginJob duration distribution below
5 ms on the same full-owner profile with an updating physical desktop.
If that fails, collect the dump/receipts first, then exact package cleanup
in ordinary Code43 and durable Code28. No m1n1, Mu, ACPI, capability,
signer, or recovery change is in this candidate.
