#!/bin/bash
# guest-ui.sh shot OUT | click X Y | wheel X Y DELTA | key TEXT -- physical coordinates, interactive session via a scheduled task
# Guest side: copy tools/agx-lab/guest/realapp/*.ps1 and arm-interactive.ps1 to C:\Users\pavel\realapp first.
T=/Users/pavel/public_windows/.local/tandem; S=${TMPDIR:-/tmp}
G() { perl -e 'alarm 60; exec @ARGV' $T/airssh -o ConnectTimeout=10 "$@" 2>/dev/null | tr -d '\r'; }
run() { printf '%s\n' "$1" > $S/ui-op.ps1; perl -e 'alarm 60; exec @ARGV' $T/airscp $S/ui-op.ps1 pavel@192.168.1.37:C:/Users/pavel/realapp/ui-op.ps1 >/dev/null 2>&1
        G 'powershell -NoProfile -ExecutionPolicy Bypass -File C:\Users\pavel\realapp\arm-interactive.ps1 -TaskName AgxClick -Script C:\Users\pavel\realapp\ui-op.ps1' >/dev/null; sleep 2; }
case $1 in
 shot) G 'powershell -NoProfile -ExecutionPolicy Bypass -File C:\Users\pavel\realapp\arm-interactive.ps1 -TaskName AgxShot -Script C:\Users\pavel\realapp\ui-shot.ps1' >/dev/null; sleep 5
       perl -e 'alarm 60; exec @ARGV' $T/airscp pavel@192.168.1.37:C:/Users/pavel/realapp/ui-shot.png "$2" >/dev/null 2>&1; sips -Z 1600 "$2" >/dev/null; echo "$2";;
 click) run "& C:\\Users\\pavel\\realapp\\ui-click.ps1 -X $2 -Y $3"; echo clicked;;
 wheel) run "& C:\\Users\\pavel\\realapp\\ui-click.ps1 -X $2 -Y $3 -Wheel $4"; echo wheeled;;
esac
