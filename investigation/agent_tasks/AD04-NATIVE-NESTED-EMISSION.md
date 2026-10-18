# Nested native emission capture

The existing capture may now hold a source-defined parent emission interval
while a nested USC interval is built. Child completion restores the parent
scope, rather than clearing the request-wide active-emission marker. All ranges
remain owner/generation/CPU/construction validated and source holds retain the
existing Abort/Retire lifecycle. This is caller-serialized and no command-byte
pointer scan is added.

The controlled original pool test uses separate real pool-backed BOs for parent
state, USC source and descriptor target. It verifies child pointer record and
parent scope restoration. A first fixture attempt deliberately exposed overlap
rejection when parent and descriptor aliases shared one BO; the test now models
the disjoint production ownership expected for separately allocated ranges. The
alias guard remains unchanged.

Source033 SHA256 dfecae0b02c90894c214482966f8b6c5c428fe94fdbdb60b8a24992cd4f16652.
Pinned Windows x64 build/link/execution PASS; host relevant suites PASS. Raw
evidence: investigation/evidence/AD04-native-nested-emission/. No hardware.

Next: invoke generic parent range capture from source-defined native state/VDM
emission points. Initial encoder must receive explicit Encoder allocation intent;
General-backed continuation rollover remains fail-closed until separate contract.
