# ANS001 RAM-only collector sources

These are the exact startup and collector bytes embedded in both indices of
ANS001-PREP-D, image SHA256
3db37261578a0fb2793e980f5d0e737adc7d7bab2eb1529e9cf0ec70ce29e8da.
CRLF bytes are preserved explicitly. No AppleAns package is embedded.

The collector requires WinPE X: and an existing offline SAN policy3 before it
runs wpeinit. It performs inventory only and writes X:\ANS001-RESULTS. It does
not bring disks online, install/remove drivers, alter trust, export results or
reboot. SAN3 is set while constructing private WIM copies, not repaired at boot.

ANSCOLLECT004 executed these exact collector/startnet bytes in a disposable
software VM. Required SAN3, wpeinit, PnP/package/loaded-driver/registry inventory
and exact-identity virtual evidence export passed. The separate VM wrapper and
its invented virtual disk identity MUST NOT be used as hardware identity.

The actual supplemental sc127, DISM0x80004002 and WMIC0x8007007e failures remain
in raw output. The emitted OPTIONAL-CHECKS.CSV names sc/DISM; the final audit
also classifies WMIC as supplementary because required disk evidence is supplied
by PnP and verified DiskPart disk/partition/volume relations. No WMIC success or
all-commands success is claimed. A COLLECTED marker is never a hardware PASS.

Durable evidence and exact private-image construction commands/hashes are in
investigation/EXPERIMENTS.md (ANS001-PREP-D and ANSCOLLECT004) and
.local/ans-native/ans001-winpe/ANS001-PREP-D-result.json. The final scope receipt
is .local/ans-native/ans001-collector-vm-004/ANSCOLLECT004-mandatory-result.json.
Only a later approved operator-inventoried external medium may receive Air logs;
never guess a disk/volume number or copy to an arbitrary writable drive.
