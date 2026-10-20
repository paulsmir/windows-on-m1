# C15 Windows ARM64 custom backing allocator checkpoint

The pinned external dxil-spirv composite uses `ChainAllocator::Block` to request
64-KiB backing blocks. Earlier host tests injected failures at the custom
allocation site, but the Windows ARM64 `/MD` exception and cleanup path was
unmeasured. This is a CPU-only test dependency; no external implementation is
copied into Git, no D3D12 DDI or shader model is enabled, and no AppleAgx
package is installed.

The reviewed EXP1542 native `dxil-utils.lib` has one allocator translation
unit. EXP1746 built a fresh test-only library from that same source with only
an allocation-hook include and one `malloc` call redirected to the hook. The
test hook is an original scratch implementation. The EXE links the three
reviewed current-project objects and the other eight hash-pinned native
libraries. Its AA64 PE, `/MD` inputs, source dependencies, new library symbols
and CRT/system imports passed independent review in
`.local/c15-windows-backing1746/root-review1746.json` (SHA-256
`d349bf9969250823c39e5a4a873cbda26cb58d2eb4cb2a3a49048e683a07f3cf`).

EXP1747 ran the exact EXE on the ordinary J313 Windows ARM64 guest with fault
ordinal zero. The one-key DXIL input used four matching 64-KiB backing
allocations and produced 980-byte owned SPIR-V/one binding key; the mixed input
used five and produced 2152 bytes/five keys. Both exited zero with empty
stderr. The Windows one-key count differs from the host's three, so the
Windows sweep used the measured counts.

EXP1748 then ran N=1..4 and N=1..5 as nine separate guest processes. Each
one-shot allocation failure occurred at its selected ordinal, yielded public
status 3 and a bytezero result, and was followed by a successful conversion
of the same input on the same thread. All nine children exited zero with
empty stderr; the guest continued to answer SSH. Raw receipt SHA-256
`8b1b05be5c185a1650e17b34f68b085c28bb9ff8050a46a0dcf611dbe19dfe38`;
independent review SHA-256
`b972f4c49eba38f5f97d5dfeeb0cd82438c099f6b8143f4ce79092be25d7bbc6`.

EXP1749 built a fresh persistent-hook EXE with the same reviewed native
closure; EXP1750 then ran the same measured nine positions. In each child,
the hook stayed armed through the failed job. Independent review found exactly
one matching failure and attempts exactly equal to N, so cleanup did not
request another matching 64-KiB backing block before returning. Each public
failure was status 3 with a bytezero result; the next job on the same thread
returned valid owned output. All nine children exited zero, stderr was empty,
and SSH remained available. Exact receipt SHA-256
`2869710b4bc1881cc8ca9347556299f1e4e6824137643a935f061c984eb5a10a`;
review SHA-256
`bd368d540120d587270b2a20d48bdb133b6f498ccb6bc5890b2cb8d88597d3cd`.

This finite observed custom backing category now has one-shot and persistent
Windows ARM64 `/MD` evidence for two fixture shapes. It does not establish
unobserved allocations, arbitrary DXIL inputs, whole-library sanitizer
coverage, typed CORE_0010 admission or AGX execution. Return to a distinct
C15 contract gate rather than repeating this allocation sweep without new
source-site or fixture evidence.
