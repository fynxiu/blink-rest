# Privacy

Blink Rest has no account, advertising, analytics, telemetry, crash-upload SDK,
or network client. The sandbox has no outgoing-network entitlement. The app does
not access screen contents, files, camera, microphone, contacts, calendar, or
browser data.

## Local data

UserDefaults stores only:

- settings schema version
- work interval
- break duration
- optional pause expiration date

Launch-at-login state comes from `SMAppService` rather than a duplicated local
Boolean. Break history, skip history, usage duration, window titles, and app
names are not persisted.

The app temporarily holds a reference to the previously active application in
memory so it can restore focus after a break. That reference is discarded after
cleanup and is never logged or saved.

Release diagnostics use Apple unified logging for errors and coarse lifecycle
state only. They do not include user content, window titles, application names,
or high-frequency timer events. macOS controls unified-log retention and may
include system logs in diagnostics collected by the user or Apple.
