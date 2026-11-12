#include "host.hpp"

#include <blinkrest/core.hpp>

#include <QApplication>
#include <QDir>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTimer>

#include <optional>
#include <variant>

namespace {
blinkrest::SessionEvent event(blinkrest::EventKind kind) {
    return {kind, std::nullopt};
}
}

int main(int argc, char* argv[]) {
    const bool smoke = argc > 1 && QString::fromLocal8Bit(argv[1]) == "--smoke";
    if (smoke) QStandardPaths::setTestModeEnabled(true);

    QApplication app(argc, argv);
    app.setApplicationName("Blink Rest");
    app.setOrganizationName("BlinkRest");
    app.setQuitOnLastWindowClosed(false);

    // Do not let two processes independently schedule the same break.
    // The lock is deliberately per-user, not shared system-wide.
    const auto config_dir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                            + "/BlinkRest";
    QDir().mkpath(config_dir);
    QLockFile instance_lock(config_dir + "/instance.lock");
    instance_lock.setStaleLockTime(0);
    if (!smoke && !instance_lock.tryLock(0)) {
        QMessageBox::information(nullptr, "Blink Rest",
                                 "Blink Rest is already running in this user session.");
        return 0;
    }

    blinkrest::linux_ui::AppHost host(smoke);
    host.initialize();

    if (smoke) {
        host.dispatch(event(blinkrest::EventKind::start_break_now));
        if (!std::holds_alternative<blinkrest::Breaking>(host.state()) || host.overlay_count() == 0)
            return 2;
        host.dispatch(event(blinkrest::EventKind::displays_changed));
        if (host.overlay_count() != QApplication::screens().size())
            return 3;
        host.on_prepare_for_sleep(true);
        host.on_screen_saver_active(true);
        if (!std::holds_alternative<blinkrest::Suspended>(host.state()) || host.overlay_count() != 0)
            return 4;
        host.on_prepare_for_sleep(false);
        const auto* partly_suspended = std::get_if<blinkrest::Suspended>(&host.state());
        if (!partly_suspended || !blinkrest::has_reason(
                partly_suspended->reasons, blinkrest::SuspensionReason::inactive_session))
            return 5;
        host.on_screen_saver_active(false);
        QTimer::singleShot(650, &app, [&host] {
            if (!std::holds_alternative<blinkrest::Running>(host.state())) {
                QCoreApplication::exit(6);
                return;
            }
            host.dispatch(event(blinkrest::EventKind::start_break_now));
            if (!std::holds_alternative<blinkrest::Breaking>(host.state())) {
                QCoreApplication::exit(7);
                return;
            }
            host.dispatch(event(blinkrest::EventKind::escape_hold_completed));
            if (!std::holds_alternative<blinkrest::Running>(host.state()) ||
                host.overlay_count() != 0) {
                QCoreApplication::exit(8);
                return;
            }
            QCoreApplication::quit();
        });
    }
    return app.exec();
}
