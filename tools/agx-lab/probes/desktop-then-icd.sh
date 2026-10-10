#!/bin/bash
# Probe: the desktop control first (EXP1143 order, no GL process yet), then the
# GL smoke test. usage: desktop-then-icd.sh <evidence dir> <local ICD dir> [mode]
# Example of composing probes: run-probe.sh passes the evidence dir and the
# extra arguments to the probe it is given.
P=$(dirname "$0")
bash "$P/desktop-loads.sh" "$1" 1
bash "$P/icd-smoke.sh" "$1" "$2" "${3:-}"
