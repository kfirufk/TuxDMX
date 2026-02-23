# TuxDMX

Cross-platform C++23 DMX control server for **ENTTEC DMX USB Pro** with a responsive web UI for desktop/tablet/phone.

## Implemented Features

- C++23 backend with CMake + presets.
- SQLite persistence for:
  - fixture templates
  - channel definitions and value ranges
  - fixture patching (universe/start/channel count)
  - live channel values
  - fixture groups
- Startup seed templates:
  - **AliExpress 60x3W RGB PAR**
  - **Mira Dye** (A-mode 13ch layout)
  - automatic one-time migration: if an older `Mira Dye` layout is detected, it is preserved as **Mira Dye (D Mode Legacy)** and `Mira Dye` is updated to A-mode
- ENTTEC DMX USB Pro discovery and status reporting:
  - serial-port scan
  - serial probe (`0x7E 0x0A 0x00 0x00 0xE7`)
  - firmware and serial surfaced in UI
- Multi-universe engine model:
  - stores/updates multiple universes
  - routes selected output universe to DMX USB Pro
  - explicit create-universe API/UI for quick setup
- Live control UI:
  - fixture cards with per-channel sliders (default)
  - optional knob mode toggle in toolbar
  - MIDI learn/clear per live control node (fixture channels and group controls)
  - performance hold buttons with smooth attack/release:
    - all-on lift
    - blackout
    - rotate for pan/tilt fixtures
    - strobe hit
  - one-click panic blackout button (forces zero DMX and disables music-reactive mode)
  - per-effect intensity controls (all-on / blackout / rotate / strobe)
  - adjustable fade seconds (decimal support) and intensity controls are MIDI-mappable
  - value-range chips
  - icon mapping for channel types and mode/range labels
  - fast blank-channel generator for unknown fixtures (manual missing workflow)
  - clean view menu (Live / Patch / Templates / Groups / Learn)
  - auto-refresh-safe form selections (pending choices are preserved until applied)
  - auto-refresh pauses while editing form fields (resumes automatically after short idle)
  - safe shutdown blackout: Ctrl+C writes zero values to fixtures and DMX output to prevent restart motion
- Audio input routing UI:
  - lists available input devices
  - shows default/selected/active input
  - allows switching microphone source live
  - MIDI mapping for reactive mode start/stop
- Group UI + API:
  - create/delete groups
  - assign fixtures to groups
  - control channel kinds for full group (dimmer/rgb/strobe/speed/etc.)
  - apply group mode labels (Jump/Gradient/Pulse/Voice, etc.)
- Template portability:
  - export templates as JSON
  - import templates from JSON file
- Audio reactive engine:
  - real microphone analysis via PortAudio when available
  - adaptive energy + bass/treble + beat + BPM estimation
  - fallback simulated analyzer when PortAudio is unavailable
  - reactive playback maps to channel kinds and labeled ranges with documented formulas in code
  - quiet-room movement guard for pan/tilt fixtures to prevent random drift in near-silence

## Build

### Prerequisites

- CMake 3.28+
- C++23 compiler
- SQLite3 dev package
- Optional: PortAudio (`portaudio-2.0`) for real microphone input

### Linux/macOS (Ninja)

```bash
cmake --preset ninja-debug
cmake --build --preset build-debug
./build/debug/tuxdmx --bind 0.0.0.0 --port 8080
```

### macOS One-Command Run Script

```bash
./scripts/run_tuxdmx_mac.sh
```

The script will:
- configure + build with CMake presets
- start the TuxDMX server
- wait for `/api/state` to respond
- open `http://127.0.0.1:8080` in your browser
- print a LAN URL for phone/tablet access

Optional flags:

```bash
./scripts/run_tuxdmx_mac.sh --port 8090 --bind 0.0.0.0 --no-open
```

### Windows (MSVC)

```powershell
cmake -S . -B build\debug -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
cmake --build build\debug
.\build\debug\tuxdmx.exe --bind 0.0.0.0 --port 8080
```

Open `http://<server-ip>:8080` from phone/desktop browser.

## Tests

```bash
cmake --preset ninja-debug
cmake --build --preset build-debug
ctest --preset test-debug
```

## CLI options

```text
--bind <ip>          default: 0.0.0.0
--port <port>        default: 8080
--db <path>          default: data/tuxdmx.sqlite
--web-root <path>    default: ./web
```

## Key API Endpoints

- `GET /api/state` full UI state payload
- `POST /api/templates` create template
- `POST /api/templates/{id}/channels`
- `POST /api/channels/{id}/ranges`
- `POST /api/fixtures` patch fixture
- `POST /api/fixtures/{id}/channels/{channel}` set channel value
- `POST /api/dmx/output-universe` set active output universe
- `POST /api/dmx/universes` create/ensure a universe exists
- `POST /api/dmx/patches` apply temporary direct DMX patches (`universe:address:value,...`)
- `POST /api/dmx/blackout` panic blackout (set known universes and fixture channels to zero, reactive off)
- `POST /api/audio/reactive` toggle music-reactive mode
- `POST /api/audio/input-device` select active audio input device (`-1` = default)
- `POST /api/groups` create group
- `POST /api/groups/{id}/fixtures` set group members
- `POST /api/groups/{id}/kinds/{kind}` apply value by channel kind
- `POST /api/groups/{id}/mode` apply mode by label
- `GET /api/templates/export` export templates JSON

## Notes

- ENTTEC DMX USB Pro is a single physical DMX output. This app supports multiple universes in software and lets you select which universe is routed to the hardware output.
- MIDI mapping in the web UI uses the browser Web MIDI API (best support in Chrome/Edge).
- Reactive formulas are intentionally commented in `/Volumes/extreme-ssd/projects/dmx512/tuxdmx/src/app/app_controller.cpp` and `/Volumes/extreme-ssd/projects/dmx512/tuxdmx/src/audio/audio_engine.cpp` so you can tune behavior quickly.
