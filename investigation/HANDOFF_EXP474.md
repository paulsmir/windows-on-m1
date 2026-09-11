# EXP474 — retained-root management closed

HARDWARE PROVEN:
- Same retained root0x9fff78000 across PREPARE/ACTIVATE/MAP/QUERY/UNMAP/CLOSE.
- Firmware-owned private prefix unchanged; no Windows raw private access.
- Broker system/crashlog mapping + Windows-owned16KiB probeVA0xffffffa010000000,
  queryPA0x9df368000 equals existing DXGK/HVC physical object; exact unmap/close0.
- Raw RX IOP0x0070000000000020 and AP0x00b0000000000020; RTKit result0/RX6.
- Management qualifier stops before application endpoint20; reverse cleanup and
  powerOFF succeed. Retained-root hypothesis confirmed for this boundary.

FINAL BOUNDARY:
Management PASS only. FirmwareResult5/PnP43 are intentional qualifier refusal,
not old management timeout. No full FirmwareStart/backend/queues/TA3D proof.
Candidate later recorded Event12911769; do not call its boot error-free.

COMMITS / ARTIFACTS:
- Root platform pin f1b2dc0265572ff65fc56dcc63d564f20ca72c7c.
- Windows integration5cbed0eddd882cf96ca7a14843bcc24b109d1c7e; raw-context guardfba9ce9.
- m1n1 current9cf6c9d9fffdf191108d222411df025e5148fef5; core0ab43fe + fix23b5d5b,
  integration3d94801 + guardcd98afa + backing reservation9cf6c9d.
- Native858c8d9f449cd762d8e027d2fe3acbbfbae0a34a58d565dc09d136ce25ea35c7;
  signed474 ZIP3c59dcb4aedeeedf63b608802d0f20026517cdad15c891feb64cf2976c40d702.
- Native archive requires shared UAT sources/headers from the exact driver
  source archive; both are under .local/experiments/EXP474-retained-root.
-17 focused tests and native host suite GREEN; WDK/UMD/Universal/signing/version
  gates pass, two pre-existing analysis warnings retained.

CURRENT CLEAN BASELINE:
Ordinary EXP377/392, broker disabled, one inert APPL0002 Code28/null INF/service.
Exact474 package and stale service removed. Final10:15:27Z at167seconds uptime:
SSH/sshd,8CPU,input/NVMe/xHCI healthy; no AppleAgx package/module/SYS/UMD;
no41/129/1001 since restored boot. Evidence final-ordinary-health.json.

NEXT FOR TERRA (not executed):
Integrate the existing production context0 mapping inventory with the proven
retained-root broker before enabling application endpoints/initdata. Current
API deliberately supports at most16 exact16KiB Windows maps in its permitted
window; only one probe was exercised. Preserve firmware/system ownership,
exact unmaps, unchanged context63/HVC/IRQ and full backing validation.
Do NOT just remove RetainedRootQualification: the full profile still contains
the rejected copied-root path. Do not replay EXP472/473. No scheduler/scanout/
UMD/acceleration work is part of this completed session.

EVIDENCE:
- result.json SHA75874c7ee666f71abd2c2d0030b487b973faa70b2deed0b17178d7225f71dce8.
- decoded-verdict.json contains human-readable broker receipts and complete
  bounded management RX/TX trace; hardware-window.log contains owner receipts.
- final health SHA7e2d82ea1d7cc71580073e48a46db64a8eb1822dd5aa728a167d40956b0c0cdd.
