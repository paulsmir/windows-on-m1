#!/bin/bash
# SPDX-License-Identifier: MIT
# MURTC0 (plan B20 stage 0) = standing Mu MUCPCWIFISMCIRQ01 (170a91b4) + J313_RTC_ANCHOR=TRUE, nothing else:
# RealTimeClockRuntimeDxe links AnchorRealTimeClockLib (ADT /chosen UTC anchor, read-only) instead of the uptime-only
# VirtualRealTimeClockLib. Form of repro-agxirq.sh / the power line's repro-thermal.sh. Offline: docker --network none.
# Gates:
#   G1 the patched tree with the flag unset rebuilds 170a91b4 bit for bit (the change is inert when off)
#   G2 only RealTimeClockRuntimeDxe.efi differs between the base and the RTC build (every other .efi identical)
#   G3 every ACPI table byte-identical, same table set
#   G4 tools/verify_j313_agx_irq_aml.py PASS on the RTC .fd (889 x2, 887 x2)
# Refuses to run while a bench launcher is active or with < 10 GiB free on the system data volume.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=/Users/pavel/pwdev-offload/MUCPCWIFISMCIRQ01          # standing tree (APFS clone source)
O=/Users/pavel/pwdev-offload/MURTC0                        # new work folder
R=/Volumes/pwdev/public_windows/.claude/worktrees/gpu-perf-parity-aaa98a   # verify_j313_agx_irq_aml.py
BASE_WANT=170a91b4d7470450640e25d915785afcb1719b58deb69c07f2c2a8fae6369b6b
if pgrep -f 'run_uefi|stagerun|launch-[a-z0-9]*\.sh|run_guest\.py' >/dev/null; then echo "REFUSED: a bench launcher is active"; exit 3; fi
FREE=$(df -g /System/Volumes/Data | awk 'NR==2{print $4}'); [ "$FREE" -ge 10 ] || { echo "REFUSED: only ${FREE} GiB free"; exit 3; }
[ -d "$O" ] || { mkdir -p "$O"; cp -c -R "$SRC/work" "$O/work"; }
M=$O/work/mu; S=$M/Silicon/Apple/AppleSiliconPkg; P=$M/Platform/MacBookAirMid2020Pkg
BD=$M/Build/MacBookAirMid2020-AARCH64/RELEASE_CLANGPDB; FD=$BD/FV/J313MACBOOKAIRMID2020_EFI.fd
FLAGS='BLD_*_J313_KBD_BACKLIGHT=TRUE BLD_*_J313_WIFI_PCIE=TRUE BLD_*_J313_AGX_MAILBOX_IRQ=TRUE'
run() { docker run --rm --pull never --network none -v $O/work:/work -e "KBD_FLAG=$2" windows-on-m1-build:local sh /work/build-in-container.sh build gpu-direct > "$1" 2>&1; }
keep() { rm -rf $O/$1; mkdir -p $O/$1/acpi; cp $FD $O/$1/J313_EFI.fd
  find $BD/AARCH64 -path '*AcpiTables*' \( -name '*.aml' -o -name '*.acpi' \) -path '*OUTPUT*' -exec cp {} $O/$1/acpi/ \;
  (cd $BD/AARCH64 && find . -name '*.efi' -path '*/OUTPUT/*' -exec shasum -a 256 {} \; | sort -k2) > $O/$1/efi.sha256; }
# inputs: the change (library + DSC selection, default FALSE)
rm -rf $S/Library/AnchorRealTimeClockLib; cp -R $HERE/AnchorRealTimeClockLib $S/Library/
cp $HERE/new/AppleSiliconPkg.dsc.inc $S/AppleSiliconPkg.dsc.inc; cp $HERE/new/MacBookAirMid2020.dsc $P/MacBookAirMid2020.dsc
run $O/build-base.log "$FLAGS"; keep base; B=$(shasum -a 256 $FD | cut -c1-64)
echo "G1 BASE $B $([ "$B" = "$BASE_WANT" ] && echo PASS || echo FAIL) (want 170a91b4)"
run $O/build-rtc.log "$FLAGS BLD_*_J313_RTC_ANCHOR=TRUE"; keep rtc
cp $FD $O/J313_EFI-murtc0.fd; echo "RTC $(shasum -a 256 $FD | cut -c1-64)"
echo "G2 efi differences (want exactly RealTimeClockRuntimeDxe.efi):"; diff $O/base/efi.sha256 $O/rtc/efi.sha256 | grep '^[<>]' | awk '{print $1, $3}'
N=$(diff $O/base/efi.sha256 $O/rtc/efi.sha256 | grep '^>' | grep -vc 'RealTimeClockRuntimeDxe.efi' || true)
D=$(diff $O/base/efi.sha256 $O/rtc/efi.sha256 | grep '^>' | grep -c 'RealTimeClockRuntimeDxe.efi' || true)
[ "$N" = 0 ] && [ "$D" = 1 ] && echo "G2 PASS" || echo "G2 FAIL (other=$N rtc=$D)"
G3=PASS; for f in $O/base/acpi/*; do b=$(basename $f); cmp -s $f $O/rtc/acpi/$b && echo "G3 SAME $b" || { echo "G3 DIFF $b"; G3=FAIL; }; done
[ "$(ls $O/base/acpi | wc -l)" = "$(ls $O/rtc/acpi | wc -l)" ] || G3=FAIL; echo "G3 $G3"
python3 $R/tools/verify_j313_agx_irq_aml.py $O/J313_EFI-murtc0.fd && echo "G4 PASS" || echo "G4 FAIL"
echo REPRO_DONE
