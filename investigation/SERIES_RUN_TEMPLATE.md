# GPU series run entry (R54, user decision 2026-09-24)

Before: `RUN <ID> <UTC> | series <ID> | variable <one change> | package SHA256 <hash> | m1n1 SHA256 <hash> | checkpoint <expected> | failure <criterion> | evidence <path>`

After: `RUN <ID> <UTC> | phases <observed> | stop <code/params or none> | SSH/CPU/storage/USB/display/RDP <state> | evidence <path> | verdict <confirmed/rejected/inconclusive> | next <cause or discriminator>`
