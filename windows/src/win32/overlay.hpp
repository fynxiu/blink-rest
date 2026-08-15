#pragma once

#include <blinkrest/core.hpp>

#include <windows.h>

#include <functional>
#include <memory>

namespace blinkrest::win32 {

class OverlayManager {
public:
    OverlayManager(HINSTANCE instance, std::function<void()> on_skip);
    ~OverlayManager();

    OverlayManager(const OverlayManager&) = delete;
    OverlayManager& operator=(const OverlayManager&) = delete;

    bool initialize();
    void present(const BreakSession& session, MonotonicInstant at);
    void update(const BreakSession& session, MonotonicInstant at);
    void dismiss();
    void discard_cached_after_wake();
    void cancel_hold();
    void reconcile();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace blinkrest::win32
