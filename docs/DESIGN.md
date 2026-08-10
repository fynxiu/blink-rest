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
