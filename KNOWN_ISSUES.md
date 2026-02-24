# Known Issues

TuxDMX is currently experimental. The issues below are actively tracked.

## Device/Transport

- Intermittent disconnect/reconnect events may occur on some DMX USB Pro-compatible setups, especially under repeated high-frequency updates.
  Typical symptom: UI reports `Device not connected. Port probe failed`, then recovers later.
  Current focus areas: USB cable quality, port power stability, adapter/chipset behavior, and write retry strategy.

## Fixture Template Behavior

- Some fixture templates still need tuning for edge cases, especially moving-head fixtures with multiple control modes.
- Music-reactive behavior may require profile/range adjustment per fixture family to avoid unwanted motion or effects.

## Platform Validation

- Windows and Linux launch tooling exists, but broader hardware matrix validation is still in progress.
- Community bug reports with logs and reproduction steps are very valuable during this stage.

## Reporting Guidance

When opening an issue, include:

- OS + version
- DMX interface model
- fixture model/template
- exact steps to reproduce
- `data/tuxdmx.log` excerpt around the failure time
