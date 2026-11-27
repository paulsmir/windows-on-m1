#!/bin/bash
# nonet-triage.sh <EXP dir> : host-side evidence when a launch ends ssh-never-up (read-only, ~30 s). Prints NONET_* lines.
# Separates: guest dead (no HV exits / bugcheck) vs guest alive without network (ARP incomplete, IRQ bursts) vs host network down.
X=$1; T=${AGX_TANDEM:?set AGX_TANDEM to the folder holding airssh}; GIP=${AGX_GUEST_IP:?}; NET=${AGX_LAN_PREFIX:?e.g. 10.0.0}; RTR=${AGX_ROUTER_IP:?}; BLD=${AGX_BUILDER_IP:?}; FL=$X/full.log
echo "NONET_RUN_UEFI $(pgrep -f 'run_uefi[.]py' | head -1 || echo none)"
echo "NONET_FULLLOG lines=$(wc -l < $FL | tr -d ' ') age_s=$(( $(date +%s) - $(stat -f %m $FL) )) bugcheck=$(grep -a -c 'BUGCHECK' $FL) exits_lines=$(grep -a -c 'HV: EXITS' $FL) dcp_swaps=$(grep -a -c 'exact D589 latch' $FL)"
echo "NONET_IRQSTAT_LAST $(grep -a 'HV: IRQSTAT' $FL | tail -3 | sed 's/.*IRQSTAT //' | tr '\n' '|')"
echo "NONET_SSH $(perl -e 'alarm 15; exec @ARGV' $T/airssh -o ConnectTimeout=8 'echo ALIVE' </dev/null 2>&1 | tr -d '\r' | tail -1)"
ping -c 1 -t 2 $GIP >/dev/null 2>&1; echo "NONET_ARP $(arp -n $GIP 2>&1 | head -1 | sed 's/ at [0-9a-f:]* / at <mac> /')"
echo "NONET_HOSTNET router=$(ping -c 1 -t 2 $RTR >/dev/null 2>&1 && echo ok || echo DOWN) builder=$(ping -c 1 -t 2 $BLD >/dev/null 2>&1 && echo ok || echo DOWN)"
ping -c 2 -t 2 $NET.255 >/dev/null 2>&1; n=0; for a in $(arp -an | grep -v incomplete | sed -n 's/.*(\([0-9.]*\)).*/\1/p' | grep "^$NET\." | sort -u); do case $a in $RTR|$BLD|$GIP|$NET.255) continue;; esac; perl -e 'alarm 4; exec @ARGV' nc -z -G 2 $a 22 >/dev/null 2>&1 && { n=$((n+1)); echo "NONET_LAN_SSH $a"; }; done; echo "NONET_LAN_SSH_OPEN $n"
