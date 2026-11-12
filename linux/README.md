# Blink Rest for Linux

The Linux version is a **Qt 6 desktop application** backed by the same C++23
state machine and break-plan implementation used by Blink Rest for Windows.
It does not depend on Windows APIs or a macOS runtime.

## Supported environments

- Ubuntu 22.04 and 24.04 graphical desktops, plus Debian-family distributions
  with Qt 6.2+ and a sufficiently recent glibc. Linux packages are built on
  Ubuntu 22.04 for compatibility with the Qt 6 naming transition.
- X11 for the most reliable always-on-top multi-monitor break windows.
  For native Wayland, install the optional qt6-wayland platform plugin.
- Wayland as a **best-effort** desktop: compositor policies may restrict window
  stacking, tray availability and fullscreen overlays. The application cannot
  override fullscreen surfaces on every Wayland compositor.
- Both x86-64 (amd64) and AArch64 (arm64) are built by the release workflow.

**Not supported:** headless servers, text-only consoles, lock screens and
forcing overlays above system-owned surfaces. Blink Rest is not a kiosk.

## Install

Download the appropriate Debian package from a GitHub release, then:

    sudo apt install ./BlinkRest-vX.Y.Z-linux-amd64.deb

Use linux-arm64.deb on an ARM64 system. The package installs Qt platform plugins through normal apt dependencies,
a desktop launcher
and the Blink Rest icon. Open **Blink Rest** from the desktop menu or run
**blinkrest** from a terminal.

The matching .tar.gz contains an unbundled executable and XDG desktop/icon
files. It **requires** Qt 6 runtime libraries on the host; it is not a static
binary or AppImage. Extract it and run **usr/bin/blinkrest** from the extracted
directory. For most users, the Debian package is preferable.

## Using the application

Click the eye icon in the system tray to open a **circular countdown
popover**, following the same 304 px visual layout as Blink Rest for macOS and
Windows. Right-click the tray icon for native quick settings. The controls
provide:

- **Take a break now**, pause for 30 or 60 minutes, or resume.
- Work intervals of **20/25/30/40 minutes**.
- Break durations of **30/45/60/90 seconds**.
- **Launch at login**, status/settings window, releases and Quit.

If your desktop does not support a system tray (for example some unmodified
GNOME Shell installations), Blink Rest shows an ordinary status/settings
countdown window instead, including the same controls and a Quit button.
Settings open in a separate grouped window with segmented interval controls.

Each break covers the Qt-recognized screens, progressing through **Look far**,
**Blink gently** and **Close your eyes** with the same central progress ring,
eye-stage glyphs, and subdued breathing halo as macOS and Windows. The
1.5-second hold-to-skip control is in the upper-right corner. Hold Escape or the visible skip button
for **1.5 seconds** to end a break deliberately. Accessibility users can
press **Ctrl+Shift+S** while the overlay is focused to skip immediately. Breaks finish automatically.

Warnings appear five seconds before a scheduled break. Changes to the work
interval start a new cycle; duration changes affect the *next* break. Pause
expiry persists across app restart.

Login autostart is a user-owned XDG file under
**~/.config/autostart/blinkrest.desktop**; no administrator permission is needed.
The app stores its schedule and pause time under the user's XDG configuration
directory (normally **~/.config/BlinkRest/BlinkRest.conf**).

## Build and test from source

Install build dependencies on Ubuntu 22.04 or 24.04:

    sudo apt install build-essential cmake ninja-build qt6-base-dev libgl1-mesa-dev

Then run from the repository root:

    cmake -S linux -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build/linux
    ctest --test-dir build/linux --output-on-failure
    ./build/linux/blinkrest

The **linux_gui_smoke** test uses Qt's offscreen platform plugin; it does not
prove that your compositor will honor topmost/fullscreen windows. Interactive
tests on X11 and Wayland are necessary before promising desktop-specific
behavior.

To build a Debian package and tarball on Ubuntu:

    linux/tools/package_release.sh 1.2.3 /tmp/blinkrest-dist

Package version numbers use the repository's shared release tag sequence.
Uninstall via **sudo apt remove blinkrest** or remove the manually extracted
tarball. If enabled, disable login autostart in settings before uninstalling
(or remove the XDG autostart file).

## Privacy and integration

Linux Blink Rest has no tracking, accounts, or network calls during normal
operation. **View latest releases** opens your web browser on GitHub. The app
does not inspect foreground windows, monitor global input or demand
Accessibility-like permissions. Sleep/wake and lock/unlock handling uses
ordinary logind and desktop screen-saver D-Bus signals where available.

Set BLINKREST_REDUCE_MOTION=1 in the launch environment to disable the
decorative blinking halo animation; the progress ring and timing continue to
work. The Linux GUI is currently English-only. X11/Wayland compositor behavior,
fractional DPI, screen hot-plugging and sleep/wake should be verified on real
hardware for each supported desktop environment.

### If dpkg reports missing libqt6*t64 dependencies

The original v1.2.3 Linux Debian packages were built on Ubuntu 24.04
and required Qt6 library package names that do not exist on Ubuntu 22.04.
Use a newer, Ubuntu-22.04-built release. A Debian package installed using
dpkg -i does not resolve dependencies automatically. Instead use:

    sudo apt install ./BlinkRest-vX.Y.Z-linux-amd64.deb

Replace amd64 with arm64 when appropriate. On Ubuntu 24.04, apt can resolve
legacy Qt6 package names through the t64 packages' Provides declarations.
To repair an incomplete installation first, use:

    sudo apt --fix-broken install

Check the proposed changes before accepting them; on an unsupported distro,
apt may propose removing Blink Rest instead.
