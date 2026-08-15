# Cross-platform behavior contract
Blink Rest has separate native macOS and Windows implementations. Behavioral
consistency comes from matching this state-machine contract, not from sharing a
cross-platform UI framework.

## Scheduling

- The default work interval is 20 minutes.
- The default break duration is 30 seconds.
- The warning begins during the final 5 seconds of a work interval.
- Work and break deadlines use monotonic time.
- Persisted pause expiration uses wall-clock time.
- A delayed tick at or beyond a work deadline starts a full break immediately.
- Completing or skipping a break starts a fresh full work interval.
- Changing the work interval starts a fresh work cycle.
- Changing the break duration does not move the current work deadline and does
  not alter a break already in progress.

## Break protocol

The stages are look far, blink, and close eyes. Supported plans are:

| Total | Look far | Blink | Close eyes |
| ---: | ---: | ---: | ---: |
| 10 s | 2 s | 5 s | 3 s |
| 20 s | 3 s | 9 s | 8 s |
| 30 s | 3 s | 12 s | 15 s |
| 45 s | 4.5 s | 18 s | 22.5 s |
| 60 s | 6 s | 24 s | 30 s |
| 90 s | 9 s | 36 s | 45 s |

Unsupported break durations fall back to the 30-second plan. Stage intervals
are half-open except for the final stage, which owns the clamped end point.

## Pause and suspension

- A future pause survives restart, sleep, and inactive-session transitions.
- An expired persisted pause is cleared before a fresh work cycle starts.
- Suspension reasons are tracked independently for system sleep, display sleep,
  and an inactive login session.
- Resume occurs only after every active suspension reason has cleared.
- After the final reason clears, resumption is delayed by 0.5 seconds.
- Entering suspension hides warnings, cancels an escape hold, dismisses overlays,
  discards the remembered foreground application, and cancels a pending resume.
- Wake invalidates cached overlay windows before presentation can resume.

## Presentation boundary

The state machine emits presentation effects but does not own native windows.
Each platform maps those effects to its own supported system APIs.

macOS maps presentation-context changes from Spaces and display topology.
Windows will map the same logical event from display topology and the supported
signals available around virtual-desktop and foreground-context changes.

During a break, a presentation-context or display change asks the native window
coordinator to reconcile the overlay set idempotently.

## System integrity

Blink Rest is a behavioral interruption, not a kiosk or security boundary.
Neither implementation should install broad input interception merely to defeat
normal operating-system navigation. Lock screens, secure desktops, force-quit
paths, accessibility features, and system-owned shortcuts remain available.
