#include "../src/settings.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cmath>
#include <cstdlib>

using namespace blinkrest::linux_ui;

void check(bool ok) { if (!ok) std::abort(); }

int main(int argc, char* argv[]) {
    QTemporaryDir temp;
    check(temp.isValid());
    qputenv("XDG_CONFIG_HOME", temp.path().toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication app(argc, argv);

    SettingsStore store;
    auto original = store.load();
    check(original.schedule.effective_work_interval() >= 1200);
    store.save_work_interval(25);
    store.save_break_duration(60);
    store.save_pause(1234567890.5);
    auto loaded = store.load();
    check(loaded.schedule.work_interval == 1500);
    check(loaded.schedule.break_duration == 60);
    check(loaded.pause_until && std::abs(*loaded.pause_until - 1234567890.5) < 0.01);
    store.save_pause(std::nullopt);
    check(!store.load().pause_until);

    QString error;
    check(store.set_autostart(true, &error));
    check(store.autostart_enabled());
    QFile desktop(SettingsStore::autostart_file_path());
    check(desktop.open(QIODevice::ReadOnly));
    auto text = desktop.readAll();
    check(text.contains("X-BlinkRest-Autostart=true"));
    check(text.contains("Exec="));
    desktop.close();
    check(store.set_autostart(false, &error));
    check(!store.autostart_enabled());
    return 0;
}
