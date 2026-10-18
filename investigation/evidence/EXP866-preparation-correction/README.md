# EXP866 preparation correction

The prior preparation missed the hard-coded autologger dependency in
`resume-stage.ps1:14`. Its pin named EXP865 bytes even though the outer
transfer manifest correctly hashed the EXP866 script. Outer artifact checks
therefore did not establish that the script's own dependencies were correct.

`audit_hash_literals.py` scans every `.ps1`, `.sh`, `.py`, and `.json` recursively
in the EXP866 directory, records every 64-hex occurrence with file/line, literal,
semantic target, actual SHA-256 and verdict, and exits nonzero for mismatches,
missing files or unresolved references. Script operands have explicit ordered
targets; JSON targets come from their field/path. It never finds a target by
searching for the expected digest. The companion file list includes scripts
with no literals. TSV output is kept outside the scan to avoid recursively
auditing audit output.

Before: 55 files, 682 literals, 681 matches and one mismatch (the reported pin).
After: 55 files, 682 matches, zero mismatches or unresolved targets. Source
manifest entries are compared with actual source.zip members. Three native
build artifacts were fetched from the EXP866 builder directory into local
`audit-inputs/`; immutable recovery, reference contract and signing tool pins
are checked against their explicitly named files in the experiment archive.
These external references are identified in each TSV target.

The corrected pin caused updates to guest-transfer-manifest, hardware-manifest,
verify-transfer, guest-payload, extract-verify, readiness-manifest and
preparation-result, in that dependency order. `rebuild_preparation.py` reproduces
this refresh. Corrected text artifacts are retained in `corrected/`.
The package binaries, firmware, source candidate and hardware procedure did
not change. The original ledger entry is preserved and corrected by append.

Verification: all three launch/recovery wrappers exit0 with `--verify-only`;
42 PowerShell scripts parse with zero errors; native PowerShell confirms the
resume autologger pin against actual bytes. All42 payload members match current
files and the guest manifest, have correct package placement and no metadata;
the syntax ZIP contains current scripts. No prior autologger or replaced
manifest/payload digest remains in the active audited corpus. Hardware NOT_RUN.

Reproduce from repository root:

```sh
python3 investigation/evidence/EXP866-preparation-correction/audit_hash_literals.py /Users/pavel/public_windows/.local/experiments/EXP866-r151-stamp --output /tmp/exp866-literals.tsv
```
