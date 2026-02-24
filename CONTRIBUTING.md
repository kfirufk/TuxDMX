# Contributing to TuxDMX

Thanks for contributing.

This project is in active experimental development. Feedback, bug reports, and focused PRs are welcome.

## Before You Start

- Review [KNOWN_ISSUES.md](./KNOWN_ISSUES.md).
- Open an issue first for significant changes.
- Keep PRs scoped and easy to review.

## Development Setup

See platform-specific instructions:

- [Linux](./docs/setup/linux.md)
- [macOS](./docs/setup/macos.md)
- [Windows](./docs/setup/windows.md)
- [DMX backend development guide](./docs/development/dmx-backends.md)

## Build and Test

```bash
cmake --preset ninja-debug
cmake --build --preset build-debug
ctest --preset test-debug
```

## Coding Expectations

- Target standard: **C++23**.
- Keep changes minimal and focused.
- Prefer explicit, readable logic over clever abstractions.
- Update docs when behavior or APIs change.
- Add or update tests when practical.

## Bug Reports

Please include:

- OS and version
- DMX interface model
- fixture template used
- exact reproduction steps
- expected vs actual behavior
- relevant log lines from `data/tuxdmx.log`

## Pull Requests

- Use descriptive commit messages.
- Link related issue(s).
- Mention test/build commands you ran.
- If a change is platform-specific, state the platform tested.
