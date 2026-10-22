#!/bin/bash
# Drive the 3DMark UI (already started maximized or windowed) to Night Raid > Custom run and set the
# toggles; physical 2560x1600 coordinates measured on J313 (EXP1876G/EXP1878G).
# usage: nightraid-custom.sh [--gt1 on|off] [--gt2 on|off] [--cpu on|off] [--window on|off] [--noasync on|off] OUTSHOT
# Toggles assume 3DMark's defaults after a start (GT1/GT2/CPU on, window off, async on). Then RUN with:
#   guest-ui.sh click 2184 502
G="$(dirname "$0")/guest-ui.sh"
gt1=on; gt2=on; cpu=on; win=off; noasync=off
while [ $# -gt 1 ]; do case $1 in --gt1) gt1=$2;; --gt2) gt2=$2;; --cpu) cpu=$2;; --window) win=$2;; --noasync) noasync=$2;; esac; shift 2; done
out=$1
$G click 1862 312 >/dev/null; sleep 3          # maximize (windowed 3DMark title bar button)
$G click 1979 80 >/dev/null; sleep 4           # BENCHMARKS
$G wheel 1280 1300 -1800 >/dev/null; sleep 3   # scroll to Night Raid
$G click 1267 380 >/dev/null; sleep 4          # Night Raid card
$G click 441 651 >/dev/null; sleep 3           # Custom run tab
[ $gt1 = off ] && { $G click 765 803 >/dev/null; sleep 1; }
[ $gt2 = off ] && { $G click 765 853 >/dev/null; sleep 1; }
[ $cpu = off ] && { $G click 765 902 >/dev/null; sleep 1; }
[ $win = on ] && { $G click 765 1016 >/dev/null; sleep 1; }
[ $noasync = on ] && { $G click 1912 862 >/dev/null; sleep 1; }
$G shot "$out" >/dev/null && sips -c 380 1600 --cropOffset 380 0 "$out" --out "${out%.png}-settings.png" >/dev/null
echo "${out%.png}-settings.png"
