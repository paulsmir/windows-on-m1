# GPU series run entry (R54, user decision 2026-09-24)

Before: `RUN <ID> <UTC> | series <ID> | variable <one change> | package SHA256 <hash> | m1n1 SHA256 <hash> | checkpoint <expected> | failure <criterion> | evidence <path>`

After: `RUN <ID> <UTC> | phases <observed> | stop <code/params or none> | SSH/CPU/storage/USB/display/RDP <state> | evidence <path> | verdict <confirmed/rejected/inconclusive> | next <cause or discriminator>`

R60 bugcheck continuation: record the durable receipt/registry proof that
`G3Armed`/`B1Armed` was cleared before GPU access and the exact installed
package identity. Preregister one bounded reboot of the same full-owner
profile. If pinned SSH returns, collect dump/receipts/ETL before replacing
the exact package with the next hash-verified candidate and rearming. If
either proof or SSH fails, record that result and use GPU-hidden dump-first
exact cleanup, then ordinary Code28.
