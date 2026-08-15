# Blink Rest

Blink Rest is a small native macOS menu bar app that periodically covers every
connected display with a quiet eye-break prompt. The default schedule is a
30-second break after 20 minutes of active-session time.

The app has no account, analytics, network client, or third-party dependencies.
It does not inspect screen contents or request Accessibility, Input Monitoring,
Screen Recording, camera, or microphone permission.

## Install and open

For normal users, see the **[Blink Rest User Guide](docs/USER_GUIDE.md)**.

The short version:

1. Download the correct ZIP from the [latest release](https://github.com/fynxiu/blink-rest/releases/latest):
   use `macos-arm64` for Apple silicon or `macos-x86_64` for an Intel Mac.
2. Extract `BlinkRest.app` and move it to `/Applications`.
3. Open Finder > **Applications** > **Blink Rest**, or launch it with Spotlight.
4. Blink Rest has no normal Dock icon or main window. After launch, look for
   the **eye icon in the macOS menu bar** and click it.

Seeing `/Applications/BlinkRest.app` confirms that the app is installed. If the
menu bar is crowded and the eye is hidden, make room by closing other menu bar
utilities; when visible, Command-drag the Blink Rest icon farther to the right.

## Requirements

- macOS 14 or later
- Xcode 26 or another Xcode release capable of building for macOS 14

## Build and test from source

```bash
xcodebuild -project BlinkRest.xcodeproj -scheme BlinkRest \
  -configuration Debug -destination 'platform=macOS' \
  -derivedDataPath /private/tmp/BlinkRestDerivedData \
  CODE_SIGNING_ALLOWED=NO build

xcodebuild -project BlinkRest.xcodeproj -scheme BlinkRest \
  -destination 'platform=macOS' \
  -derivedDataPath /private/tmp/BlinkRestDerivedData \
  CODE_SIGNING_ALLOWED=NO \
  -only-testing:BlinkRestTests test
```

Open `BlinkRest.xcodeproj` in Xcode to run the app with normal local signing.
A signed, stably installed app bundle is required to validate Launch at Login.

For a local command-line install into `/Applications`:

```bash
make install
```

This builds a Release app, applies an ad-hoc local signature, stops any running
BlinkRest instance, and replaces `/Applications/BlinkRest.app`. Use `make run`
to install and launch it in one step, or `make uninstall` to remove it.

## Publishing

Publishing is dispatched through GitHub Actions so one workflow owns the tag,
platform builds, release assets, and final checksums. Start with a dry run:

```bash
make publish-dry-run VERSION=1.2.3 PLATFORMS=windows
```

Then publish from a clean `main` branch with the platforms that changed:

```bash
make publish VERSION=1.2.3 PLATFORMS=both
```

`PLATFORMS` accepts `macos`, `windows`, or `both` and defaults to `both`.
Versions use one global `vX.Y.Z` tag sequence, while individual platforms may
skip versions when they have no changes. Published releases are immutable:
adding another platform later requires a new version rather than modifying an
older release. Asset names are the compatibility contract used by the in-app
update checkers.

The workflow also creates unsigned human-facing installers while signing
credentials are unavailable: a DMG next to each macOS ZIP and a per-user Inno
Setup executable next to the Windows portable ZIP. The ZIP asset names remain
the compatibility contract used by update discovery.

## Usage

- Select the eye icon in the menu bar to see the next break.
- Choose **Take a break now** to start immediately.
- Pause for 30 minutes or one hour when interruption would be unsafe.
- During a break, hold Escape or the visible skip control for 1.5 seconds.
- VoiceOver, Voice Control, Switch Control, and Full Keyboard Access users can
  activate the skip control immediately.

Settings offer four work intervals, four break durations, and Launch at Login.
Changing the work interval restarts the current work cycle. Changing the break
duration affects the next break without postponing it.

Blink Rest does not suppress trackpad or macOS system gestures. Its break
windows use supported AppKit collection behaviors to remain present across
Spaces, full-screen applications, and Stage Manager without requesting Input
Monitoring or Accessibility permission.

## Limitations

Blink Rest is a behavioral interruption, not a kiosk or security lock. System
shortcuts and force-quit mechanisms remain available. The app does not detect
meetings, presentations, screen sharing, or whether someone is looking at the
display. Its schedule is a configurable reminder, not a medical treatment or
efficacy claim.

See [acceptance](docs/ACCEPTANCE.md), [architecture](docs/ARCHITECTURE.md),
[design](docs/DESIGN.md), [privacy](docs/PRIVACY.md), and the
[user guide](docs/USER_GUIDE.md) for details.
