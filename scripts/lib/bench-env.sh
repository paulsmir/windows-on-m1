# shellcheck shell=sh  (POSIX: sourced from bash and sh scripts)
# Machine-specific bench values (paths, proxy serial ports, guest account) are kept out of the
# repository.  Source this file; it loads KEY=VALUE lines from the first readable of
# $AGX_BENCH_ENV and ~/.config/agx-bench.env, then derives the common paths.  Required keys:
#   AGX_BENCH_ROOT     main checkout on the bench host (holds .local/, proxyenv/, .worktrees/)
#   AGX_PROXY_PORT_A   m1n1 proxy serial device
#   AGX_PROXY_PORT_B   second (vUART) serial device
#   AGX_GUEST_SSH      user@host of the Windows guest
#   AGX_GUEST_HOME     the guest account's home folder, e.g. C:/Users/<name>
#   AGX_BUILDER_KEY    ssh key for the Windows builder
for agx_bench_env_file in "${AGX_BENCH_ENV:-}" "${HOME:-}/.config/agx-bench.env"; do
  if [ -n "$agx_bench_env_file" ] && [ -r "$agx_bench_env_file" ]; then
    set -a
    # shellcheck disable=SC1090
    . "$agx_bench_env_file"
    set +a
    break
  fi
done
unset agx_bench_env_file
for agx_key in AGX_BENCH_ROOT AGX_PROXY_PORT_A AGX_PROXY_PORT_B AGX_GUEST_SSH AGX_GUEST_HOME AGX_BUILDER_KEY; do
  eval "agx_val=\${$agx_key:-}"
  if [ -z "$agx_val" ]; then
    echo "bench-env: $agx_key is not set (see scripts/lib/bench-env.sh)" >&2
    return 2 2>/dev/null || exit 2
  fi
done
unset agx_key agx_val
AGX_TANDEM="${AGX_TANDEM:-$AGX_BENCH_ROOT/.local/tandem}"
AGX_PROXY_PYTHON="${AGX_PROXY_PYTHON:-$AGX_BENCH_ROOT/proxyenv/bin/python}"
AGX_INTEGRATION="${AGX_INTEGRATION:-$AGX_BENCH_ROOT/.worktrees/integration-ad04-windows-compiler}"
export AGX_TANDEM AGX_PROXY_PYTHON AGX_INTEGRATION
