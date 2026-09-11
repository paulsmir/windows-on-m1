# Delegation usage record

| Task ID | Tier | Worker | Input/return size | Raw evidence needed by architect | Result | Rework |
| --- | --- | --- | --- | --- | --- | --- |
| AD04-CLANG-DISCRIMINATOR | C | deterministic approved SSH runner; Devstral API has no tool executor | compact return + raw 5.5KB diagnostic | no | FAIL: Clang reached intrinsic spelling before agx_index/off_t; no source change | no |
| ORCH-DRY-RUN-001 | C | local mechanical control | small / compact JSON | no | PASS: create, clean scope, evidence and cleanup | no |

This table is intentionally lightweight. It tracks whether compression actually
avoided unnecessary architect context, not model-accounting estimates.
