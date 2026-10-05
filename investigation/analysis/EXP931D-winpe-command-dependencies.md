# EXP931D: remove unavailable WinPE command dependencies

EXP931C photo A922EBC4 proves T: readable with correct NTFS serial, then pre-repair pause. Exact WIM directory inspection and command gate fail on BOTH indices: findstr.exe and fc.exe are absent. Script calls findstr immediately after ntfsinfo, and fc later for the backup. Native builder fsutil emits ASCII and findstr succeeds: encoding hypothesis rejected. This is a recovery payload packaging/script defect, not a GPT or graphics failure.

WINDOWS CONTRACT: Native helper already compares GPT ID, partition length, FSCTL NTFS serial and geometry before aliases. ReadFile returns success with zero bytes at EOF; failures and unequal contents/lengths must fail backup comparison. Reviewed https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile and https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/chkdsk . CHKDSK still requires identity and byte-identical offline backup first.

AGX/ASAHI CONTRACT: No change to hardware behavior. Same measured immutable m1n1 377 / Mu392 recovery owns initialization, IRQ, DMA and power. AGX broker remains disabled; no firmware, ACPI or graphics contract is implicated by a missing PE executable.

TRANSLATION: Remove duplicate text search of serial (native exact comparison remains); replace unavailable fc with native ReadFile byte comparator in existing helper. Keep copy, all identity gates and original CHKDSK /f /x unchanged. Failure prints the precise step and copy/compare receipts last. A host check inventories both WIM indices against external commands invoked by this specific batch script.

WHAT IS STILL UNKNOWN: Whether the verified offline SYSTEM backup gate succeeds on Air and whether CHKDSK resolves NTFS metadata corruption. No new guesses about register/driver behavior. One run tests the repaired payload dependencies.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Actual old payload RED for missing findstr/fc; fixed script and packed image must be GREEN. Native backup tests cover equal multi-block contents, same-length corruption after block one, truncation, missing and locked files; all mismatch/read failures refuse repair. Existing identity/alias tests remain GREEN.

Recovery: confirmed pre-CHKDSK pause permits exact-owner reboot after dual-plane checks. Preserve images A/B/C and host-verified SYSTEM/BCD backups. New ESP evidence directory J313-EXP931D-recovery. After CHKDSK automatic reboot use normal377/392, collect/hash logs and independently verify RegFlushKey and dirty state before GPU work. Never interrupt potentially active CHKDSK.
