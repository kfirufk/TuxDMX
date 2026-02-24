#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

CONFIGURE_PRESET="ninja-debug"
BUILD_PRESET="build-debug"
BIND_ADDRESS="0.0.0.0"
PORT="18181"
DB_PATH="${REPO_ROOT}/data/tuxdmx.sqlite"
WEB_ROOT="${REPO_ROOT}/web"
NO_OPEN="0"

usage() {
  cat <<'USAGE'
Usage: run_tuxdmx_mac.sh [options]

Options:
  --bind <ip>              Server bind IP (default: 0.0.0.0)
  --port <port>            Server port (default: 18181)
  --configure-preset <p>   CMake configure preset (default: ninja-debug)
  --build-preset <p>       CMake build preset (default: build-debug)
  --db <path>              SQLite database path (default: ./data/tuxdmx.sqlite)
  --web-root <path>        Web assets root (default: ./web)
  --no-open                Do not open browser automatically
  --help                   Show this help
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --bind)
      BIND_ADDRESS="${2:-}"
      shift 2
      ;;
    --port)
      PORT="${2:-}"
      shift 2
      ;;
    --configure-preset)
      CONFIGURE_PRESET="${2:-}"
      shift 2
      ;;
    --build-preset)
      BUILD_PRESET="${2:-}"
      shift 2
      ;;
    --db)
      DB_PATH="${2:-}"
      shift 2
      ;;
    --web-root)
      WEB_ROOT="${2:-}"
      shift 2
      ;;
    --no-open)
      NO_OPEN="1"
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if ! [[ "${PORT}" =~ ^[0-9]+$ ]] || [[ "${PORT}" -lt 1 ]] || [[ "${PORT}" -gt 65535 ]]; then
  echo "Invalid --port value: ${PORT}" >&2
  exit 1
fi

case "${CONFIGURE_PRESET}" in
  ninja-debug)
    BINARY_PATH="${REPO_ROOT}/build/debug/tuxdmx"
    ;;
  ninja-release)
    BINARY_PATH="${REPO_ROOT}/build/release/tuxdmx"
    ;;
  *)
    # Fallback for custom presets: try debug path first, then release.
    BINARY_PATH="${REPO_ROOT}/build/debug/tuxdmx"
    ;;
esac

mkdir -p "$(dirname "${DB_PATH}")"

cd "${REPO_ROOT}"

echo "==> Configuring (${CONFIGURE_PRESET})"
cmake --preset "${CONFIGURE_PRESET}"

echo "==> Building (${BUILD_PRESET})"
cmake --build --preset "${BUILD_PRESET}"

if [[ ! -x "${BINARY_PATH}" ]]; then
  if [[ -x "${REPO_ROOT}/build/release/tuxdmx" ]]; then
    BINARY_PATH="${REPO_ROOT}/build/release/tuxdmx"
  elif [[ -x "${REPO_ROOT}/build/debug/tuxdmx" ]]; then
    BINARY_PATH="${REPO_ROOT}/build/debug/tuxdmx"
  else
    echo "tuxdmx binary not found after build." >&2
    exit 1
  fi
fi

SERVER_PID=""
cleanup() {
  if [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" 2>/dev/null; then
    kill "${SERVER_PID}" 2>/dev/null || true
    wait "${SERVER_PID}" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM

echo "==> Starting server"
"${BINARY_PATH}" \
  --bind "${BIND_ADDRESS}" \
  --port "${PORT}" \
  --db "${DB_PATH}" \
  --web-root "${WEB_ROOT}" &
SERVER_PID=$!

LOCAL_URL="http://127.0.0.1:${PORT}"
READY_URL="${LOCAL_URL}/api/state"

echo "==> Waiting for server readiness (${READY_URL})"
READY="0"
for _ in {1..120}; do
  if ! kill -0 "${SERVER_PID}" 2>/dev/null; then
    echo "Server exited before becoming ready." >&2
    wait "${SERVER_PID}" || true
    exit 1
  fi
  if curl -fsS "${READY_URL}" >/dev/null 2>&1; then
    READY="1"
    break
  fi
  sleep 0.25
done

if [[ "${READY}" != "1" ]]; then
  echo "Server did not become ready in time." >&2
  exit 1
fi

LAN_IP="$(ipconfig getifaddr en0 2>/dev/null || true)"
if [[ -z "${LAN_IP}" ]]; then
  LAN_IP="$(ipconfig getifaddr en1 2>/dev/null || true)"
fi

echo "==> TuxDMX is running"
echo "Local: ${LOCAL_URL}"
if [[ -n "${LAN_IP}" ]]; then
  echo "LAN:   http://${LAN_IP}:${PORT}"
fi
echo "Press Ctrl+C to stop."

if [[ "${NO_OPEN}" != "1" ]]; then
  open "${LOCAL_URL}" >/dev/null 2>&1 || true
fi

wait "${SERVER_PID}"
