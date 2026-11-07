# C15 package approval JSON case contract

EXP1524 stopped in Windows PowerShell ConvertFrom-Json at build-package.ps1
line8, before input validation, destination creation, notice copying or any
compiler/catalog/signing child. The raw native error is preserved in
.local/c15-package1524-result/04.stderr. The collector retained an empty output
ZIP and reported the missing build-verdict rather than fabricating success.

The owning metadata generator combined Python provenance fields relative,
bytes, sha256 with PowerShell consumer fields Relative, Bytes, SHA256 in the
same NoticeFiles objects. Python's ordinary JSON loader accepts these names;
the observed Windows PowerShell consumer refuses the case aliases. All28
records have three collisions:84 pairs. Their values agree; license bytes,
source40e, native runtime1520 and actual1523 UMD qualification are unchanged.

The producer fix emits only Source, Relative, Bytes and SHA256 in the native
approval's notice view. It first requires old alias values to agree. Original
license provenance remains in the separately pinned host notice-files.json;
it is not discarded or rewritten inside the license distribution. EXP1524
inputs/results remain immutable, and the corrected native attempt uses1525.

scripts/agx_powershell_json.py checks every object with object_pairs_hook,
refusing both exact duplicate keys and case-fold aliases before a Python
parser can discard either value. Build schemas use ASCII field names; Unicode
case-fold aliases are conservatively refused too. The checker never rewrites
ambiguous JSON or chooses a preferred value. It supports BOM input, preserves
valid data and does not confuse identical names in separate array elements.
The controller invokes this check before transport, and the short native
stage gate additionally parses the actual approval before the producer.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? The actual EXP1524 nested
relative/Relative pair, differing bytes/Bytes values and exact duplicate keys
must stop locally. The permissive parser reproduced three failing tests;
the explicit pair checker passes all six tests. The old actual approval is
rejected with the same key pair as native PowerShell; the corrected28-row
approval has zero aliases and unchanged names/sizes/license hashes. Proofs
and RED/GREEN logs are in .local/c15-package1524-prep and
.local/c15-package1525-prep/json-case-repair-proof.json.

This is a build-workflow fix, outside the725-file driver snapshot. No driver,
firmware, public capability, signer or package contents were changed to work
around the JSON failure. Actual1525 parser/build results remain a separate
preregistered gate. The full host result is recorded beside the targeted
checks in EXPERIMENTS.md before native1525 launch.
