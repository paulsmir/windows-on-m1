#!/bin/bash
# diskgate2.sh: two-part disk gate for pwdev as a sparsebundle on the SSD (architect 2026-11-27, bench-verified band arithmetic).
# PASS needs inside_free = image total - used >= INSIDE_MIN_MIB (8192) AND ssd_free >= worst_growth + MARGIN_MIB (1024), where
# worst_growth = (max_bands - bands) x (band + 128 KiB ._ cluster) + sum over existing real bands (._* excluded) of (band - size).
# Also the system data volume >= SYS_MIN_MIB (2048). Mock inputs: DG2_TOTAL DG2_USED DG2_SSDFREE DG2_BANDS DG2_SHORTDEF_MIB DG2_SYSFREE.
B=/Volumes/SSD/pwdev.sparsebundle
TOTAL=${DG2_TOTAL:-$(df -m /Volumes/pwdev | awk 'NR==2{print $2}')}; USED=${DG2_USED:-$(df -m /Volumes/pwdev | awk 'NR==2{print $3}')}
SSDF=${DG2_SSDFREE:-$(df -m /Volumes/SSD | awk 'NR==2{print $4}')}; SYSF=${DG2_SYSFREE:-$(df -m /System/Volumes/Data | awk 'NR==2{print $4}')}
BS=$(plutil -extract band-size raw $B/Info.plist 2>/dev/null || echo 8388608); SZ=$(plutil -extract size raw $B/Info.plist 2>/dev/null || echo 322122547200)
MAXB=$(( SZ / BS )); NB=${DG2_BANDS:-$(ls $B/bands | wc -l | tr -d ' ')}
sdef(){ find $B/bands -type f ! -name '._*' -exec stat -f %z {} + 2>/dev/null | awk -v bs=$BS '{d+=bs-$1} END{printf "%d", d/1048576}'; }
if [ -n "${DG2_SHORTDEF_MIB:-}" ]; then SDEF=$DG2_SHORTDEF_MIB; else SDEF=$(sdef); fi
IN=$(( TOTAL - USED )); AD=$(( (MAXB - NB) * 128 / 1024 )); WG=$(( (MAXB - NB) * BS / 1048576 + SDEF + AD ))
NEED=$(( WG + ${MARGIN_MIB:-1024} ))   # AD: each new band also gets an AppleDouble ._ file = one 128 KiB exFAT cluster
V=PASS; [ $IN -ge ${INSIDE_MIN_MIB:-8192} ] || V=FAIL; [ $SSDF -ge $NEED ] || V=FAIL; [ $SYSF -ge ${SYS_MIN_MIB:-2048} ] || V=FAIL
echo "DISKGATE2 $V inside_free_mib=$IN ssd_free_mib=$SSDF bands=$NB/$MAXB worst_growth_mib=$WG (unalloc $(( (MAXB-NB)*BS/1048576 )) + short $SDEF + appledouble $AD) need_ssd_mib=$NEED sysdata_free_mib=$SYSF floors inside=${INSIDE_MIN_MIB:-8192} margin=${MARGIN_MIB:-1024} sys=${SYS_MIN_MIB:-2048}"
[ $V = PASS ]
