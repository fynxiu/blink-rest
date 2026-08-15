#pragma once

#include <windows.h>

#include <memory>

namespace blinkrest::win32 {

class WarningPanel {
public:
    explicit WarningPanel(HINSTANCE instance);
    ~WarningPanel();

    WarningPanel(const WarningPanel&) = delete;
    WarningPanel& operator=(const WarningPanel&) = delete;

    bool initialize();
    void show(int seconds_remaining);
    void hide();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace blinkrest::win32
