# Native SELF pin rebinding scope

EXP1514 stopped at preflight before command1. The input registry described two
historical runner files using the newly edited1514 runner's size and digest.
The files themselves were unchanged: finite read-only builder hashes matched the
accepted1512 registry exactly.

- EXP1451 runner: 9983 bytes, ae9f626bb1c27474d7c1e874fa33a5e206a3fad43225350b197ad2be4ed686c7.
- Mapped-backend runner: 10281 bytes, d76163c3ef960eb8a73be1bfc7307e777adff622bda02432be6707a72860ec5e.
- Incorrect1514 values on both records: 14004 bytes, cc68cd34cf4935374fd3023cde7ec6dbda3e8a2a75f751a7aae5f1bf1500da8a.

The actual preparation producer selected every path ending in `run_native.py`.
The owning metadata producer must select its own absolute normalized Windows
path, require exactly one match, and preserve every other input record. Restore
both historical records from accepted1512; retain them as inputs. Do not change
builder files or drop the pin checks.

Actual corrected producer: `.local/c15-full-umd-native1516-prep/finalize1516.py`.
Regression on the actual14516-entry registry reproduces two historical corruptions
with the old suffix predicate and preserves all14515 other records with the new
full-path predicate. Proof SHA b3a01bffd380af7349d20ef5826048d5c84ac593e7f805c631820da44c603a68.
Root independently compared all common1512/1516 records, local and nested SELF,
ZIP members and canonical approval path. Registry, original producer evidence and
all1514 STOP receipts remain preserved; no old result is rewritten.

This repairs build preparation only. New native attempt1516 retains the exact
private-source graph, flags and seventeen commands, with a fresh builder root.
The private DLL/host target names1514 remain explicit artifact names. Production
DDI, runtime, caps, guest package and firmware are unchanged. Actual item gates,
normal1504 runtime verifier and later PE/CRT/TLS review remain mandatory.
