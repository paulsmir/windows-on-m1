# EXP921: fresh DWM still does not enter physical presentation

One held-handle, non-critical console DWM reinitialization actually ran on exact
package30.0.920.0/source d16f42c0. Original boot23:14:25.640379Z reachedCode0/CPU8;
DWM1224 was replaced by1228 at23:17:04, Explorer5168 survived, same boot/Code0.
Before466B SHA2f1c1c50 andafter502B SHAd0e48c9b were independently host verified.
The transition-only hypothesis is rejected: the fresh process completes initial
GPU work (submit2/complete1/fence16427) but physical and virtual Present remain0.
No TDR capture. A replacement PID does not prove working display.

45012 new-PID DWM events show Apple/FL10 HWDevice409 and two failed display-set
derivations201 (fSucceeded0). Its UMD log contains no native DXGI Present,
SetDisplayMode or other DXGI entry. These observations locate the boundary before
primary/presentation entry; they do not identify the exact internal refusal.
8268 DxgKrnl events all have Level0; the exploratory count labelled ERRORLEVEL
was not an error count. Internal event20 Status5 is not a proven failed NTSTATUS.
ETW converted wall times differ from process-receipt UTC; use PID/QPC correlation,
not a false conclusion that the new process emitted events before it existed.

The user explicitly removed long waits while Present0. Monitor stopped atitslast
sample421.982s; originalcollector23:22:38, orderedrestart23:30:44. DCP +120/+300/+600
same-current-surface samples remainedzero during collection/control work; no
stability acceptance is claimed. Windows kept SSH briefly during ordered shutdown
with Explorer gone, then owner exited and USB reenumerated. No host signal/reset.

Original16file gate a094f864; frozen98filegate1d9c60da:5complete/1oldpartial;
lastretained232151 snapshot recovered separately andsize/SHAverified, thenonlyits
exactguestduplicate removed. Partialhostcopy preserved. Original collector's first
EncodedCommand was rejected for command length before execution; short hash-pinned
file invocation succeeded. Recovery15filegatecd1d6b22 preceded Code43 exactcleanup.
Normal377/392 final Code28 verifiedtwice23:42:28/23:43:23: oneAPPL0002,
package/module/service/signer/arm/diagnostics0, CPU8/disks2USB5/SSH/RDPservice/autologon1.
Cleanup3filegatef45fbfbc. EarlierCIMBoot23:39:38 laterreported23:42:05 duringarchive;
record the difference without inventing a physical reboot. No hidden recovery.

Host capacity:8 closed ETLs werefsynced,size/SHAverified thenrelocated withoriginal
paths preserved aslinks to internal2026-10-02/EXP921-completed. Oldpartial notmoved.
All old receipts/hashes remainvalid; archive targets mustbe preserved.

User-authorized early<=600 cleanup completed:304folders,2294files,312729300logical
bytes;315084800allocated inreadonlyaudit. The314728960B tar SHA9e014c6c and776109B
manifest SHA39ca548b are atinternal guest-EXP001-600. Every regular member wasread
and independentlysize/SHAmatched beforeeachsourcefolder wasrehashed/removed.
No reparse oractiveconfiguration references; remainingearlyfolders0. Guesttarcopy
removed onlyafterhostverification. Initial archivefinalmetadata failed onorderedmap
Measure-Object aftertarcompletion; independentreadonlyreceipt recovered it, no
archive overwrite. Perfolderdeletionlog andfinalreceipt arehostverified. Freshfree
4992929792; this doesnotclaim67GBdeleted. Personal/Windows/pagefile/recovery untouched.

Next: analysis/EXP922-fullscreen-sdk-present-plan.md. A typed public fullscreen
primary/clear/onePresent workload distinguishes the untested physical primary path
from already-successful composition-only creation. ARM64 SDKclient f8b7f525 built
withW4/WX/PREfast andexplicit26100 includes/libs. It isnotyetstaged/run; no capability,
NO_REDIRECTION, firmware ordriverpackage change. No ambient750s wait, no DWM retry.
If a real refusal is reproduced, fix its owner with an actual regression test.
Desktop remains unproved and no package isaccepted.
