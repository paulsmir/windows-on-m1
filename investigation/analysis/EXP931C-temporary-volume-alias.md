# EXP931C: temporary WinPE volume aliases

Observed: operator photo confirms exact GPT/serial/length match, then SetVolumeMountPoint returns 87. CHKDSK was not entered. Why Mount Manager rejects this request remains unknown; no claim that GUID format was wrong.

WINDOWS CONTRACT: QueryDosDevice resolves enumerated Volume{GUID} to its current native device. DefineDosDevice with RAW_TARGET_PATH creates a temporary drive letter; EXACT_MATCH_ON_REMOVE only removes the owned alias. Reject occupied letters unless their current target is identical. All subprocesses remain in the same WinPE security context. Microsoft sources: https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-definedosdevicew and https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-querydosdevicew . API contracts inspected; original implementation independently written, no sample code copied.

AGX/ASAHI CONTRACT: Not applicable to this user-mode recovery naming boundary. Immutable m1n1 377 / Mu392 own hardware initialization, IRQ, DMA and power as in the last recovery; AGX broker disabled. No hardware protocol, ACPI or driver change.

TRANSLATION: Existing helper validates GPT, NTFS geometry and serial before any alias. Resolve each enumerated name to a native target, create T: and R: only if vacant, verify resolved alias. Existing script again checks serial and Windows files, copies and compares SYSTEM to ESP before CHKDSK /f /x. Aliases vanish on reboot. No persistent drive-letter change or hive replacement.

WHAT IS STILL UNKNOWN: Whether native aliases allow this WinPE boot to pass the backup gate and whether CHKDSK repairs the SYSTEM.LOG2 metadata corruption. Test only this naming change on hardware; retain backup guard.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Native x64 test exercises temporary alias resolution/read, idempotence, occupied-letter refusal without target replacement, and exact cleanup. Existing identity matcher checks GUID, serial and size. No artificial RED for environment-specific error87; preserve photo as actual failing evidence.

Checkpoint: verified aliases -> SYSTEM duplicate byte match -> CHKDSK log and automatic reboot. Failure before CHKDSK pauses, with receipts. Recovery: exact-owner reboot only after observed pre-repair pause, or normal automatic completion then immutable377/392 broker-disabled Windows. Do not interrupt potentially active CHKDSK.
