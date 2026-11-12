#include "../src/tray_popover.hpp"
#include "../src/overlay.hpp"

#include <QApplication>
#include <QImage>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QMenu>
#include <QScreen>
#include <QStandardPaths>

#include <cstdlib>

using namespace blinkrest;
using namespace blinkrest::linux_ui;

void check(bool condition) {
    if (!condition) std::abort();
}

int main(int argc, char* argv[]) {
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    int started = 0;
    int paused = 0;
    int resumed = 0;
    TrayPopover popover(
        [&] { ++started; },
        [&](int duration) { paused = duration; },
        [&] { ++resumed; },
        [] {},
        [] {});
    check(popover.width() == 304);
    check(popover.height() == 360);
    auto* countdown_widget = popover.findChild<QWidget*>("countdownRing");
    check(countdown_widget != nullptr);

    popover.update_presentation({PopoverPhase::running, 1195, 0.2});
    check(popover.presentation().phase == PopoverPhase::running);
    check(popover.findChild<QLabel*>("popoverStatus")->text() == "Active");
    auto* primary = popover.findChild<QPushButton*>("primaryAction");
    check(primary != nullptr && primary->isEnabled());
    check(primary->text() == "Take a break now");
    primary->click();
    check(started == 1);

    auto* pause_button = popover.findChild<QToolButton*>("pauseAction");
    check(pause_button != nullptr && pause_button->menu());
    check(pause_button->menu()->actions().size() == 2);
    pause_button->menu()->actions().first()->trigger();
    check(paused == 1800);

    popover.update_presentation({PopoverPhase::paused, 120, 0});
    check(popover.findChild<QLabel*>("popoverStatus")->text() == "Paused");
    check(primary->text() == "Resume");
    primary->click();
    check(resumed == 1);

    popover.update_presentation({PopoverPhase::breaking, 15, 0.5});
    check(!primary->isEnabled());
    popover.update_presentation({PopoverPhase::warning, 4, 0.99});
    check(popover.findChild<QLabel*>("popoverStatus")->text() == "Break soon");

    // Render the popover so a broken paint/layout path fails in CI, even
    // without a system tray. Qt's offscreen platform is used by CTest.
    popover.show_standalone();
    app.processEvents();
    // Catch ring/button overlap even if rendering itself succeeds.
    check(countdown_widget->geometry().bottom() <
          primary->geometry().top());
    QImage image = popover.grab().toImage();
    check(!image.isNull() && image.width() == 304 && image.height() == 360);
    if (qEnvironmentVariableIsSet("BLINKREST_SNAPSHOT_DIR")) {
        const QString directory = qEnvironmentVariable("BLINKREST_SNAPSHOT_DIR");
        QDir().mkpath(directory);
        check(image.save(directory + "/popover.png"));
    }
    popover.hide();

    auto* screen = QGuiApplication::primaryScreen();
    check(screen != nullptr);
    int skipped = 0;
    BreakOverlay overlay(screen, [&] { ++skipped; });
    const BreakSession session{100, 130, 30};
    overlay.display(session, 100);
    app.processEvents();
    check(overlay.findChild<QWidget*>("eyeProgressRing") != nullptr);
    check(overlay.findChild<QPushButton*>("holdToSkip") != nullptr);
    check(overlay.findChild<QLabel*>("breakStage")->text()
          == "Look at a real distant object");
    auto* headline = overlay.findChild<QLabel*>("breakStage");
    check(headline->width() >= 560 || screen->geometry().width() < 640);
    check(headline->height() >= 60);
    overlay.refresh(105);
    check(overlay.findChild<QLabel*>("breakStage")->text()
          == "Blink slowly and fully");
    if (qEnvironmentVariableIsSet("BLINKREST_SNAPSHOT_DIR")) {
        const QString directory = qEnvironmentVariable("BLINKREST_SNAPSHOT_DIR");
        check(overlay.grab().save(directory + "/overlay.png"));
    }
    overlay.refresh(120);
    check(overlay.findChild<QLabel*>("breakStage")->text()
          == "Close your eyes");
    check(skipped == 0);
    overlay.hide();
    return 0;
}
