# Design

## Product principles

Blink Rest is quiet, precise, restrained, and native. It occupies one menu bar
item during normal use and avoids statistics, streaks, accounts, gamification,
articles, and medical promises. Each state presents one primary action.

## Interface

- The menu popover shows status, one countdown, one primary action, pause,
  Settings, and Quit.
- A nonactivating warning appears five seconds before a scheduled break.
- One opaque AppKit window per display covers the full screen during a break.
- Break plans use three phases: look at a real distant
  object, blink slowly and fully, and close the eyes.
- The final phase states that the display will clear automatically, so the user
  does not need to look back to determine when the break is over.
- Escape and pointer hold controls provide a deliberate skip path. Assistive
  technologies receive a directly activatable alternative.

## Visual system

- System font and SF Symbols
- 8-point spacing grid
- Graphite overlay `#0E1013`
- Primary text `#F5F7FA`
- Secondary text `#A6AFBC`
- Accent `#77A9FF`
- No rotation, spring motion, particles, parallax, or simulated depth

Reduce Motion disables breathing and decorative transitions. Reduce
Transparency replaces the warning material with an opaque surface. Color is
never the only status indicator.

The editable app-icon source is `Design/AppIcon.svg`. The previous design board
was intentionally not included because it described excluded functionality and
a light floating overlay that conflicted with the implemented product.


## Cross-platform visual and interaction contract

All implementations use a 304 px-wide primary countdown surface with an
app-name/status header, circular progress ring, MM:SS countdown, context caption,
prominent primary action, and secondary pause control. Settings and Quit
appear in the popover footer on macOS/Linux and in the right-click menu on
Windows. macOS uses SwiftUI; Windows uses Direct2D; Linux uses Qt Widgets and QPainter.
Platform-native controls may differ in rendering details, but must preserve
layout hierarchy, timing semantics and interaction order.

During a break, each display uses the same #0E1013 opaque background, a 188 px
progress circle, semantic eye/filled-eye/slashed-eye symbols, a 40 px headline,
and 16 px supporting text. A restrained #77A9FF progress arc indicates break
progress. During the blink stage, a subtle 1.8 s breathing halo animates (when
motion is allowed). Place skip instructions and a 1.5 s pointer/keyboard hold
control at the top right. On Linux, Ctrl+Shift+S is also an immediate accessible
alternative while the overlay has focus.

The warning is a small non-modal pill, not an intrusive full window or desktop
notification. Settings group Schedule, Updates and System. Schedule choices
use segmented, mutually exclusive controls; changing the work interval resets
the current work cycle, while changing break duration leaves it intact.

On Linux, a normal system-tray click opens the same countdown popover. Right
click retains quick settings. If the desktop environment does not support the
system tray (notably stock GNOME), Linux shows the same countdown surface as
a small standalone window rather than exposing a bare settings form.
Wayland stacking and cross-app global Escape interception cannot be made
identical to the macOS and Windows implementations without compositor-specific
privileges; do not claim otherwise.
