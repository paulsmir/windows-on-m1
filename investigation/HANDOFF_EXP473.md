# Handoff — stopped at retained-root identity boundary

HARDWARE PROVEN:
EXP472 RO transport reads live firmware entries and Windows context0 preserves
them byte-exact before existing mappings. Crashlog mapping/readback and grant
pass. No IOP/AP ACK. EXP470 native retained-root management passes.

FINAL BOUNDARY:
BLOCKED, not PASS. EXP473 copies exact prefix into a native-owned root using
otherwise known-good native mapping/grant and reproduces IOP/AP0 timeout.
Copied prefix is insufficient; retained-root identity/publication needs a new
architecture decision. Do not repeat copied-root or speculative cache-bit runs.

COMMITS:
- Root926dd6b575b20515bb65dddbec7bf846e2e28441: live prefix/deferred map/qualifier.
- Root9f65ea178f56930ddc9f534e8d5798c8c65d411f: CPUPrepared configuration without false mapping.
- m1n1d80a72d721f7f1a2291eba7e05670eab45776370: RO broker+0x280 view. Build requires
  shared header from root, SHA7e2cfcd061163077e9444a20b3f6ae630761b602a5ade2508fb79e5b6a3f9292.
- Mu unchanged. Root gitlink predates live m1n1: use explicit commit above.

CURRENT CLEAN BASELINE:
Ordinary EXP377 m1n1 + EXP392 Mu, broker disabled, one APPL0002 Code28/null INF
and service. Exact test packages/service removed. 00:42:01Z SSH/8CPU/input/NVMe/
xHCI healthy, no41/129/1001 since boot. No AppleAgx package/module/SYS/UMD.
Evidence .local/experiments/EXP472-prepared-prefix/final-ordinary-health.json.

NEXT FOR TERRA:
First obtain an explicit retained-root identity/publication design decision;
firmware private trees remain firmware-owned. Current read-only transport cannot
publish Windows kernel mappings into that retained root. No implementation of
that expanded contract is authorized by this handoff. Keep context63/HVC/IRQ/
handoff ownership unchanged. No RenderKm/Patch/SubmitCommand/TA3D/scanout work.

Evidence:
- EXP472/result.json SHA4336f414d0901142d6916dea93da93e71c18c7820b7e0425eee6329f199bfa19.
- EXP473/result.jsonl SHAb9256d96f9090e04320478f6c5224101c3bbaf3bdf2adedfea2de69b941ea0b2.
- Directories .local/experiments/EXP472-prepared-prefix and EXP473-native-copied-root.
-27 focused tests GREEN; KMD/UMD/Universal/signing/version gates pass, two old
analysis warnings retained. Qualifier stops before application endpoints.
