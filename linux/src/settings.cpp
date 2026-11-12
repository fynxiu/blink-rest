#include "settings.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <array>
#include <cmath>


namespace blinkrest::linux_ui {
namespace {
QSettings open_settings() {
    return QSettings(QSettings::IniFormat, QSettings::UserScope, "BlinkRest", "BlinkRest");
}

template<std::size_t N>
int allowed_or_default(int value, const std::array<int, N>& allowed, int fallback) {
    for (int option : allowed) if (value == option) return value;
    return fallback;
}

QString escaped_exec_path(QString path) {
    // The Desktop Entry Exec key uses % for field codes and a quoted argument
    // with backslash-escaped special characters.
    path.replace("%", "%%");
    path.replace("\\", "\\\\");
    path.replace("\"", "\\\"");
    path.replace("$", "\\$");
    path.replace(QChar(0x60), QStringLiteral("\\") + QChar(0x60));
    return QStringLiteral("\"") + path + QStringLiteral("\"");
}
constexpr auto marker = "X-BlinkRest-Autostart=true";
}

UserSettings SettingsStore::load() const {
    auto settings = open_settings();
    const int work = allowed_or_default(
        settings.value("workMinutes", 20).toInt(), std::array{20, 25, 30, 40}, 20);
    const int rest = allowed_or_default(
        settings.value("breakSeconds", 30).toInt(), std::array{30, 45, 60, 90}, 30);
    const QVariant pause = settings.value("pauseUntil");
    std::optional<WallInstant> until;
    if (pause.isValid()) {
        bool valid = false;
        const double parsed = pause.toDouble(&valid);
        if (valid && parsed > 0 && std::isfinite(parsed)) until = parsed;
    }
    return {{static_cast<double>(work) * 60, static_cast<double>(rest), 5.0}, until};
}

void SettingsStore::save_work_interval(int minutes) const {
    auto settings = open_settings();
    settings.setValue("workMinutes", minutes);
    settings.sync();
}
void SettingsStore::save_break_duration(int seconds) const {
    auto settings = open_settings();
    settings.setValue("breakSeconds", seconds);
    settings.sync();
}
void SettingsStore::save_pause(std::optional<WallInstant> until) const {
    auto settings = open_settings();
    if (until) settings.setValue("pauseUntil", *until);
    else settings.remove("pauseUntil");
    settings.sync();
}

QString SettingsStore::autostart_file_path() {
    const auto config = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return QDir(config).filePath("autostart/blinkrest.desktop");
}

bool SettingsStore::autostart_enabled() const {
    QFile file(autostart_file_path());
    if (!file.open(QIODevice::ReadOnly)) return false;
    return file.readAll().contains(marker);
}

bool SettingsStore::set_autostart(bool enabled, QString* error) const {
    const QString path = autostart_file_path();
    QFile existing(path);
    if (existing.exists()) {
        if (!existing.open(QIODevice::ReadOnly)) {
            if (error) *error = QString("Cannot read %1").arg(path);
            return false;
        }
        const bool ours = existing.readAll().contains(marker);
        existing.close();
        if (!ours) {
            if (error) *error = QString("Refusing to overwrite an unrelated file: %1").arg(path);
            return false;
        }
    }
    if (!enabled) {
        if (QFile::exists(path) && !QFile::remove(path)) {
            if (error) *error = QString("Cannot remove %1").arg(path);
            return false;
        }
        return true;
    }

    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (error) *error = "Cannot create the XDG autostart directory";
        return false;
    }
    const QString exe = QCoreApplication::applicationFilePath();
    const QString desktop = QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Blink Rest\n"
        "Comment=Periodic eye-break reminders\n"
        "Exec=%1\n"
        "Icon=blinkrest\n"
        "Terminal=false\n"
        "Categories=Utility;\n"
        "X-GNOME-Autostart-enabled=true\n"
        "X-BlinkRest-Autostart=true\n").arg(escaped_exec_path(exe));
    QSaveFile destination(path);
    if (!destination.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = destination.errorString();
        return false;
    }
    const auto bytes = desktop.toUtf8();
    if (destination.write(bytes) != bytes.size() || !destination.commit()) {
        if (error) *error = destination.errorString();
        return false;
    }
    return true;
}
} // namespace blinkrest::linux_ui
