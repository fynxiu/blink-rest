# Architecture

## State and time

`SessionController` owns the single session state and executes effects emitted
by the pure `SessionReducer`. Work and break deadlines use a monotonic clock;
only persisted pause expiration uses wall-clock `Date`. A delayed tick advances
directly to the state appropriate for the current instant, with break deadlines
taking precedence over warning thresholds.

Suspension uses a set of concrete reasons: system sleep, display sleep, and an
inactive login session. The session resumes only after every active reason has
cleared and a short debounce has elapsed. A still-valid persisted pause remains
paused across sleep and session switching.

Work-interval and break-duration changes are distinct events. A work-interval
change starts a fresh cycle; a break-duration change preserves the current work
deadline and is snapshotted when the next break begins.

## UI and AppKit boundary

SwiftUI renders menu, settings, warning content, and break content. AppKit owns
the warning panel and break windows because window level, Spaces, full-screen
auxiliary behavior, key-window handling, and per-display reconciliation are
not view concerns.

`OverlayWindowCoordinator` reconciles stable display descriptors into one
`BreakWindow` per display. Presentation, dismissal, event-monitor installation,
and reconciliation are idempotent. `FrontmostApplicationManager` captures the
last non-Blink-Rest app and restores it only as a best effort.

## Testability

System boundaries are protocols: time source, scheduler, settings persistence,
warning presentation, overlay presentation, frontmost app management, display
topology, window construction, and login-item control. Unit tests use fakes and
advance time without real sleeps. UI tests use debug-only launch arguments and
a supported 10-second break plan.
