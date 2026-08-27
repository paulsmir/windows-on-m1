#!/bin/sh
# Start the host-assisted Mu/Windows guest with early virtual-UART capture.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PROXY=${M1N1DEVICE:-}
VUART=${M1N1VUART:-}
FIRMWARE=
RAMDISK=
DRY_RUN=0
LOW_MEM=1
DISPLAY=virtual
DEBUG=uart
CHAINLOAD=0
M1N1=
CONTRACT_OUTPUT=
FOREGROUND=0
AGX_POWER_BROKER=0
BOOTSTRAP_TIMEOUT=${ASSISTED_BOOTSTRAP_TIMEOUT:-45}

usage() {
    echo "usage: $0 [--proxy DEVICE] [--vuart DEVICE] [--firmware FILE]" >&2
    echo "          [--display none|physical|virtual|both] [--debug off|uart|full|monitor]" >&2
    echo "          [--ramdisk FILE] [--chainload] [--m1n1 FILE]" >&2
    echo "          [--contract-output FILE]" >&2
    echo "          [--agx-power-broker]" >&2
    echo "          [--no-low-mem] [--foreground] [--dry-run]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --proxy) [ "$#" -ge 2 ] || usage; PROXY=$2; shift 2 ;;
        --vuart) [ "$#" -ge 2 ] || usage; VUART=$2; shift 2 ;;
        --firmware) [ "$#" -ge 2 ] || usage; FIRMWARE=$2; shift 2 ;;
        --display) [ "$#" -ge 2 ] || usage; DISPLAY=$2; shift 2 ;;
        --debug) [ "$#" -ge 2 ] || usage; DEBUG=$2; shift 2 ;;
        --ramdisk) [ "$#" -ge 2 ] || usage; RAMDISK=$2; shift 2 ;;
        --chainload) CHAINLOAD=1; shift ;;
        --m1n1) [ "$#" -ge 2 ] || usage; M1N1=$2; shift 2 ;;
        --contract-output) [ "$#" -ge 2 ] || usage; CONTRACT_OUTPUT=$2; shift 2 ;;
        --no-low-mem) LOW_MEM=0; shift ;;
        --foreground) FOREGROUND=1; shift ;;
        --agx-power-broker) AGX_POWER_BROKER=1; shift ;;
        --dry-run) DRY_RUN=1; shift ;;
        -h|--help) usage ;;
        *) usage ;;
    esac
done

case "$DISPLAY" in none|physical|virtual|both) ;; *) usage ;; esac
case "$DEBUG" in off|uart|full|monitor) ;; *) usage ;; esac
case "$BOOTSTRAP_TIMEOUT" in ''|*[!0-9]*|0) usage ;; esac

MANIFEST_PROFILE=debug
case "$DEBUG" in
    off) PROFILE=release; MANIFEST_PROFILE=release ;;
    uart) PROFILE=debug-uart ;;
    full) PROFILE=debug-forensic ;;
    monitor) PROFILE=debug-monitor ;;
esac
[ -n "$FIRMWARE" ] || FIRMWARE="$ROOT/dist/j313/$PROFILE/J313_EFI.fd"
[ -n "$M1N1" ] || M1N1="$ROOT/dist/j313/$PROFILE/m1n1.macho"

discover_ports() {
    [ -n "$PROXY" ] && { [ "$DEBUG" = off ] || [ -n "$VUART" ]; } && return
    set -- /dev/cu.usbmodem*
    if [ "$1" = '/dev/cu.usbmodem*' ] || [ "$#" -ne 2 ]; then
        echo "Unable to select proxy/vUART automatically." >&2
        echo "Connect the Air, list /dev/cu.usbmodem*, then pass --proxy and --vuart." >&2
        exit 1
    fi
    roles=$("$PYTHON" "$ROOT/tools/proxy_port_roles.py" "$@") || exit 1
    detected_proxy=$(printf '%s\n' "$roles" | sed -n '1p')
    detected_vuart=$(printf '%s\n' "$roles" | sed -n '2p')
    [ -n "$PROXY" ] || PROXY=$detected_proxy
    if [ "$DEBUG" != off ]; then
        [ -n "$VUART" ] || VUART=$detected_vuart
    fi
}

PYTHON="$ROOT/proxyenv/bin/python"
[ -x "$PYTHON" ] || PYTHON=python3

if [ "$DRY_RUN" -eq 0 ]; then
    discover_ports
else
    [ -n "$PROXY" ] || PROXY='<proxy-device>'
    if [ "$DEBUG" != off ]; then
        [ -n "$VUART" ] || VUART='<vuart-device>'
    fi
fi

if [ "$DRY_RUN" -eq 1 ]; then
    echo "mode: assisted development"
    echo "display: $DISPLAY"
    echo "debug: $DEBUG"
    [ "$FOREGROUND" -eq 0 ] && echo "execution: detached" || echo "runner: foreground"
    echo "proxy: $PROXY"
    if [ "$DEBUG" = off ]; then
        echo "virtual UART: disabled"
    else
        echo "ordering: reader-before-guest"
        echo "virtual UART: $VUART"
    fi
    case "$DISPLAY" in virtual|both) echo "USB framebuffer: enabled" ;; *) echo "USB framebuffer: disabled" ;; esac
    case "$DEBUG" in
        full|monitor) echo "telemetry: enabled" ;;
        *) echo "telemetry: disabled" ;;
    esac
    [ "$CHAINLOAD" -eq 0 ] && echo "chainload: disabled" || echo "chainload: $M1N1"
    echo "firmware: $FIRMWARE"
    [ -z "$RAMDISK" ] || echo "RAM disk: $RAMDISK"
    [ -z "$CONTRACT_OUTPUT" ] || echo "launch contract: $CONTRACT_OUTPUT"
    [ "$AGX_POWER_BROKER" -eq 0 ] && echo "AGX G2 power broker: disabled" || echo "AGX G2 power broker: enabled"
    [ "$DEBUG" = off ] || echo "logs: $ROOT/hv.log and $ROOT/guest-uart.log"
    exit 0
fi

[ -f "$FIRMWARE" ] || { echo "Firmware not found: $FIRMWARE" >&2; exit 1; }
[ "$CHAINLOAD" -eq 0 ] || [ -f "$M1N1" ] || {
    echo "m1n1 image not found: $M1N1" >&2
    exit 1
}
[ -z "$RAMDISK" ] || [ -f "$RAMDISK" ] || {
    echo "RAM disk not found: $RAMDISK" >&2
    exit 1
}

MANIFEST=$(dirname "$FIRMWARE")/MANIFEST.json
[ -f "$MANIFEST" ] || { echo "Artifact manifest not found: $MANIFEST" >&2; exit 1; }
set -- "$PYTHON" "$ROOT/tools/artifact_manifest.py" verify "$MANIFEST" \
    --profile "$MANIFEST_PROFILE" --display "$DISPLAY" --debug "$DEBUG"
if [ "$CHAINLOAD" -eq 1 ]; then
    set -- "$@" --require-role m1n1.macho=assisted-chainload
fi
"$@"
if [ "$CHAINLOAD" -eq 1 ] && [ "$(dirname "$M1N1")" != "$(dirname "$FIRMWARE")" ]; then
    echo "m1n1 and Mu must come from the same artifact profile directory" >&2
    exit 1
fi

if pgrep -f '[r]un_uefi.py' >/dev/null; then
    echo "A guest runner already owns the proxy. Use scripts/reset-assisted.sh." >&2
    exit 1
fi

cd "$ROOT"
rm -f assisted-runner.log guest-uart.log guest-uart.tlog guest-uart-reader.log hv.log guest.pid

if [ "$CHAINLOAD" -eq 1 ]; then
    echo "Chainloading matching m1n1: $M1N1"
    M1N1DEVICE="$PROXY" "$PYTHON" \
        "$ROOT/m1n1_windows/proxyclient/tools/chainload.py" "$M1N1"
fi

READER=
if [ "$DEBUG" != off ]; then
    # The reader must hold the virtual UART open before Mu emits its first byte.
    nohup "$PYTHON" -u "$ROOT/extra/uart-reader.py" "$VUART" 2400 \
        </dev/null >guest-uart-reader.log 2>&1 &
    READER=$!
    sleep 2
fi

set -- "$FIRMWARE" --device "$PROXY" --display-mode "$DISPLAY" --debug-mode "$DEBUG"
[ -z "$RAMDISK" ] || set -- "$@" --ramdisk "$RAMDISK"
[ -z "$CONTRACT_OUTPUT" ] || set -- "$@" --contract-output "$CONTRACT_OUTPUT"
[ "$LOW_MEM" -eq 0 ] || set -- "$@" --low-mem

if [ "$FOREGROUND" -eq 1 ]; then
    echo "runner: foreground (Ctrl-C remains a diagnostic snapshot in debug modes)"
    if [ "$DEBUG" = off ]; then
        PYTHONUNBUFFERED=1 M1N1DEVICE="$PROXY" WOM1_AGX_G2_POWER_BROKER="$AGX_POWER_BROKER" \
            exec "$PYTHON" -u "$ROOT/run_uefi.py" "$@"
    else
        # Keep run_uefi.py as the foreground process (and therefore the direct
        # SIGINT/SIGTERM owner), while the persistent log/viewer remains the
        # single source of truth.  A pipeline would make another process the
        # foreground PID and route
        # diagnostic signals wrongly.
        PYTHONUNBUFFERED=1 M1N1DEVICE="$PROXY" WOM1_AGX_G2_POWER_BROKER="$AGX_POWER_BROKER" \
            exec "$PYTHON" -u "$ROOT/run_uefi.py" "$@" >hv.log 2>&1
    fi
elif [ "$DEBUG" = off ]; then
    # Release mode disables guest UART, framebuffer streaming and telemetry,
    # but the host bootstrap must remain observable.  Without this log a
    # post-launch failure is indistinguishable from a running Windows guest.
    PYTHONUNBUFFERED=1 M1N1DEVICE="$PROXY" WOM1_AGX_G2_POWER_BROKER="$AGX_POWER_BROKER" \
        nohup "$PYTHON" -u "$ROOT/run_uefi.py" "$@" </dev/null >assisted-runner.log 2>&1 &
else
    PYTHONUNBUFFERED=1 M1N1DEVICE="$PROXY" WOM1_AGX_G2_POWER_BROKER="$AGX_POWER_BROKER" \
        nohup "$PYTHON" -u "$ROOT/run_uefi.py" "$@" </dev/null >hv.log 2>&1 &
fi
RUNNER=$!
echo "$RUNNER" >guest.pid

if [ "$DEBUG" = off ]; then
    RUNNER_LOG="$ROOT/assisted-runner.log"
else
    RUNNER_LOG="$ROOT/hv.log"
fi

# A live PID only proves that Python has not exited.  Do not announce a guest
# until run_uefi.py has completed CPU, NVMe, stage-2 and framebuffer setup and
# reached the explicit handoff immediately before hv.start().
deadline=$(( $(date +%s) + BOOTSTRAP_TIMEOUT ))
HANDOFF=0
while [ "$(date +%s)" -lt "$deadline" ]; do
    if grep -q "Starting guest..." "$RUNNER_LOG" 2>/dev/null; then
        HANDOFF=1
        break
    fi
    kill -0 "$RUNNER" 2>/dev/null || break
    sleep 1
done

if [ "$HANDOFF" -ne 1 ]; then
    if kill -0 "$RUNNER" 2>/dev/null; then
        echo "runner did not reach guest handoff within ${BOOTSTRAP_TIMEOUT}s; inspect $RUNNER_LOG" >&2
        kill -TERM "$RUNNER" 2>/dev/null || true
    else
        echo "runner exited before initialization (guest handoff); inspect $RUNNER_LOG" >&2
    fi
    if [ "$DEBUG" = off ]; then
        tail -n 40 "$RUNNER_LOG" >&2 || true
    else
        tail -n 80 "$RUNNER_LOG" >&2 || true
    fi
    [ -z "$READER" ] || kill "$READER" 2>/dev/null || true
    exit 1
fi

HARDWARE_GATE_FAILURE=
if grep -Eq "Starting CPU [0-9]+ .*Failed!" "$RUNNER_LOG"; then
    HARDWARE_GATE_FAILURE="secondary CPU startup failed"
elif grep -q "Apple ANS initialization failed" "$RUNNER_LOG"; then
    HARDWARE_GATE_FAILURE="Apple ANS initialization failed"
elif grep -q "backend=0" "$RUNNER_LOG"; then
    HARDWARE_GATE_FAILURE="NVMe backend=0"
elif [ "$AGX_POWER_BROKER" -eq 1 ] && ! grep -q "AGX boot config snapshot v2" "$RUNNER_LOG"; then
    HARDWARE_GATE_FAILURE="AGX scalar snapshot missing"
fi
if [ -n "$HARDWARE_GATE_FAILURE" ]; then
    echo "runner failed a hardware bootstrap gate: $HARDWARE_GATE_FAILURE" >&2
    kill -TERM "$RUNNER" 2>/dev/null || true
    tail -n 80 "$RUNNER_LOG" >&2 || true
    [ -z "$READER" ] || kill "$READER" 2>/dev/null || true
    exit 1
fi

[ -z "$READER" ] && echo "runner=$RUNNER" || echo "reader=$READER runner=$RUNNER"
if [ "$DEBUG" = off ]; then
    echo "host bootstrap log: $ROOT/assisted-runner.log"
else
    echo "hypervisor log: $ROOT/hv.log"
    echo "guest UART log: $ROOT/guest-uart.log"
fi
