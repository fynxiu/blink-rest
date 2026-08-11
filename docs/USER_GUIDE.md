# Blink Rest User Guide

Blink Rest is a macOS menu bar app that reminds you to take regular eye
breaks. It does not open a normal application window or keep an icon in the
Dock. After Blink Rest starts, you use it from the eye icon in the macOS menu
bar.

## Install Blink Rest

### 1. Download the correct build

Open the [latest Blink Rest release](https://github.com/fynxiu/blink-rest/releases/latest)
and download the ZIP file for your Mac:

- Apple silicon Macs (M1, M2, M3, M4, and later): download the asset ending in
  `-macos-arm64.zip`.
- Intel Macs: download the asset ending in `-macos-x86_64.zip`.

If you are unsure which Mac you have, choose Apple menu > **About This Mac**.
A Mac showing an Apple M-series chip uses the arm64 build. A Mac showing an
Intel processor uses the x86_64 build.

### 2. Move Blink Rest to Applications

1. Double-click the downloaded ZIP file to extract it.
2. Move `BlinkRest.app` into the **Applications** folder.
3. Open Finder and choose **Applications** in the sidebar.
4. Confirm that **Blink Rest** is listed there.

If Blink Rest appears in Applications, installation is complete. The normal
installed location is:

```text
/Applications/BlinkRest.app
```

## Open Blink Rest

You can start Blink Rest in either of these ways:

- Open Finder > **Applications** and double-click **Blink Rest**.
- Press Command-Space, type **Blink Rest**, and press Return.

After launch, Blink Rest intentionally does **not** show a normal app window
or Dock icon. Look for the **eye icon** in the right side of the macOS menu
bar. Click the eye to open Blink Rest.

### If macOS blocks the first launch

Blink Rest is distributed outside the Mac App Store. If macOS displays a
security warning and does not offer an Open button, open **System Settings >
Privacy & Security**, find the message about Blink Rest, choose **Open Anyway**,
and confirm the launch. Only do this when you downloaded Blink Rest from this
project's official GitHub release.

## If You Cannot See the Eye Icon

A crowded Mac menu bar, especially on a MacBook with a camera notch, can hide
third-party menu bar items even when Blink Rest is running.

1. Open Blink Rest again from Applications or Spotlight. This confirms you are
   launching the installed app rather than looking for a normal window.
2. Temporarily quit or hide other menu bar utilities to make room.
3. When the Blink Rest eye is visible, hold Command and drag it farther to the
   right so it is less likely to be hidden.

macOS controls the final menu bar layout; Blink Rest cannot force its status
item to remain visible when the menu bar runs out of space.

## Use Blink Rest

Click the eye icon to see the current countdown and controls.

- **Take a break now** starts an eye break immediately.
- **Pause** suspends reminders for 30 minutes or one hour.
- **Resume** restarts reminders after a pause.
- **Settings** lets you choose the work interval, break duration, and whether
  Blink Rest should launch when you log in.
- **Quit** exits Blink Rest.

By default, Blink Rest starts a 30-second break after 20 minutes of active
session time. Five seconds before a scheduled break, a small warning appears.
During the break, Blink Rest covers every connected display and guides you
through the break stages.

To intentionally skip an active break, hold **Escape** or hold the visible
skip control for 1.5 seconds.

## Launch at Login

Open the Blink Rest menu, choose **Settings**, and enable **Launch at Login**
if you want reminders to start automatically after you sign in to your Mac.

## Uninstall Blink Rest

1. Open the Blink Rest menu and choose **Quit**.
2. If Launch at Login is enabled, disable it in Blink Rest Settings first, or
   remove Blink Rest from **System Settings > General > Login Items**.
3. Open Finder > **Applications**.
4. Move **Blink Rest** to the Trash.

Developers who installed from the source repository can instead run:

```bash
make uninstall
```

## Install From Source

This section is for developers. From the repository root, run:

```bash
make install
```

The command builds a Release app, signs it locally, stops an existing Blink
Rest process, and installs the result at `/Applications/BlinkRest.app`. A
successful installation ends with:

```text
Installed: /Applications/BlinkRest.app
```

To install and immediately launch the app, run:

```bash
make run
```

## Troubleshooting Checklist

- **I installed it but nothing opened:** Blink Rest has no normal app window or
  Dock icon. Open it from Applications or Spotlight, then look for the eye in
  the menu bar.
- **I cannot find it in Applications:** move the extracted `BlinkRest.app` into
  `/Applications` and try again.
- **The eye icon disappeared:** the menu bar may be out of space; make room by
  hiding or quitting other menu bar utilities.
- **I want to check it immediately:** click the eye icon and choose **Take a
  break now** instead of waiting for the scheduled countdown.

For implementation details, see [Architecture](ARCHITECTURE.md),
[Design](DESIGN.md), [Privacy](PRIVACY.md), and [Acceptance](ACCEPTANCE.md).
