#!/bin/sh
# Bounded unattended assisted-boot supervisor for J313 development.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SSH_HOST=
SSH_USER=pavel
SSH_KEY=${SSH_KEY:-$HOME/.ssh/air}
MAX_GENERATIONS=3
BOOT_GRACE=240
PROBE_INTERVAL=10
FAILURE_LIMIT=3
DIAGNOSTICS=forensic
RECOVERY_POLICY=capture
DRY_RUN=0

usage() {
    echo "usage: $0 --ssh-host HOST [--ssh-user USER] [--ssh-key FILE]" >&2
    echo "          [--max-generations N] [--boot-grace SEC]" >&2
    echo "          [--diagnostics forensic|monitor] [--dry-run]" >&2
    echo "          [--recovery-policy capture|disable-auto]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --ssh-host) [ "$#" -ge 2 ] || usage; SSH_HOST=$2; shift 2 ;;
        --ssh-user) [ "$#" -ge 2 ] || usage; SSH_USER=$2; shift 2 ;;
        --ssh-key) [ "$#" -ge 2 ] || usage; SSH_KEY=$2; shift 2 ;;
        --max-generations) [ "$#" -ge 2 ] || usage; MAX_GENERATIONS=$2; shift 2 ;;
        --boot-grace) [ "$#" -ge 2 ] || usage; BOOT_GRACE=$2; shift 2 ;;
        --diagnostics) [ "$#" -ge 2 ] || usage; DIAGNOSTICS=$2; shift 2 ;;
        --recovery-policy) [ "$#" -ge 2 ] || usage; RECOVERY_POLICY=$2; shift 2 ;;
        --dry-run) DRY_RUN=1; shift ;;
        -h|--help) usage ;;
        *) usage ;;
    esac
done

[ -n "$SSH_HOST" ] || usage
case "$MAX_GENERATIONS:$BOOT_GRACE" in
    *[!0-9:]*|0:*|*:0) usage ;;
esac
case "$DIAGNOSTICS" in
    forensic) DEBUG_MODE=full; ARTIFACT_PROFILE=debug-forensic ;;
    monitor) DEBUG_MODE=monitor; ARTIFACT_PROFILE=debug-monitor ;;
    *) usage ;;
esac
case "$RECOVERY_POLICY" in capture|disable-auto) ;; *) usage ;; esac

PROFILE="$ROOT/dist/j313/$ARTIFACT_PROFILE"
M1N1="$PROFILE/m1n1.macho"
FIRMWARE="$PROFILE/J313_EFI.fd"
CAPTURE_BASE="$ROOT/.local/platform-stability/supervisor"
RUN_ID=$(date -u +%Y%m%dT%H%M%SZ)-$$
CAPTURE_ROOT="$CAPTURE_BASE/$RUN_ID"
LOCK_DIR="$ROOT/.local/platform-stability/supervisor.lock"

if [ "$DRY_RUN" -eq 1 ]; then
    echo "artifacts: dist/j313/$ARTIFACT_PROFILE/m1n1.macho dist/j313/$ARTIFACT_PROFILE/J313_EFI.fd"
    echo "max generations: $MAX_GENERATIONS"
    echo "boot grace: $BOOT_GRACE seconds"
    echo "diagnostics: $DIAGNOSTICS (artifact debug=$DEBUG_MODE)"
    echo "health: SSH ${SSH_USER}@${SSH_HOST}, $FAILURE_LIMIT consecutive failures"
    echo "snapshot signal: SIGINT (guest continues)"
    echo "reboot signal: SIGTERM (explicit hardware reboot)"
    if [ "$RECOVERY_POLICY" = disable-auto ]; then
        echo "recovery policy: disable-auto after SSH-ready"
    else
        echo "recovery policy: captured read-only after SSH-ready"
    fi
    echo "recovery: wait for a fresh proxy generation; no infinite boot loop"
    exit 0
fi

mkdir -p "$(dirname "$LOCK_DIR")"
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
    lock_pid=$(cat "$LOCK_DIR/pid" 2>/dev/null || true)
    if [ -n "$lock_pid" ] && kill -0 "$lock_pid" 2>/dev/null; then
        echo "another assisted supervisor already owns the USB boot path" >&2
        exit 1
    fi
    rm -f "$LOCK_DIR/pid"
    rmdir "$LOCK_DIR" 2>/dev/null || {
        echo "unable to recover stale assisted supervisor lock" >&2
        exit 1
    }
    mkdir "$LOCK_DIR" || exit 1
fi
printf '%s\n' "$$" >"$LOCK_DIR/pid"
cleanup_lock() {
    rm -f "$LOCK_DIR/pid"
    rmdir "$LOCK_DIR" 2>/dev/null || true
}
terminate_supervisor() {
    exit 130
}
trap cleanup_lock EXIT
trap terminate_supervisor HUP INT TERM

PYTHON="$ROOT/proxyenv/bin/python"
[ -x "$PYTHON" ] || { echo "proxyenv is required" >&2; exit 1; }
[ -f "$M1N1" ] && [ -f "$FIRMWARE" ] || {
    echo "missing debug artifacts; run scripts/build-standalone.sh --debug-build --display physical --debug full" >&2
    exit 1
}
"$PYTHON" "$ROOT/tools/artifact_manifest.py" verify "$PROFILE/MANIFEST.json" \
    --profile debug --display physical --debug "$DEBUG_MODE" \
    --require-role m1n1.macho=assisted-chainload
mkdir -p "$CAPTURE_ROOT"

wait_roles() {
    while :; do
        set -- /dev/cu.usbmodem*
        if [ "$1" != '/dev/cu.usbmodem*' ] && [ "$#" -eq 2 ]; then
            roles=$($PYTHON "$ROOT/tools/proxy_port_roles.py" "$@" 2>/dev/null || true)
            # Never interpret early TTY text as a path (in particular a line
            # ending in ':' becomes an invalid serial baud suffix).
            devices=$(printf '%s\n' "$roles" | sed -n '\#^/dev/cu\.usbmodem#p')
            proxy=$(printf '%s\n' "$devices" | sed -n '1p')
            vuart=$(printf '%s\n' "$devices" | sed -n '2p')
            [ -n "$proxy" ] && [ -n "$vuart" ] && return 0
        fi
        sleep 2
    done
}

healthy_ssh() {
    ssh -i "$SSH_KEY" -o BatchMode=yes -o ConnectTimeout=4 \
        -o StrictHostKeyChecking=accept-new "${SSH_USER}@${SSH_HOST}" \
        'cmd /c echo CODEX_J313_ALIVE' 2>/dev/null | grep -q CODEX_J313_ALIVE
}

capture_recovery_policy() {
    output=$1
    ssh -i "$SSH_KEY" -o BatchMode=yes -o ConnectTimeout=5 \
        -o StrictHostKeyChecking=accept-new "${SSH_USER}@${SSH_HOST}" \
        'cmd /c "bcdedit /enum {current} & reagentc /info"' >"$output" 2>&1
}

disable_automatic_recovery() {
    run_dir=$1
    backup='C:\\Windows\\Temp\\windows-on-m1-bcd-before-disable-auto.bak'

    capture_recovery_policy "$run_dir/recovery-policy-before.log" || return 1
    ssh -i "$SSH_KEY" -o BatchMode=yes -o ConnectTimeout=5 \
        -o StrictHostKeyChecking=accept-new "${SSH_USER}@${SSH_HOST}" \
        "cmd /c \"bcdedit /export $backup && bcdedit /set {current} recoveryenabled No && bcdedit /set {current} bootstatuspolicy IgnoreAllFailures\"" \
        >"$run_dir/recovery-policy-disable.log" 2>&1 || return 1
    capture_recovery_policy "$run_dir/recovery-policy-after.log"
}

generation=1
while [ "$generation" -le "$MAX_GENERATIONS" ]; do
    echo "generation $generation/$MAX_GENERATIONS: waiting for responsive proxy"
    wait_roles
    run_dir=$(printf '%s/generation-%03d' "$CAPTURE_ROOT" "$generation")
    mkdir -p "$run_dir"
    cp "$PROFILE/MANIFEST.json" "$run_dir/MANIFEST.json"

    "$ROOT/scripts/run-assisted.sh" --proxy "$proxy" --vuart "$vuart" \
        --firmware "$FIRMWARE" --m1n1 "$M1N1" --chainload \
        --display physical --debug "$DEBUG_MODE" \
        --contract-output "$run_dir/launch-contract.bin"
    runner=$(cat "$ROOT/guest.pid")
    echo "$runner" >"$run_dir/runner.pid"

    boot_deadline=$(( $(date +%s) + BOOT_GRACE ))
    while [ "$(date +%s)" -lt "$boot_deadline" ]; do
        if healthy_ssh; then
            echo "$(date -u +%FT%TZ) ssh-ready" >>"$run_dir/health.log"
            if [ "$RECOVERY_POLICY" = disable-auto ]; then
                disable_automatic_recovery "$run_dir" ||
                    echo "automatic recovery policy update failed" \
                        >>"$run_dir/recovery-policy-disable.log"
            else
                capture_recovery_policy "$run_dir/recovery-policy.log" || \
                    echo "recovery policy capture failed" >>"$run_dir/recovery-policy.log"
            fi
            break
        fi
        kill -0 "$runner" 2>/dev/null || break
        sleep "$PROBE_INTERVAL"
    done

    failures=0
    outage_captured=0
    while kill -0 "$runner" 2>/dev/null; do
        if healthy_ssh; then
            failures=0
            outage_captured=0
            echo "$(date -u +%FT%TZ) ssh-ok" >>"$run_dir/health.log"
        else
            failures=$((failures + 1))
            echo "$(date -u +%FT%TZ) ssh-fail count=$failures" >>"$run_dir/health.log"
            if [ "$failures" -ge "$FAILURE_LIMIT" ]; then
                # SSH is not a guest heartbeat: the login screen, an
                # unconfigured network adapter, or xHCI restart can make it
                # unavailable while all vCPUs continue to execute. Preserve
                # evidence but never reset from this signal alone.
                echo "$(date -u +%FT%TZ) ssh unavailable; guest reset suppressed" \
                    >>"$run_dir/health.log"
                cp "$ROOT/hv.log" "$run_dir/hv-ssh-unavailable.log" 2>/dev/null || true
                if [ "$outage_captured" -eq 0 ]; then
                    kill -INT "$runner"
                    sleep 3
                    kill -INT "$runner"
                    sleep 3
                    cp "$ROOT/hv.log" "$run_dir/hv-snapshots.log" 2>/dev/null || true
                    "$PYTHON" "$ROOT/tools/platform_stability.py" \
                        --log "$run_dir/hv-snapshots.log" \
                        --output "$run_dir/stability-classification.json" \
                        >>"$run_dir/health.log" 2>&1 || true
                    outage_captured=1
                fi
                failures=0
            fi
        fi
        sleep "$PROBE_INTERVAL"
    done

    # Do not overlap generations. The hardware reboot path must tear down the
    # old owner before the next proxy probe can succeed.
    while kill -0 "$runner" 2>/dev/null; do sleep 2; done
    cp "$ROOT/hv.log" "$run_dir/hv.log" 2>/dev/null || true
    cp "$ROOT/guest-uart.log" "$run_dir/guest-uart.log" 2>/dev/null || true
    generation=$((generation + 1))
done

echo "bounded supervisor stopped after $MAX_GENERATIONS generations"
exit 1
