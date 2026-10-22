#!/bin/bash
# Reinstall a previous experiment's package unchanged under a new experiment number
# (no rebuild): copy the builder output directory OLD_DIR to NEW_DIR so prepare.sh
# fetches the same package and build receipt. The driver version stays the old one;
# clone_experiment.py takes the versions from the INF DriverVer (EXP1878 lesson: a
# rebuild only to renumber the version cost a build and gated nothing new).
# usage: reuse_package.sh OLD_DIR NEW_DIR
set -e
. "$(dirname "$0")/../lib.sh"
O=$1; N=$2
builder "cmd /c \"if exist $BH_WIN\\$N (echo NEW_DIR_EXISTS & exit 1) else (robocopy $BH_WIN\\$O $BH_WIN\\$N kmd-build-receipt.json build.stdout & robocopy $BH_WIN\\$O\\package $BH_WIN\\$N /E >NUL & echo COPIED)\"" | tr -d '\r' | tail -1
builder "cmd /c \"copy /y $BH_WIN\\$N\\build.stdout $BH_WIN\\$N\\arm64-build.log >NUL & dir /b $BH_WIN\\$N\"" | tr -d '\r'
