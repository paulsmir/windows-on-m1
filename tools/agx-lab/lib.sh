#!/bin/bash
# Shared helpers for the AGX lab scripts. Source this file; it loads the private
# lab configuration (hosts, keys, serial ports, local paths) from $AGX_LAB_ENV or
# <main checkout>/.local/agx-lab.env. See lab.env.example for the variables.

AGX_LAB_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
if [ -z "${AGX_LAB_ENV:-}" ]; then
  _common=$(git -C "$AGX_LAB_DIR" rev-parse --path-format=absolute --git-common-dir 2>/dev/null)
  AGX_LAB_ENV=$(dirname "${_common:-$AGX_LAB_DIR/../../.git}")/.local/agx-lab.env
fi
if [ ! -r "$AGX_LAB_ENV" ]; then
  echo "agx-lab: missing configuration $AGX_LAB_ENV (start from tools/agx-lab/lab.env.example)" >&2
  return 1 2>/dev/null || exit 1
fi
# shellcheck source=/dev/null
. "$AGX_LAB_ENV"
: "${AGX_REPO:=$(git -C "$AGX_LAB_DIR" rev-parse --show-toplevel)}"
read -r -a AGX_PORTS <<< "$AGX_SERIAL_PORTS"
AGX_PROXY_PORT=${AGX_PORTS[0]}
# Guest home as a Windows path, for commands run by PowerShell on the guest.
GH_WIN=${AGX_GUEST_HOME//\//\\}
BH_WIN=${AGX_BUILDER_HOME//\//\\}

# Option arrays (macOS /bin/bash is 3.2: plain arrays only).
_guest_opts() {
  SSH_OPTS=(-i "$AGX_GUEST_KEY" -o BatchMode=yes -o ConnectTimeout="${1:-7}" -o ServerAliveInterval=5 -o ServerAliveCountMax=3
            -o StrictHostKeyChecking=yes -o UserKnownHostsFile="$AGX_GUEST_KNOWN_HOSTS")
}
_builder_opts() { SSH_OPTS=(-i "$AGX_BUILDER_KEY" -o BatchMode=yes -o ConnectTimeout="${1:-10}"); }

# guest [-t seconds] <command>: run a command on the Windows guest.
guest() {
  local t=7; [ "${1:-}" = -t ] && { t=$2; shift 2; }
  _guest_opts "$t"
  ssh "${SSH_OPTS[@]}" "$AGX_GUEST" "$@" 2> >(grep -v '^\*\*' >&2)
}

# Guest paths are relative to AGX_GUEST_HOME unless they start with a drive letter.
_guest_path() { case "$1" in [A-Za-z]:*) echo "$1" ;; .) echo "$AGX_GUEST_HOME" ;; *) echo "$AGX_GUEST_HOME/$1" ;; esac; }

# guest_put <local file>... <guest directory>
guest_put() {
  local dst=${*: -1}; _guest_opts
  scp -q "${SSH_OPTS[@]}" "${@:1:$#-1}" "$AGX_GUEST:$(_guest_path "$dst")/"
}

# guest_get <guest path or glob> <local path>
guest_get() { _guest_opts; scp -q "${SSH_OPTS[@]}" "$AGX_GUEST:$(_guest_path "$1")" "$2"; }

# guest_alive [seconds]: the guest answers SSH.
guest_alive() { guest -t "${1:-8}" 'echo up' 2>/dev/null | grep -q up; }

builder() { _builder_opts; ssh "${SSH_OPTS[@]}" "$AGX_BUILDER" "$@" 2> >(grep -v '^\*\*' >&2); }
_builder_path() { case "$1" in [A-Za-z]:*) echo "$1" ;; .) echo "$AGX_BUILDER_HOME" ;; *) echo "$AGX_BUILDER_HOME/$1" ;; esac; }
builder_put() {
  local dst=${*: -1}; _builder_opts
  scp -q "${SSH_OPTS[@]}" "${@:1:$#-1}" "$AGX_BUILDER:$(_builder_path "$dst")/"
}
builder_get() { _builder_opts; scp -q "${SSH_OPTS[@]}" "$AGX_BUILDER:$(_builder_path "$1")" "$2"; }

serial_ports_present() { local p; for p in "${AGX_PORTS[@]}"; do [ -e "$p" ] || return 1; done; }

# serial_guard <command...>: run a command holding the m1n1 serial lock.
serial_guard() {
  local a=() p; for p in "${AGX_PORTS[@]}"; do a+=(--port "$p"); done
  (cd "$AGX_REPO" && python3 scripts/air-serial-guard.py "${a[@]}" -- "$@")
}

# wait_proxy_ready: guest down, launcher gone, serial ports back, m1n1 proxy answers.
# This is the "Running proxy" state the next launcher needs.
wait_proxy_ready() {
  local i
  for i in $(seq 1 60); do guest_alive 3 || break; sleep 5; done
  for i in $(seq 1 80); do pgrep -f '[r]un_uefi' >/dev/null || break; sleep 5; done
  for i in $(seq 1 40); do serial_ports_present && break; sleep 3; done
  sleep 10
  serial_guard "$AGX_PROXY_PYTHON" "$AGX_LAB_DIR/proxy_nop.py" "$AGX_REPO" "$AGX_PROXY_PORT" 2>&1 | tail -1 | grep -q PROXY_NOP_PASS
}

# wait_guest_up [tries]: after a launch, wait for Windows SSH (60 s + tries x 5 s).
wait_guest_up() {
  local i; sleep 60
  for i in $(seq 1 "${1:-70}"); do guest_alive 4 && return 0; sleep 5; done
  return 1
}

# Guest scripts write UTF-16LE text (Out-File); print it as UTF-8.
utf16_cat() { iconv -f UTF-16LE -t UTF-8 "$1" 2>/dev/null | tr -d '\r\000'; }

# snap_receipts <evidence dir> <tag>: KMD registry receipts and the process list.
snap_receipts() {
  local E=$1 tag=$2
  guest_put "$AGX_LAB_DIR/guest/receipts.ps1" . >/dev/null 2>&1
  guest "powershell -NoProfile -ExecutionPolicy Bypass -File $GH_WIN\\receipts.ps1" | tr -d '\r' > "$E/rcpt-$tag.txt"
  mkdir -p "$E/rcpt-$tag"; guest_get 'rcpt/*' "$E/rcpt-$tag/" >/dev/null 2>&1
  guest 'powershell -NoProfile -Command "Get-Process | Sort-Object Id | ForEach-Object { [string]$_.Id + \" \" + $_.ProcessName + \" \" + $_.HandleCount + \" \" + [int]($_.PrivateMemorySize64/1MB) }"' \
    | tr -d '\r' > "$E/procs-$tag.txt" 2>/dev/null
}

# arm_interactive <guest script> [positional args]: start a guest script in the
# interactive desktop session (loads must drive the user's desktop, not SSH's).
arm_interactive() {
  local a=; [ -n "${2:-}" ] && a=" -Arguments \"$2\""
  guest "powershell -NoProfile -ExecutionPolicy Bypass -File $GH_WIN\\arm-interactive.ps1 -Script $1$a" | tr -d '\r'
}

# run_load <evidence dir> <name> <guest load script> <done file> [vsync seconds] [settle seconds]
# One user-like load with a vsync trace, receipts in the middle and its done file.
run_load() {
  local E=$1 name=$2 script=$3 done=$4 secs=${5:-42} settle=${6:-20}
  (guest "$GH_WIN\\AppleAgxVsyncTrace.exe $secs" > "$E/vsync-$name.txt" 2>&1 &)
  sleep 3
  arm_interactive "$GH_WIN\\$script"
  sleep "$settle"; snap_receipts "$E" "$name"; sleep 21
  guest_get "$done" "$E/$done" >/dev/null 2>&1
  utf16_cat "$E/$done" | sed "s/^/[$name] /" | tee -a "$E/probe.txt"
}

# guest_kill <process name>...: stop guest processes left by a load.
guest_kill() {
  local n; for n in "$@"; do
    guest -t 8 "powershell -NoProfile -Command \"Get-Process $n -EA 0 | Stop-Process -Force\"" >/dev/null 2>&1
  done
}

# upload_guest_tools: the interactive launcher, receipts, events, loads and the
# vsync tracer, into AGX_GUEST_HOME.
upload_guest_tools() {
  guest_put "$AGX_LAB_DIR"/guest/*.ps1 "$AGX_VSYNC_TRACE" . >/dev/null 2>&1 || echo "GUEST_TOOLS_UPLOAD_FAILED"
}

# guest_timeout <seconds> <command>: a guest command killed after <seconds>
# (self-tests can hang the GPU; the probe must go on).
guest_timeout() {
  local s=$1; shift; _guest_opts 8
  perl -e 'alarm shift; exec @ARGV' "$s" ssh "${SSH_OPTS[@]}" "$AGX_GUEST" "$@"
}

# post_vid <evidence dir>: the video self-test after the loads (90 s cap).
post_vid() {
  guest_timeout 90 "powershell -NoProfile -Command \"\$o = & $GH_WIN\\agx_vid_selftest.exe 2>&1; \\\"post vid exit=\$LASTEXITCODE\\\"; \$o | Select -Last 1\"" 2>&1 \
    | tr -d '\r' | grep -v '^\*\*' | tee -a "$1/probe.txt"
}

# profile_load <evidence dir> <name> <guest load script> <done file> <etl name>
# A load with a 15 s context-switch ETW capture (guest/cswitch.wprp!AgxWait) in
# its middle. Decode the ETL on the builder (builder/xperf-dump.ps1), then use
# analysis/dwm_frames.py and friends on the CSV.
profile_load() {
  local E=$1 name=$2 script=$3 done=$4 etl=$5
  guest_put "$AGX_LAB_DIR/guest/cswitch.wprp" . >/dev/null 2>&1
  guest -t 8 "powershell -NoProfile -Command \"Remove-Item $GH_WIN\\$etl.etl -EA 0\"" >/dev/null 2>&1
  (guest "$GH_WIN\\AppleAgxVsyncTrace.exe 42" > "$E/vsync-$name.txt" 2>&1 &)
  sleep 3
  arm_interactive "$GH_WIN\\$script"
  sleep 10
  guest -t 8 "wpr -start $GH_WIN\\cswitch.wprp!AgxWait -filemode" 2>&1 | tr -d '\r' | grep -v '^\*\*' | sed "s/^/[wpr $name] /" | tee -a "$E/probe.txt"
  sleep 15
  guest -t 8 "wpr -stop $GH_WIN\\$etl.etl" 2>&1 | tr -d '\r' | grep -v '^\*\*' | tail -2 | sed "s/^/[wpr $name] /" | tee -a "$E/probe.txt"
  snap_receipts "$E" "$name"; sleep 12
  guest_get "$done" "$E/$done" >/dev/null 2>&1
  utf16_cat "$E/$done" | sed "s/^/[$name] /" | tee -a "$E/probe.txt"
  guest_get "$etl.etl" "$E/$etl.etl" >/dev/null 2>&1; ls -la "$E/$etl.etl" | tee -a "$E/probe.txt"
}
