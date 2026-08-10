# Blink Rest

Blink Rest is a small native macOS menu bar app that periodically covers every
connected display with a quiet eye-break prompt. The default schedule is a
20-second break after 20 minutes of active-session time.

The app has no account, analytics, network client, or third-party dependencies.
It does not inspect screen contents or request Accessibility, Input Monitoring,
Screen Recording, camera, or microphone permission.

## Requirements

- macOS 14 or later
- Xcode 26 or another Xcode release capable of building for macOS 14

## Build and test

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

## Usage

- Select the eye icon in the menu bar to see the next break.
- Choose **Take a break now** to start immediately.
- Pause for 30 minutes or one hour when interruption would be unsafe.
- During a break, hold Escape or the visible skip control for 1.5 seconds.
- VoiceOver, Voice Control, Switch Control, and Full Keyboard Access users can
  activate the skip control immediately.

Settings offer four work intervals, three break durations, and Launch at Login.
Changing the work interval restarts the current work cycle. Changing the break
duration affects the next break without postponing it.

## Limitations

Blink Rest is a behavioral interruption, not a kiosk or security lock. System
shortcuts and force-quit mechanisms remain available. The app does not detect
meetings, presentations, screen sharing, or whether someone is looking at the
display. Its schedule is a configurable reminder, not a medical treatment or
efficacy claim.

See [acceptance](docs/ACCEPTANCE.md), [architecture](docs/ARCHITECTURE.md),
[design](docs/DESIGN.md), and [privacy](docs/PRIVACY.md) for details.
