# Linux Setup and Launch

## Requirements

- CMake 3.28+
- C++23 compiler (`c++`, `g++`, or `clang++`)
- Ninja (for `ninja-*` presets)
- SQLite3 development package
- `curl` (used by launcher readiness checks)

Optional:
- PortAudio development package (`portaudio-2.0`) for real microphone input
- RtMidi development package (`rtmidi`) for server-side MIDI backend

## Quick Start

```bash
./scripts/run_tuxdmx_linux.sh
```

The script:
- checks required tools
- prints install hints when requirements are missing
- configures/builds with CMake presets
- starts server and waits for `/api/state`
- opens browser (unless disabled)

## Launcher Options

```text
--bind <ip>              default: 0.0.0.0
--port <port>            default: 8080
--configure-preset <p>   default: ninja-debug
--build-preset <p>       default: build-debug
--test-preset <p>        default: test-debug
--run-tests              run tests before launch
--db <path>              default: ./data/tuxdmx.sqlite
--web-root <path>        default: ./web
--log-file <path>        default: ./data/tuxdmx.log
--ready-timeout <sec>    default: 30
--no-open                do not auto-open browser
```

Example:

```bash
./scripts/run_tuxdmx_linux.sh --run-tests --port 8090 --bind 0.0.0.0 --no-open
```

## Package Hints

Debian/Ubuntu:

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build pkg-config libsqlite3-dev curl
```

Fedora:

```bash
sudo dnf install -y gcc-c++ cmake ninja-build pkgconf-pkg-config sqlite-devel curl
```

Arch:

```bash
sudo pacman -S --needed base-devel cmake ninja pkgconf sqlite curl
```
