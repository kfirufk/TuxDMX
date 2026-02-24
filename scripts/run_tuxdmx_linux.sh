#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

CONFIGURE_PRESET="ninja-debug"
BUILD_PRESET="build-debug"
TEST_PRESET="test-debug"
BIND_ADDRESS="0.0.0.0"
PORT="8080"
DB_PATH="${REPO_ROOT}/data/tuxdmx.sqlite"
WEB_ROOT="${REPO_ROOT}/web"
LOG_FILE="${REPO_ROOT}/data/tuxdmx.log"
RUN_TESTS="0"
NO_OPEN="0"
READY_TIMEOUT_SEC="30"

usage() {
  cat <<'USAGE'
Usage: run_tuxdmx_linux.sh [options]

Options:
  --bind <ip>              Server bind IP (default: 0.0.0.0)
  --port <port>            Server port (default: 8080)
  --configure-preset <p>   CMake configure preset (default: ninja-debug)
  --build-preset <p>       CMake build preset (default: build-debug)
  --test-preset <p>        CTest preset for --run-tests (default: test-debug)
  --run-tests              Run tests before launching server
  --db <path>              SQLite database path (default: ./data/tuxdmx.sqlite)
  --web-root <path>        Web assets root (default: ./web)
  --log-file <path>        Server log file path (default: ./data/tuxdmx.log)
  --ready-timeout <sec>    Readiness timeout in seconds (default: 30)
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
    --test-preset)
      TEST_PRESET="${2:-}"
      shift 2
      ;;
    --run-tests)
      RUN_TESTS="1"
      shift
      ;;
    --db)
      DB_PATH="${2:-}"
      shift 2
      ;;
    --web-root)
      WEB_ROOT="${2:-}"
      shift 2
      ;;
    --log-file)
      LOG_FILE="${2:-}"
      shift 2
      ;;
    --ready-timeout)
      READY_TIMEOUT_SEC="${2:-}"
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

command_exists() {
  command -v "$1" >/dev/null 2>&1
}

version_ge() {
  local a b i
  IFS='.' read -r -a a <<< "${1:-0.0.0}"
  IFS='.' read -r -a b <<< "${2:-0.0.0}"
  for i in 0 1 2; do
    local ai="${a[$i]:-0}"
    local bi="${b[$i]:-0}"
    if ((10#${ai} > 10#${bi})); then
      return 0
    fi
    if ((10#${ai} < 10#${bi})); then
      return 1
    fi
  done
  return 0
}

show_dependency_hints() {
  echo ""
  echo "Install requirements with one of these package manager commands:"
  echo "Debian/Ubuntu:"
  echo "  sudo apt update && sudo apt install -y build-essential cmake ninja-build pkg-config libsqlite3-dev curl"
  echo "Fedora:"
  echo "  sudo dnf install -y gcc-c++ cmake ninja-build pkgconf-pkg-config sqlite-devel curl"
  echo "Arch:"
  echo "  sudo pacman -S --needed base-devel cmake ninja pkgconf sqlite curl"
}

show_configure_hints() {
  local log_file="$1"
  echo ""
  echo "Common fixes for CMake configure failures:"

  if grep -Eqi "Could NOT find SQLite3|Could not find SQLite3|SQLite3_FOUND" "${log_file}"; then
    echo "- SQLite3 development package is missing."
    echo "  Debian/Ubuntu: sudo apt install -y libsqlite3-dev"
    echo "  Fedora:        sudo dnf install -y sqlite-devel"
    echo "  Arch:          sudo pacman -S --needed sqlite"
  fi

  if grep -Eqi "CMAKE_CXX_COMPILER|No CMAKE_CXX_COMPILER|No CMAKE_C_COMPILER|is not a full path to an existing compiler" "${log_file}"; then
    echo "- C/C++ compiler not found."
    echo "  Debian/Ubuntu: sudo apt install -y build-essential"
    echo "  Fedora:        sudo dnf install -y gcc-c++"
    echo "  Arch:          sudo pacman -S --needed base-devel"
  fi

  if grep -Eqi "CMAKE_MAKE_PROGRAM|Ninja" "${log_file}"; then
    echo "- Ninja not found but a ninja preset is selected."
    echo "  Debian/Ubuntu: sudo apt install -y ninja-build"
    echo "  Fedora:        sudo dnf install -y ninja-build"
    echo "  Arch:          sudo pacman -S --needed ninja"
  fi
}

get_lan_ip() {
  local lan_ip=""
  if command_exists hostname; then
    lan_ip="$(hostname -I 2>/dev/null | awk '{print $1}')"
    if [[ -n "${lan_ip}" ]]; then
      echo "${lan_ip}"
      return 0
    fi
  fi

  if command_exists ip; then
    lan_ip="$(ip -4 route get 1.1.1.1 2>/dev/null | awk '{for (i=1; i<=NF; i++) if ($i=="src") {print $(i+1); exit}}')"
    if [[ -n "${lan_ip}" ]]; then
      echo "${lan_ip}"
      return 0
    fi
  fi

  return 1
}

if ! [[ "${PORT}" =~ ^[0-9]+$ ]] || [[ "${PORT}" -lt 1 ]] || [[ "${PORT}" -gt 65535 ]]; then
  echo "Invalid --port value: ${PORT}" >&2
  exit 1
fi

if ! [[ "${READY_TIMEOUT_SEC}" =~ ^[0-9]+$ ]] || [[ "${READY_TIMEOUT_SEC}" -lt 5 ]]; then
  echo "Invalid --ready-timeout value: ${READY_TIMEOUT_SEC} (must be >= 5)" >&2
  exit 1
fi

declare -a missing_tools=()

if ! command_exists cmake; then
  missing_tools+=("cmake 3.28+")
else
  cmake_version_line="$(cmake --version | head -n 1 || true)"
  cmake_version="0.0.0"
  if [[ "${cmake_version_line}" =~ ([0-9]+\.[0-9]+\.[0-9]+) ]]; then
    cmake_version="${BASH_REMATCH[1]}"
  fi
  if ! version_ge "${cmake_version}" "3.28.0"; then
    missing_tools+=("cmake 3.28+ (found ${cmake_version})")
  fi
fi

if [[ "${CONFIGURE_PRESET}" == *"ninja"* ]] && ! command_exists ninja; then
  missing_tools+=("ninja")
fi

if ! command_exists c++ && ! command_exists g++ && ! command_exists clang++; then
  missing_tools+=("C++ compiler (c++, g++, or clang++)")
fi

if ! command_exists curl; then
  missing_tools+=("curl")
fi

if [[ "${#missing_tools[@]}" -gt 0 ]]; then
  echo ""
  echo "Missing required tools:"
  for tool in "${missing_tools[@]}"; do
    echo "  - ${tool}"
  done
  show_dependency_hints
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
    BINARY_PATH="${REPO_ROOT}/build/debug/tuxdmx"
    ;;
esac

mkdir -p "$(dirname "${DB_PATH}")"
mkdir -p "$(dirname "${LOG_FILE}")"

cd "${REPO_ROOT}"

CONFIGURE_LOG="$(mktemp -t tuxdmx-configure.XXXXXX.log)"
SERVER_PID=""

cleanup() {
  if [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" 2>/dev/null; then
    kill "${SERVER_PID}" 2>/dev/null || true
    wait "${SERVER_PID}" 2>/dev/null || true
  fi
  rm -f "${CONFIGURE_LOG}" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

echo "==> Configuring (${CONFIGURE_PRESET})"
if ! cmake --preset "${CONFIGURE_PRESET}" 2>&1 | tee "${CONFIGURE_LOG}"; then
  show_configure_hints "${CONFIGURE_LOG}"
  exit 1
fi

echo "==> Building (${BUILD_PRESET})"
cmake --build --preset "${BUILD_PRESET}"

if [[ "${RUN_TESTS}" == "1" ]]; then
  echo "==> Running tests (${TEST_PRESET})"
  ctest --preset "${TEST_PRESET}"
fi

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

echo "==> Starting server"
"${BINARY_PATH}" \
  --bind "${BIND_ADDRESS}" \
  --port "${PORT}" \
  --db "${DB_PATH}" \
  --web-root "${WEB_ROOT}" \
  --log-file "${LOG_FILE}" &
SERVER_PID=$!

LOCAL_URL="http://127.0.0.1:${PORT}"
READY_URL="${LOCAL_URL}/api/state"

echo "==> Waiting for server readiness (${READY_URL})"
READY="0"
ATTEMPTS=$((READY_TIMEOUT_SEC * 4))
for ((i = 1; i <= ATTEMPTS; i += 1)); do
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

echo "==> TuxDMX is running"
echo "Local: ${LOCAL_URL}"
if LAN_IP="$(get_lan_ip)"; then
  echo "LAN:   http://${LAN_IP}:${PORT}"
fi
echo "Log:   ${LOG_FILE}"
echo "Press Ctrl+C to stop."

if [[ "${NO_OPEN}" != "1" ]]; then
  if command_exists xdg-open; then
    xdg-open "${LOCAL_URL}" >/dev/null 2>&1 || true
  elif command_exists gio; then
    gio open "${LOCAL_URL}" >/dev/null 2>&1 || true
  else
    echo "Browser auto-open skipped (xdg-open/gio not available)."
  fi
fi

wait "${SERVER_PID}"
