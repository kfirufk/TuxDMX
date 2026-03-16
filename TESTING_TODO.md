# TuxDMX Testing Todo

This file tracks the highest-value manual tests for current development.

## Priority Right Now

- DMX transport stability on macOS, especially with USB hubs, alternate cables, and repeated connect/disconnect cycles.
- Audio-reactive behavior for mixed rigs:
  - static fixtures like RGB PAR cans
  - moving fixtures with pan/tilt and effect channels
- Music-reactive quiet-room behavior:
  - no random drift
  - no stuck movement
  - no lights staying on after sound stops in blackout-style profiles
- Multi-fixture sessions with repeated hold buttons and scene recalls while DMX is active.

## Test Matrix

### 1. DMX Transport Stability

- Start server with DMX frame debug logging off.
- Connect the DMX interface directly to the Mac.
- Run a 10-15 minute idle session with fixtures patched and music reactive off.
- Verify:
  - no disconnect/reconnect loop
  - no unexplained fixture movement
  - no repeated scan spam in `data/tuxdmx.log`

- Repeat the same test through the usual USB hub.
- Repeat with the alternate USB cable that previously changed behavior.
- During each run:
  - press `Hold: All On` repeatedly
  - press `Blackout`
  - recall a few saved scenes
  - stop the server with `Ctrl+C`
- Verify:
  - fixtures respond correctly
  - shutdown blackout still clears output
  - the app surfaces any failure with a useful endpoint/error hint

### 2. Device Recovery / Reconnect

- Start with the DMX interface connected and confirmed in the UI.
- While the app is running:
  - unplug and replug USB
  - unplug and replug DMX cable
  - power-cycle the fixture chain if practical
- Verify:
  - the UI keeps showing the last known endpoint/identity
  - reconnect happens cleanly when the device returns
  - logs clearly distinguish missing endpoint vs transient write issue

### 3. Audio-Reactive Basics

- Test with a real microphone input selected.
- Test with near-silence.
- Test with speech, steady music, bass-heavy music, and sharp beat-driven music.
- Verify:
  - threshold meter reflects room noise realistically
  - fixtures below threshold stay quiet in blackout-style modes
  - lights return to quiet/off state after music stops
  - movement fixtures do not drift in silence

### 4. Mixed Fixture Behavior

- Patch at least:
  - one RGB PAR template
  - one moving-head or pan/tilt capable template
  - one fixture with mode/effect channels
- Test each reactive profile against that mixed rig.
- Verify:
  - PAR fixtures emphasize color/dimmer/strobe
  - moving fixtures only move when the mode intends movement
  - unsupported channels stay stable instead of getting random junk values

### 5. Scene / Live Override Interaction

- Enable music reactive mode.
- Move live sliders while reactive mode is active.
- Recall saved scenes while music reactive is active.
- Use hold buttons while music reactive is active.
- Verify:
  - overrides feel intentional
  - transitions are smooth
  - no fixture remains stuck in a macro/effect state afterward

### 6. Cross-Platform Smoke Tests

- Windows:
  - run via `scripts/run_tuxdmx_windows.cmd`
  - confirm DMX detection, audio device listing, MIDI backend status
- Linux:
  - run via the Linux launcher/build flow
  - confirm serial detection and audio input selection
- Verify:
  - backend availability is reported clearly
  - no platform falls back silently without telling you why

## Logging To Capture During Failures

- `data/tuxdmx.log`
- UI debug log panel
- DMX status card values:
  - transport state
  - endpoint
  - last error kind
  - last error hint
  - retry counts

If a failure is reproducible, note:

- OS and version
- direct USB vs hub
- cable used
- DMX interface model
- fixture chain connected
- whether music reactive mode was on
- selected reactive profile
- threshold value

## Current Goal

The next development focus is making audio-reactive playback feel reliable, intentional, and party-ready for mixed fixture types without causing motion drift, stuck output, or random behavior in silence.
