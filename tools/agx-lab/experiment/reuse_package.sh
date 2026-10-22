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
builder "cmd /c \"if exist $AGX_BUILDER_HOME\\$N (echo NEW_DIR_EXISTS & exit 1) else (robocopy $AGX_BUILDER_HOME\\$O $AGX_BUILDER_HOME\\$N kmd-build-receipt.json build.stdout & robocopy $AGX_BUILDER_HOME\\$O\\package $AGX_BUILDER_HOME\\$N /E >NUL & echo COPIED)\"" | tr -d '\r' | tail -1
builder "cmd /c \"copy /y $AGX_BUILDER_HOME\\$N\\build.stdout $AGX_BUILDER_HOME\\$N\\arm64-build.log >NUL & dir /b $AGX_BUILDER_HOME\\$N\"" | tr -d '\r'
