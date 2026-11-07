# Preserve arguments when a root-signature handle is unchanged

Source-first correction, prepared while GPU EXP1562 runs its frozen source
`f42452fc`. This note changes no deployed package or capability.

## WINDOWS CONTRACT:

Microsoft's [root-signature semantics](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-a-root-signature)
distinguish changing the root signature from setting the same signature again.
A change invalidates old bindings; redundant selection of the current signature
preserves them. Graphics and compute have independent root state. This also
matters to bundle inheritance: a bundle may select its caller's signature and
update only some inherited parameters. This correction does not implement bundles.

The pinned WDK 26100 `d3d12umddi.h`, SHA-256
`d338e00bbd079cfed42434b5bd8cb9c658238cfe22747fb7d3b7cff3a3cabc28`,
defines both typed command-list root-signature setters. Current original
`agx_d3d12_ddi.cpp` implementations always zero the selected bank's VA and
constant arrays, including when the supplied nonnull handle is already current.
`ClearRootArguments` separately clears both banks and keeps their signatures.

## AGX/ASAHI CONTRACT:

The existing draw/dispatch recording code consumes those root arrays and the
native owner resolves their binding data. There is no AGX command to issue when
an unchanged CPU root-signature identity is selected. Asahi submission, native
compiler, m1n1, Mu, UAT, interrupts, power and recovery are unaffected.

## TRANSLATION:

Compare the incoming nonnull private root-signature pointer with the selected
bank before clearing arguments. Preserve the bank on equal identity; retain the
existing clear for a different signature or a null selection. Leave the opposite
bank and explicit `ClearRootArguments` semantics intact. A diagnostic on the
redundant branch can distinguish actual DDI forwarding from runtime API elision.
Its three values identify the bank and count nonzero VA/constant entries. Empty
runtime initialization must not masquerade as a repeated call with bound inputs.
No table layout, root serialization, descriptor encoding or capability changes.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Previously bound values vanish
after redundant selection of their same live root identity. The regression must
execute the actual current setters and list definition, seed both banks, verify
equal identity preserves them, changed identity clears only its bank, and the
explicit clear still clears both. Host stand-ins for unused list value fields
are not native ABI or root-layout evidence; real WDK compilation is separate.

## WHAT IS STILL UNKNOWN:

The physical runtime may suppress redundant public API calls. A later fresh
compute/graphics probe must check output and actual redundant-branch receipts;
absence of the receipt limits the verdict to public API behavior. A CPU setter
result alone does not establish GPU execution, bundle support or SM6 admission.

## Checkpoint and recovery

First reproduce data loss with the current-source CPU regression, then implement
the narrow identity guard and run relevant tests and the WDK compile gate. A
later probe selects a root, binds nonzero constants and buffer descriptors,
selects the identical root again, and checks compute words/graphics pixels.
Each bank has its own isolated-device PASS/FAIL step and receipts. Use the
ordinary signed-package gates and exact GPU-visible rollback. EXP1562 stays
unchanged and must finish evidence collection and recovery before a new install.
