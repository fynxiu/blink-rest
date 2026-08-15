#pragma once

#include <windows.h>

#include <functional>
#include <memory>

namespace blinkrest::win32 {

enum class TrayPopoverPhase {
    running,
    warning,
    paused,
    breaking,
};

struct TrayPopoverPresentation {
    TrayPopoverPhase phase = TrayPopoverPhase::running;
    int remaining_seconds = 0;
    double progress = 0.0;
};

class TrayPopover {
public:
    using VoidCallback = std::function<void()>;
    using PauseCallback = std::function<void(int)>;

    TrayPopover(
        HINSTANCE instance,
        VoidCallback take_break_now,
        PauseCallback pause_for_seconds,
        VoidCallback resume
    );
    ~TrayPopover();

    TrayPopover(const TrayPopover&) = delete;
    TrayPopover& operator=(const TrayPopover&) = delete;

    bool initialize();
    void update(const TrayPopoverPresentation& presentation);
    void toggle(const RECT& anchor);
    void hide();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace blinkrest::win32
