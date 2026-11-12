#pragma once
#include <blinkrest/core.hpp>

#include <QString>

#include <optional>

namespace blinkrest::linux_ui {

struct UserSettings {
    SessionSchedule schedule;
    std::optional<WallInstant> pause_until;
};

class SettingsStore {
public:
    UserSettings load() const;
    void save_work_interval(int minutes) const;
    void save_break_duration(int seconds) const;
    void save_pause(std::optional<WallInstant> until) const;

    bool autostart_enabled() const;
    bool set_autostart(bool enabled, QString* error) const;

    static QString autostart_file_path();
};

} // namespace blinkrest::linux_ui
