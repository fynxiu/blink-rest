#pragma once

#include <blinkrest/core.hpp>

#include <windows.h>

#include <optional>

namespace blinkrest::win32 {

struct StoredSettings {
    SessionSchedule schedule{};
    std::optional<WallInstant> pause_until;
};

class RegistrySettingsStore {
public:
    RegistrySettingsStore();
    ~RegistrySettingsStore();

    RegistrySettingsStore(const RegistrySettingsStore&) = delete;
    RegistrySettingsStore& operator=(const RegistrySettingsStore&) = delete;

    StoredSettings load();
    void persist_schedule(const SessionSchedule& schedule);
    void persist_pause_until(std::optional<WallInstant> value);

private:
    HKEY key_ = nullptr;
};

}  // namespace blinkrest::win32
