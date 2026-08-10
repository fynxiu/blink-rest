# Acceptance

Last updated: 2026-08-04.

## Automated gates

The following results are from the final Swift 6 validation run. Xcode emitted
host-environment diagnostics about unavailable CoreSimulator and file-event
services; no Blink Rest source warning or error was emitted.

- [x] Debug unsigned build, exit 0:
  `xcodebuild -quiet -project BlinkRest.xcodeproj -scheme BlinkRest -configuration Debug -destination 'platform=macOS,arch=arm64' -derivedDataPath /private/tmp/BlinkRestFinalCurrent CODE_SIGNING_ALLOWED=NO build`
- [x] App, 66 unit tests, and one UI test compiled, exit 0:
  `xcodebuild -quiet -project BlinkRest.xcodeproj -scheme BlinkRest -configuration Debug -destination 'platform=macOS,arch=arm64' -derivedDataPath /private/tmp/BlinkRestFinalCurrent CODE_SIGNING_ALLOWED=NO build-for-testing`
- [ ] Unit-test execution. The test action exited 65 after compilation because
  `com.apple.testmanagerd.control` lookup failed with sandbox error 159; no test
  result is claimed.
- [ ] UI smoke-test execution. It requires the same unavailable test service and
  an approved interactive app session; the test remains compiled in the scheme.
- [x] Universal Release unsigned build, exit 0:
  `xcodebuild -quiet -project BlinkRest.xcodeproj -scheme BlinkRest -configuration Release -destination 'generic/platform=macOS' -derivedDataPath /private/tmp/BlinkRestFinalNoCoverageRelease CODE_SIGNING_ALLOWED=NO clean build`
- [x] Release coverage instrumentation is explicitly disabled. `otool`, `nm`,
  and `strings` found no LLVM coverage sections, profile symbols, or
  `default.profraw` path in the universal product.
- [x] Static analyzer, exit 0:
  `xcodebuild -quiet -project BlinkRest.xcodeproj -scheme BlinkRest -configuration Debug -destination 'platform=macOS,arch=arm64' -derivedDataPath /private/tmp/BlinkRestFinalAnalyze CODE_SIGNING_ALLOWED=NO clean analyze`
- [x] `plutil -lint` for `Info.plist`, entitlements, project file, and compiled
  English and Simplified Chinese strings, exit 0.
- [x] Catalog JSON parsing and `xcstringstool compile --dry-run` for `en` and
  `zh-Hans`, exit 0.
- [x] `git diff --check`, exit 0.
- [x] Source audits found no placeholders, third-party package references,
  network clients, global keyboard monitors, or restricted permission keys.

The attempted unit-test command was:

```bash
xcodebuild -quiet -project BlinkRest.xcodeproj -scheme BlinkRest \
  -configuration Debug -destination 'platform=macOS,arch=arm64' \
  -derivedDataPath /private/tmp/BlinkRestFinalTests \
  CODE_SIGNING_ALLOWED=NO -only-testing:BlinkRestTests test
```

## Manual matrix

Unchecked items have not been claimed as passed. Native app execution approval
was unavailable in the final environment, so no interactive item is checked.

- [ ] Single-display desktop
- [ ] Dual-display extended desktop
- [ ] Another application in a full-screen Space
- [ ] Multiple Spaces
- [ ] Stage Manager on and off
- [ ] Attach and detach a display during a break
- [ ] System sleep and wake
- [ ] Display sleep and wake
- [ ] Lock/unlock or inactive/active user session
- [ ] Light and Dark appearance
- [ ] Reduce Motion
- [ ] Reduce Transparency
- [ ] English and Simplified Chinese
- [ ] Pause across application restart
- [ ] Pause across sleep and session switching
- [ ] Signed Launch at Login registration, approval, and removal
- [ ] Twenty repeated natural and skipped breaks
- [ ] Release CPU and memory sampling for 60 seconds
- [ ] VoiceOver, Voice Control, Switch Control, and Full Keyboard Access
- [ ] Force Quit, lock screen, accessibility shortcuts, and system alerts remain available

## Performance procedure

For a signed Release build with the popover closed, record the Mac model, macOS
version, display count, and scale. After a 30-second warm-up, sample process CPU
and resident memory once per second for 60 seconds. Report average CPU, peak CPU,
and peak resident memory; investigate sustained CPU above 1% or memory above
80 MB rather than treating hardware-sensitive figures as universal guarantees.
