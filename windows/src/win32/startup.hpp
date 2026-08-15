#pragma once

namespace blinkrest::win32 {

class StartupRegistration {
public:
    bool is_enabled() const;
    bool set_enabled(bool enabled) const;
};

}  // namespace blinkrest::win32
