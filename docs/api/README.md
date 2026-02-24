# TuxDMX API Guide

This document is for developers who want to build their own client UI for TuxDMX.

## Architecture

TuxDMX is server-first:

- the server owns DMX output, audio analysis, MIDI input routing, and persistence
- the built-in web UI is only one client of that server API
- other clients can be added later (mobile app, desktop app, custom control surface UI, etc.)

## Base URL

- Local: `http://127.0.0.1:8080`
- LAN: `http://<server-ip>:8080`

## Request and Response Format

- Transport: HTTP
- Response content type: `application/json; charset=utf-8`
- Most `POST` bodies are form-encoded (`application/x-www-form-urlencoded`)

Standard response envelope:

- Success: `{"ok":true, ...}`
- Error: `{"ok":false,"error":"..."}`

## Auth and Network Model

- Current API has **no authentication**.
- Treat it as a trusted-LAN control surface.
- If exposed beyond local/trusted network, place it behind your own auth/TLS reverse proxy.

## Polling Strategy

- The built-in UI polls `GET /api/state` every ~2.5 seconds.
- Custom clients can do the same, then send mutation requests for user actions.

## Endpoint Summary

### State and Diagnostics

| Method | Path | Purpose |
|---|---|---|
| GET | `/api/status` | Lightweight status payload |
| GET | `/api/state` | Full app state (dmx/audio/templates/fixtures/groups/scenes/midi/logs) |
| GET | `/api/logs` | Recent in-memory debug logs |
| POST | `/api/logs/clear` | Clear debug log buffer |

### Templates

| Method | Path | Purpose | Body fields |
|---|---|---|---|
| GET | `/api/templates` | List templates with channels/ranges | - |
| GET | `/api/templates/export` | Export templates JSON | - |
| POST | `/api/templates` | Create template | `name`, `description` |
| POST | `/api/templates/{id}/replace` | Replace template metadata/definition | `name`, `description` |
| POST | `/api/templates/{id}/channels` | Add channel to template | `channel_index`, `name`, `kind`, `default_value` |
| POST | `/api/channels/{id}/update` | Update template channel metadata | `name`, `kind`, `default_value` |
| POST | `/api/channels/{id}/ranges` | Add value range to template channel | `start_value`, `end_value`, `label` |
| POST | `/api/channels/{id}/ranges/clear` | Clear all ranges on channel | - |

### Fixtures

| Method | Path | Purpose | Body fields |
|---|---|---|---|
| GET | `/api/fixtures` | List patched fixtures with live values | - |
| POST | `/api/fixtures` | Patch fixture | `name`, `template_id`, `start_address`, optional `universe`, `channel_count`, `allow_overlap` |
| POST | `/api/fixtures/{id}/channels/{channel}` | Set live DMX channel value | `value` |
| POST | `/api/fixtures/{id}/enabled` | Enable/disable fixture | `enabled` |
| POST | `/api/fixtures/reorder` | Reorder fixture cards/processing | `fixture_ids` (CSV) |
| POST | `/api/fixtures/{id}/delete` | Remove fixture patch | - |

### Groups

| Method | Path | Purpose | Body fields |
|---|---|---|---|
| GET | `/api/groups` | List groups | - |
| POST | `/api/groups` | Create group | `name` |
| POST | `/api/groups/{id}/fixtures` | Set group members | `fixture_ids` (CSV) |
| POST | `/api/groups/{id}/kinds/{kind}` | Apply value by channel kind | `value` |
| POST | `/api/groups/{id}/mode` | Apply mode by label | `label` |
| POST | `/api/groups/{id}/delete` | Delete group | - |

### Scenes

| Method | Path | Purpose | Body fields |
|---|---|---|---|
| GET | `/api/scenes` | List scenes | - |
| POST | `/api/scenes` | Create scene from current fixture values | `name`, optional `transition_seconds` |
| POST | `/api/scenes/{id}/update` | Rename/update transition | `name`, `transition_seconds` |
| POST | `/api/scenes/{id}/capture` | Capture current values into scene | - |
| POST | `/api/scenes/{id}/recall` | Recall/morph to scene | optional `transition_seconds` |
| POST | `/api/scenes/{id}/delete` | Delete scene | - |

### MIDI (Server-side)

| Method | Path | Purpose | Body fields |
|---|---|---|---|
| GET | `/api/midi` | MIDI backend/inputs/mappings status | - |
| POST | `/api/midi/input-mode` | Listen mode (`all` or specific input id) | `mode` |
| POST | `/api/midi/learn/start` | Arm learn for control id | `control_id` |
| POST | `/api/midi/learn/cancel` | Cancel learn | - |
| POST | `/api/midi/mappings/clear` | Clear mapping | `control_id` |

### DMX Engine

| Method | Path | Purpose | Body fields |
|---|---|---|---|
| GET | `/api/dmx/devices` | List compatible DMX output candidates + current selection | - |
| POST | `/api/dmx/devices/select` | Set preferred output device selection mode | `mode` (`auto` or `manual`), `device_id` (required when `mode=manual`) |
| POST | `/api/dmx/devices/scan` | Force reconnect and refresh DMX output candidates | - |
| POST | `/api/dmx/patches` | Apply temporary direct patches | `patches` (`universe:address:value,...`) |
| POST | `/api/dmx/blackout` | Panic blackout and disable reactive mode | - |
| POST | `/api/dmx/write-retry-limit` | Set write retries | `retries` (1..200) |
| POST | `/api/dmx/output-universe` | Select routed output universe | `universe` |
| POST | `/api/dmx/universes` | Ensure universe exists | `universe` |

`/api/status` and `/api/state` now include DMX device-selection fields:

- `dmx.devices[]` candidate list (`id`, `name`, `endpoint`, `serial`, firmware info, `connected`)
- `dmx.activeDeviceId`
- `dmx.preferredDeviceId`
- `dmx.selectionMode` (`auto` or `manual`)

### Audio Reactive

| Method | Path | Purpose | Body fields |
|---|---|---|---|
| POST | `/api/audio/reactive` | Toggle music reactive mode | `enabled` |
| POST | `/api/audio/reactive-threshold` | Set reactive threshold | `threshold` (0..1 float) |
| POST | `/api/audio/reactive-profile` | Set profile | `profile` (`balanced` or `volume_blackout`) |
| POST | `/api/audio/input-device` | Select audio input | `device_id` (`-1` = system default) |

## Minimal Custom UI Flow

1. Poll `GET /api/state`.
2. Render fixtures/channels from `state.fixtures`.
3. Send direct user actions with mutation endpoints:
   - channel fader move -> `POST /api/fixtures/{id}/channels/{channel}`
   - scene recall button -> `POST /api/scenes/{id}/recall`
   - blackout button -> `POST /api/dmx/blackout`
4. Re-poll `GET /api/state` after writes or on interval.

## Example Requests

Set fixture channel value:

```bash
curl -X POST "http://127.0.0.1:8080/api/fixtures/3/channels/5" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  --data "value=180"
```

Create scene:

```bash
curl -X POST "http://127.0.0.1:8080/api/scenes" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  --data "name=Battle%20Red&transition_seconds=1.2"
```

Set audio reactive profile:

```bash
curl -X POST "http://127.0.0.1:8080/api/audio/reactive-profile" \
  -H "Content-Type: application/x-www-form-urlencoded" \
  --data "profile=volume_blackout"
```

## Notes

- Endpoint surface is still evolving during experimental development.
- For stability, keep your custom client loosely coupled to `/api/state` and tolerant of added fields.
