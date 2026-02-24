# macOS Setup and Launch

## Requirements

- CMake 3.28+
- C++23-capable compiler (Apple Clang from Xcode Command Line Tools)
- Ninja
- SQLite3 development package

Optional:
- PortAudio (`portaudio-2.0`) for real microphone input
- RtMidi (`rtmidi`) for server-side MIDI backend
- If RtMidi is not available, CoreMIDI fallback is used automatically

## Quick Start

```bash
./scripts/run_tuxdmx_mac.sh
```

The script:
- configures/builds with CMake presets
- starts server and waits for `/api/state`
- opens browser (unless disabled)

## Launcher Options

```text
--bind <ip>              default: 0.0.0.0
--port <port>            default: 8080
--configure-preset <p>   default: ninja-debug
--build-preset <p>       default: build-debug
--db <path>              default: ./data/tuxdmx.sqlite
--web-root <path>        default: ./web
--no-open                do not auto-open browser
```

Example:

```bash
./scripts/run_tuxdmx_mac.sh --port 8090 --bind 0.0.0.0 --no-open
```

## Package Hint (Homebrew)

```bash
brew install cmake ninja sqlite
```
