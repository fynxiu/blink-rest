# Blink Rest for Windows

This directory contains the native Windows implementation. Development is
driven from macOS and continuously compiled and smoke-tested on a real Windows
11 machine over SSH.

## What is implemented now

- A dependency-free C++23 core library.
- The same work, warning, break, pause, and suspension state transitions as the
  current macOS reducer.
- The same supported break plans and half-open stage boundaries.
- Presentation reconciliation events that the future Win32 host can feed from
  display and virtual-desktop changes.
- Native CMake tests that compile and run on macOS and will also run with MSVC.
- A Win32 message-loop host with a native notification-area icon and menu.
- Per-monitor topmost break overlays rendered with Direct2D and DirectWrite.
- Per-Monitor v2 DPI handling, display reconciliation, and wake recovery.
- A native warning panel, foreground-window best-effort restoration, and
  lock/sleep/session lifecycle handling.
- Supported virtual-desktop presentation recovery via IVirtualDesktopManager;
  during warnings and breaks the host detects a foreground desktop change and
  asks the overlay coordinator to reconcile idempotently.
- Registry-backed schedule and pause persistence.
- Tray settings for the four work intervals and four break durations, plus
  per-user Launch at Login registration.
- Embedded Blink Rest application/tray icon resources derived from the same
  artwork used by the macOS app.
- Left-click tray popover with live countdown/progress and primary pause/resume
  controls; right-click retains the native quick-settings menu.
- Direct2D eye-stage geometry aligned with the macOS eye, filled-eye, and
  slashed-eye visual language, including the Blink breathing halo.

The portable core remains independent of AppKit, Swift, Win32, Direct2D,
registry, tray, and packaging code; those boundaries live in src/win32.

## Validate on macOS

From the repository root:

    cmake -S windows -B /private/tmp/BlinkRestWindowsMac -DCMAKE_BUILD_TYPE=Debug
    cmake --build /private/tmp/BlinkRestWindowsMac
    ctest --test-dir /private/tmp/BlinkRestWindowsMac --output-on-failure

This does not claim that the Windows desktop application works. It proves that
the platform-independent behavior compiles cleanly with a second native
toolchain and has executable regression tests before Win32 work begins.

## Remaining Windows acceptance and release work

The main remaining work is interactive and release-oriented:

- Interactive acceptance for fullscreen apps, multiple virtual desktops,
  multi-monitor/DPI changes, system shortcuts, and sleep/wake cycles.
- Public-release Authenticode signing still requires a code-signing
  certificate; the release packager reports this explicitly.

## Build a Windows release asset

On Windows, create an x64 release ZIP and SHA-256 manifest with:

    powershell -ExecutionPolicy Bypass -File tools\package_release.ps1 -Version 1.1.0

The packager configures and builds Release with MSVC, runs CTest, runs the
headless executable smoke test, stages BlinkRest.exe plus this README, creates
BlinkRest-vX.Y.Z-windows-x64.zip, and writes SHA256SUMS.txt. It also locates
SignTool from the installed Windows SDK and reports the executable's
Authenticode status. An unsigned package is explicitly treated as a local/test
artifact; public release signing still requires a code-signing certificate.
The requested semantic version is also stamped into the executable's Windows
version resource and verified before packaging.
The same command also creates `BlinkRest-vX.Y.Z-windows-x64-setup.exe` with
Inno Setup. It installs per-user under `%LOCALAPPDATA%\Programs\BlinkRest`,
so elevation is not required. Until Authenticode credentials are configured,
both the portable executable and installer are expected to remain unsigned.

Production releases are orchestrated by `.github/workflows/release.yml`.
Each `vX.Y.Z` GitHub Release may contain macOS assets, the Windows x64 asset,
or both, selected explicitly at dispatch time. A published release is treated
as immutable; a later Windows-only or macOS-only change uses a new global tag.

Measure the idle Release process with:

    powershell -ExecutionPolicy Bypass -File tools\measure_release.ps1 \
        -Executable build-release\Release\blinkrest_win32.exe

The probe launches the native host with --resident-smoke. That mode reuses the
headless initialization path so it does not depend on Explorer tray services,
but stays in the normal Win32 message loop instead of exiting immediately. The
probe records input-idle startup latency, CPU usage over a short idle sample,
private bytes, private working set (when the Windows performance class is
available), total working set, and executable size, then terminates the sampled
process. This is a reproducible resident-host baseline; interactive desktop
acceptance remains a separate manual check.

The Win32 layer should translate operating-system events into this core rather
than duplicating timing or state-transition policy in window code.
