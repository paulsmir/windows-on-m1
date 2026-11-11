# quiet-gate.sh (sourced by probes from EXP2330 on; architect 2026-11-10T21:56:52Z): qgate <tag> waits in 60 s steps
# (20 min bound) while Windows servicing processes run; prints QGATE lines with the process list and available MB;
# returns 1 (INVALID-ENV) if they are still present. Never kills them, never touches update settings.
qgate() { local tag=$1 i out busy
  [ "$tag" = first-arm ] && perl -e 'alarm 30; exec @ARGV' "$T/airssh" -o ConnectTimeout=8 'reg query "HKLM\SOFTWARE\Microsoft\WindowsUpdate\UpdatePolicy\Settings"' 2>/dev/null | tr -d '\r' > "$E/wu-pause-status.txt" && echo "WU_PAUSE_STATUS $(grep -a -E 'PausedQualityStatus|PausedFeatureStatus' "$E/wu-pause-status.txt" | tr -s ' ' | tr '\n' ';')" | tee -a "$E/qgate.txt"
  for i in $(seq 0 20); do
    # 2026-11-10T23:56Z fix: the query goes as -EncodedCommand. The quoted form broke in cmd.exe (the regex "|" became a
    # pipe: "TrustedInstaller is not recognized"), so every poll of EXP2331 read no-answer. Decoded text:
    # $n=@(Get-Process -EA 0 | ? { $_.ProcessName -match "^(TiWorker|TrustedInstaller|wuauclt.*|MoUsoCoreWorker|usoclient|DismHost|msiexec|CompatTelRunner)$" } | % { $_.ProcessName + ":" + [int]($_.WorkingSet64/1MB) }); $m=[int]((Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory/1024); "BUSY=" + ($n -join ",") + " AVAIL_MB=$m"
    out=$(perl -e 'alarm 40; exec @ARGV' "$T/airssh" -o ConnectTimeout=8 'powershell -NoProfile -EncodedCommand JABuAD0AQAAoAEcAZQB0AC0AUAByAG8AYwBlAHMAcwAgAC0ARQBBACAAMAAgAHwAIAA/ACAAewAgACQAXwAuAFAAcgBvAGMAZQBzAHMATgBhAG0AZQAgAC0AbQBhAHQAYwBoACAAIgBeACgAVABpAFcAbwByAGsAZQByAHwAVAByAHUAcwB0AGUAZABJAG4AcwB0AGEAbABsAGUAcgB8AHcAdQBhAHUAYwBsAHQALgAqAHwATQBvAFUAcwBvAEMAbwByAGUAVwBvAHIAawBlAHIAfAB1AHMAbwBjAGwAaQBlAG4AdAB8AEQAaQBzAG0ASABvAHMAdAB8AG0AcwBpAGUAeABlAGMAfABDAG8AbQBwAGEAdABUAGUAbABSAHUAbgBuAGUAcgApACQAIgAgAH0AIAB8ACAAJQAgAHsAIAAkAF8ALgBQAHIAbwBjAGUAcwBzAE4AYQBtAGUAIAArACAAIgA6ACIAIAArACAAWwBpAG4AdABdACgAJABfAC4AVwBvAHIAawBpAG4AZwBTAGUAdAA2ADQALwAxAE0AQgApACAAfQApADsAIAAkAG0APQBbAGkAbgB0AF0AKAAoAEcAZQB0AC0AQwBpAG0ASQBuAHMAdABhAG4AYwBlACAAVwBpAG4AMwAyAF8ATwBwAGUAcgBhAHQAaQBuAGcAUwB5AHMAdABlAG0AKQAuAEYAcgBlAGUAUABoAHkAcwBpAGMAYQBsAE0AZQBtAG8AcgB5AC8AMQAwADIANAApADsAIAAiAEIAVQBTAFkAPQAiACAAKwAgACgAJABuACAALQBqAG8AaQBuACAAIgAsACIAKQAgACsAIAAiACAAQQBWAEEASQBMAF8ATQBCAD0AJABtACIA' 2>/dev/null | tr -d '\r' | grep -a 'BUSY=' | head -1)
    echo "QGATE $tag t=${i}min ${out:-no-answer}" | tee -a "$E/qgate.txt"
    busy=$(echo "$out" | sed -n 's/^BUSY=\([^ ]*\) .*/\1/p')
    [ -n "$out" ] && [ -z "$busy" ] && return 0
    [ $i -lt 20 ] && sleep 60
  done
  [ -z "$out" ] && { echo "QGATE $tag TOOL-FAIL no answer from the guest query after 20 min (not a servicing verdict)" | tee -a "$E/qgate.txt"; return 2; }
  echo "QGATE $tag INVALID-ENV servicing still running after 20 min" | tee -a "$E/qgate.txt"; return 1; }
