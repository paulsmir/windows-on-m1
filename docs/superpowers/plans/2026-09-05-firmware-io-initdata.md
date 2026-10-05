# Firmware IO and initdata offline plan

Spec:docs/superpowers/specs/2026-09-05-firmware-io-initdata-design.md. Approved continuing ladder, no replacement root/backend. Work in existing workspace, preserve unrelated changes.

1. Core task: extend retained EL2 owner with fixed T8103 IO inventory, exact77-leaf validation and rollback/stopped-close. Only native root core/header and dedicated tests; parent owns manifest ABI/transport/Windows codec. Agree manifest record ABI before code. Test real combined graph and every allocator failure; commit coherent core.
2. Parent: shared bounded versioned read-only manifest/descriptor codec, native fixed-window exporter and Windows serializer into existing HwdataB. Keep public RAM mapping unchanged. Test malformed/stale/readonly/window bounds and unchanged-on-failure encoding.
3. Focused review both ownership and integration. Continue offline audit/materialization of remaining HwdataA/B defaults, ADT performance inputs, pointer fields and related initdata contents. No hardware until coherent native initdata bytes are ready.
4. Once complete: frozen26100 build/native build/analysis/Universal/sign/version/hash, exact preregistered next hardware candidate; collect new first boundary and cleanup. Do not repeat474/475 or send workload prematurely.

Self-review: firmware IO is an additional EL2-owned class, not Windows rawPA admission; broker resource fits existing Mu4KiB. No duplicate UAT algorithm. The known zero HwdataA/B issue explicitly blocks initdata hardware, not independent offline implementation.
