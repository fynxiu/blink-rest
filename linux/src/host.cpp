#include "host.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QButtonGroup>
#include <QCursor>
#include <QDBusConnection>
#include <QDir>
#include <QEvent>
#include <QDesktopServices>
#include <QUrl>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <type_traits>
#include <variant>

namespace blinkrest::linux_ui {
namespace {
double monotonic_now() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
double wall_now() {
    return std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
QString format_remaining(double seconds) {
    const auto whole = std::max(0, static_cast<int>(std::ceil(seconds)));
    if (whole >= 3600) return QString("%1 h %2 min").arg(whole / 3600).arg((whole % 3600) / 60);
    if (whole >= 60) return QString("%1 min %2 s").arg(whole / 60).arg(whole % 60);
    return QString("%1 s").arg(whole);
}
SessionEvent make_event(EventKind kind) { return {kind, std::nullopt}; }
}

AppHost::AppHost(bool smoke_mode)
    : smoke_mode_(smoke_mode),
      overlays_([this] { dispatch(make_event(EventKind::escape_hold_completed)); }) {
    auto loaded = settings_.load();
    schedule_ = loaded.schedule;
    pause_until_ = loaded.pause_until;
    resume_timer_.setSingleShot(true);
    connect(&resume_timer_, &QTimer::timeout, this, [this] {
        dispatch(make_event(EventKind::suspension_resume_debounce_elapsed));
    });
    tick_timer_.setTimerType(Qt::CoarseTimer);
    tick_timer_.setInterval(250);
    connect(&tick_timer_, &QTimer::timeout, this, [this] {
        dispatch(make_event(EventKind::tick));
    });
}

AppHost::~AppHost() { delete control_window_; }

void AppHost::initialize() {
    if (!smoke_mode_) {
        initialize_ui();
        initialize_lifecycle();
    }
    connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen* screen) {
        connect(screen, &QScreen::geometryChanged, this, [this] { screens_changed(); });
        screens_changed();
    });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this](QScreen*) { screens_changed(); });
    connect(qApp, &QGuiApplication::primaryScreenChanged, this, [this](QScreen*) {
        screens_changed();
    });
    for (auto* screen : QGuiApplication::screens()) {
        connect(screen, &QScreen::geometryChanged, this, [this] { screens_changed(); });
    }
    dispatch(make_event(EventKind::launch));
    tick_timer_.start();
    if (!smoke_mode_) {
        tray_check_timer_.setInterval(1500);
        connect(&tray_check_timer_, &QTimer::timeout, this, [this] { check_tray_availability(); });
        tray_check_timer_.start();
        check_tray_availability();
    }
}

SessionContext AppHost::context() const {
    return {monotonic_now(), wall_now(), schedule_, pause_until_};
}

void AppHost::dispatch(SessionEvent requested) {
    auto next = reduce_session(state_, requested, context());
    state_ = std::move(next.state);
    for (const auto& effect : next.effects) apply_effect(effect);
    const bool rapid = std::holds_alternative<Breaking>(state_) ||
                       std::holds_alternative<Warning>(state_);
    const int interval = rapid ? 250 : 1000;
    if (tick_timer_.interval() != interval) tick_timer_.setInterval(interval);
    if (!smoke_mode_) update_ui();
}

void AppHost::apply_effect(const SessionEffect& effect) {
    std::visit([this](const auto& e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, ShowWarning>) {
            if (!smoke_mode_) warning_.display_seconds(e.seconds_remaining);
        } else if constexpr (std::is_same_v<T, HideWarning>) {
            warning_.hide();
        } else if constexpr (std::is_same_v<T, PresentOverlay>) {
            overlays_.present(e.session, monotonic_now());
        } else if constexpr (std::is_same_v<T, UpdateOverlay>) {
            overlays_.update(e.at);
        } else if constexpr (std::is_same_v<T, DismissOverlay> ||
                             std::is_same_v<T, DiscardCachedOverlayWindowsAfterWake>) {
            overlays_.dismiss();
        } else if constexpr (std::is_same_v<T, CancelEscapeHold>) {
            overlays_.cancel_holds();
        } else if constexpr (std::is_same_v<T, ReconcileOverlays>) {
            overlays_.reconcile(monotonic_now());
        } else if constexpr (std::is_same_v<T, PersistPauseUntil>) {
            pause_until_ = e.value;
            settings_.save_pause(e.value);
        } else if constexpr (std::is_same_v<T, ScheduleSuspensionResumeDebounce>) {
            resume_timer_.start(static_cast<int>(e.delay * 1000));
        } else if constexpr (std::is_same_v<T, CancelSuspensionResumeDebounce>) {
            resume_timer_.stop();
        }
        // Capturing/restoring another app's foreground window is intentionally
        // unsupported on Linux without privileged desktop/window inspection.
    }, effect);
}

void AppHost::set_work_minutes(int minutes) {
    if (minutes != 20 && minutes != 25 && minutes != 30 && minutes != 40) return;
    if (schedule_.work_interval == minutes * 60) return;
    schedule_.work_interval = minutes * 60;
    settings_.save_work_interval(minutes);
    dispatch(make_event(EventKind::work_interval_changed));
}
void AppHost::set_break_seconds(int seconds) {
    if (seconds != 30 && seconds != 45 && seconds != 60 && seconds != 90) return;
    if (schedule_.break_duration == seconds) return;
    schedule_.break_duration = seconds;
    settings_.save_break_duration(seconds);
    dispatch(make_event(EventKind::break_duration_changed));
}
void AppHost::on_prepare_for_sleep(bool sleeping) {
    dispatch(make_event(sleeping ? EventKind::system_will_sleep : EventKind::system_did_wake));
}
void AppHost::on_screen_saver_active(bool active) {
    dispatch(make_event(active ? EventKind::session_did_resign_active :
                            EventKind::session_did_become_active));
}
void AppHost::screens_changed() {
    dispatch(make_event(EventKind::displays_changed));
    dispatch(make_event(EventKind::presentation_context_changed));
}

QIcon AppHost::eye_icon() {
    QPixmap image(64, 64);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor color(95, 160, 225);
    painter.setPen(QPen(color, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPainterPath outline;
    outline.moveTo(5, 32);
    outline.cubicTo(18, 12, 46, 12, 59, 32);
    outline.cubicTo(46, 52, 18, 52, 5, 32);
    painter.drawPath(outline);
    painter.setBrush(color);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(32, 32), 8, 8);
    return QIcon(image);
}

void AppHost::initialize_ui() {
    popover_ = std::make_unique<TrayPopover>(
        [this] { dispatch(make_event(EventKind::start_break_now)); },
        [this](int duration) { dispatch(SessionEvent::pause(wall_now() + duration)); },
        [this] { dispatch(make_event(EventKind::resume)); },
        [this] { show_settings(); },
        [] { QApplication::quit(); });
    popover_->setWindowIcon(eye_icon());
    tray_.setIcon(eye_icon());
    tray_.setToolTip("Blink Rest");
    initialize_menu();
    tray_.setContextMenu(&menu_);
    connect(&tray_, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
        // Normal click opens the same countdown popover as macOS and Windows.
        // Right-click continues to expose quick settings through QMenu.
        if (reason == QSystemTrayIcon::Trigger && popover_) {
            update_ui();
            popover_->show_at(QCursor::pos());
        }
    });
    tray_.show();
    initialize_control_window();
}

void AppHost::show_settings() {
    if (!control_window_) return;
    control_window_->show();
    control_window_->raise();
    control_window_->activateWindow();
}

void AppHost::initialize_menu() {
    break_now_ = menu_.addAction("Take a break now");
    connect(break_now_, &QAction::triggered, this, [this] {
        dispatch(make_event(EventKind::start_break_now));
    });
    auto* pause30 = menu_.addAction("Pause for 30 minutes");
    auto* pause60 = menu_.addAction("Pause for 1 hour");
    connect(pause30, &QAction::triggered, this, [this] {
        dispatch(SessionEvent::pause(wall_now() + 1800));
    });
    connect(pause60, &QAction::triggered, this, [this] {
        dispatch(SessionEvent::pause(wall_now() + 3600));
    });
    resume_ = menu_.addAction("Resume reminders");
    connect(resume_, &QAction::triggered, this, [this] {
        dispatch(make_event(EventKind::resume));
    });

    auto* work_menu = menu_.addMenu("Work interval");
    work_group_ = new QActionGroup(this);
    work_group_->setExclusive(true);
    for (int minutes : {20, 25, 30, 40}) {
        auto* action = work_menu->addAction(QString("%1 minutes").arg(minutes));
        action->setCheckable(true);
        action->setData(minutes);
        work_group_->addAction(action);
        connect(action, &QAction::triggered, this, [this, minutes] { set_work_minutes(minutes); });
    }

    auto* break_menu = menu_.addMenu("Break duration");
    break_group_ = new QActionGroup(this);
    break_group_->setExclusive(true);
    for (int seconds : {30, 45, 60, 90}) {
        auto* action = break_menu->addAction(QString("%1 seconds").arg(seconds));
        action->setCheckable(true);
        action->setData(seconds);
        break_group_->addAction(action);
        connect(action, &QAction::triggered, this, [this, seconds] { set_break_seconds(seconds); });
    }

    menu_.addSeparator();
    autostart_ = menu_.addAction("Launch at login");
    autostart_->setCheckable(true);
    connect(autostart_, &QAction::triggered, this, [this](bool enabled) {
        toggle_autostart(enabled);
    });

    show_window_ = menu_.addAction("Settings");
    connect(show_window_, &QAction::triggered, this, [this] { show_settings(); });
    auto* releases = menu_.addAction("View latest releases");
    connect(releases, &QAction::triggered, this, [] {
        QDesktopServices::openUrl(QUrl("https://github.com/fynxiu/blink-rest/releases/latest"));
    });
    menu_.addSeparator();
    auto* quit = menu_.addAction("Quit Blink Rest");
    connect(quit, &QAction::triggered, qApp, &QCoreApplication::quit);
    connect(&menu_, &QMenu::aboutToShow, this, [this] { update_ui(); });
}

void AppHost::initialize_control_window() {
    control_window_ = new QWidget(nullptr, Qt::Window);
    control_window_->setObjectName("settingsWindow");
    control_window_->setWindowTitle("Blink Rest — Settings");
    control_window_->setWindowIcon(eye_icon());
    control_window_->setMinimumWidth(480);
    control_window_->resize(500, 415);
    control_window_->setAttribute(Qt::WA_DeleteOnClose, false);
    control_window_->setStyleSheet(
        "QWidget#settingsWindow { background: #F5F5F7; }"
        "QGroupBox { border: 1px solid #E0E0E5; border-radius: 12px;"
        " background: white; margin-top: 14px; padding-top: 12px;"
        " font-weight: 600; color: #333333; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 12px;"
        " padding: 0 5px; background: #F5F5F7; }"
        "QPushButton#segmentedChoice { background: #F1F1F3; color: #333333;"
        " border: 1px solid #D9DAE0; padding: 8px; min-height: 18px; }"
        "QPushButton#segmentedChoice:checked { background: #0078D7;"
        " color: white; border-color: #0078D7; font-weight: 600; }"
        "QPushButton#segmentedChoice:focus { border: 2px solid #77A9FF; }"
        "QCheckBox { color: #292929; }");

    auto* root = new QVBoxLayout(control_window_);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(12);

    auto* header = new QLabel("Blink Rest", control_window_);
    QFont heading_font = header->font();
    heading_font.setPixelSize(23);
    heading_font.setWeight(QFont::DemiBold);
    header->setFont(heading_font);
    root->addWidget(header);

    auto* schedule = new QGroupBox("Schedule", control_window_);
    auto* schedule_layout = new QVBoxLayout(schedule);
    schedule_layout->setContentsMargins(16, 20, 16, 16);
    schedule_layout->setSpacing(10);

    const auto make_segmented = [schedule](const std::vector<int>& values,
                                          const QString& unit, QButtonGroup*& group) {
        auto* container = new QWidget(schedule);
        auto* row = new QHBoxLayout(container);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(0);
        group = new QButtonGroup(container);
        group->setExclusive(true);
        for (int value : values) {
            auto* button = new QPushButton(QString("%1 %2").arg(value).arg(unit), container);
            button->setObjectName("segmentedChoice");
            button->setCheckable(true);
            button->setCursor(Qt::PointingHandCursor);
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            button->setAccessibleName(QString("%1 %2").arg(value).arg(unit));
            group->addButton(button, value);
            row->addWidget(button);
        }
        return container;
    };

    schedule_layout->addWidget(new QLabel("Work interval", schedule));
    schedule_layout->addWidget(make_segmented({20, 25, 30, 40}, "min", work_buttons_));
    schedule_layout->addWidget(new QLabel("Break duration", schedule));
    schedule_layout->addWidget(make_segmented({30, 45, 60, 90}, "sec", duration_buttons_));

    connect(work_buttons_, &QButtonGroup::idClicked, this, [this](int minutes) {
        set_work_minutes(minutes);
    });
    connect(duration_buttons_, &QButtonGroup::idClicked, this, [this](int seconds) {
        set_break_seconds(seconds);
    });

    auto* hint = new QLabel(
        "Work interval changes restart the current cycle. "
        "Break duration changes apply to the next break.", schedule);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #727272; font-size: 12px;");
    schedule_layout->addWidget(hint);
    root->addWidget(schedule);

    auto* updates = new QGroupBox("Updates", control_window_);
    auto* updates_layout = new QHBoxLayout(updates);
    updates_layout->setContentsMargins(16, 20, 16, 12);
    auto* version = new QLabel(QString("Version %1").arg(BLINKREST_VERSION_STRING), updates);
    version->setStyleSheet("color: #565656;");
    updates_layout->addWidget(version);
    updates_layout->addStretch(1);
    auto* latest = new QPushButton("View latest release…", updates);
    connect(latest, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl("https://github.com/fynxiu/blink-rest/releases/latest"));
    });
    updates_layout->addWidget(latest);
    root->addWidget(updates);

    auto* system = new QGroupBox("System", control_window_);
    auto* system_layout = new QVBoxLayout(system);
    system_layout->setContentsMargins(16, 20, 16, 14);
    control_autostart_ = new QCheckBox("Launch at login", system);
    connect(control_autostart_, &QCheckBox::toggled, this, [this](bool enabled) {
        toggle_autostart(enabled);
    });
    system_layout->addWidget(control_autostart_);
    root->addWidget(system);

    auto* privacy = new QLabel(
        "No analytics. No account. View latest release opens GitHub in your browser.",
        control_window_);
    privacy->setWordWrap(true);
    privacy->setStyleSheet("color: #727272; font-size: 12px;");
    root->addWidget(privacy);
    root->addStretch(1);
}

void AppHost::initialize_lifecycle() {
    // These are normal desktop signals; no input-monitoring or privileged
    // permissions are needed. On desktops without these services they simply
    // do not emit, so we also use monotonic deadlines as a safety net.
    QDBusConnection::systemBus().connect(
        "org.freedesktop.login1", "/org/freedesktop/login1",
        "org.freedesktop.login1.Manager", "PrepareForSleep",
        this, SLOT(on_prepare_for_sleep(bool)));
    QDBusConnection::sessionBus().connect(
        "org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver",
        "org.freedesktop.ScreenSaver", "ActiveChanged",
        this, SLOT(on_screen_saver_active(bool)));
    QDBusConnection::sessionBus().connect(
        "org.gnome.ScreenSaver", "/org/gnome/ScreenSaver",
        "org.gnome.ScreenSaver", "ActiveChanged",
        this, SLOT(on_screen_saver_active(bool)));
}

void AppHost::toggle_autostart(bool enabled) {
    QString error;
    if (!settings_.set_autostart(enabled, &error)) {
        if (!smoke_mode_) QMessageBox::warning(control_window_, "Blink Rest", error);
    }
    update_ui();
}

void AppHost::check_tray_availability() {
    const bool has_tray = QSystemTrayIcon::isSystemTrayAvailable();
    if (has_tray && !tray_.isVisible()) tray_.show();

    // GNOME Shell and some Wayland environments do not expose a tray host.
    // The same countdown design remains available as a standalone window.
    if (!has_tray && popover_ && !popover_->isVisible()) {
        popover_->show_standalone();
        shown_as_fallback_ = true;
    } else if (has_tray && shown_as_fallback_) {
        popover_->hide();
        shown_as_fallback_ = false;
    }
}

QString AppHost::status_text() const {
    const double mono = monotonic_now();
    const double wall = wall_now();
    if (const auto* running = std::get_if<Running>(&state_))
        return "Next eye break in " + format_remaining(running->deadline - mono);
    if (const auto* warning = std::get_if<Warning>(&state_))
        return "Eye break starts in " + format_remaining(warning->break_starts_at - mono);
    if (const auto* breaking = std::get_if<Breaking>(&state_))
        return "On a break · " + format_remaining(breaking->session.remaining(mono)) + " left";
    if (const auto* paused = std::get_if<Paused>(&state_))
        return "Paused · " + format_remaining(paused->until - wall) + " left";
    return "Reminders suspended while the session is inactive";
}

void AppHost::update_ui() {
    const bool paused = std::holds_alternative<Paused>(state_);
    const bool breaking = std::holds_alternative<Breaking>(state_);
    const bool suspended = std::holds_alternative<Suspended>(state_);
    tray_.setToolTip("Blink Rest · " + status_text());

    if (popover_) {
        PopoverPresentation presentation;
        const double mono = monotonic_now();
        const double wall = wall_now();
        const double work = std::max(1.0, schedule_.effective_work_interval());
        if (const auto* running = std::get_if<Running>(&state_)) {
            presentation.phase = PopoverPhase::running;
            presentation.remaining_seconds = static_cast<int>(
                std::ceil(std::max(0.0, running->deadline - mono)));
            presentation.progress = std::clamp(
                1.0 - (running->deadline - mono) / work, 0.0, 1.0);
        } else if (const auto* warning = std::get_if<Warning>(&state_)) {
            presentation.phase = PopoverPhase::warning;
            presentation.remaining_seconds = static_cast<int>(
                std::ceil(std::max(0.0, warning->break_starts_at - mono)));
            presentation.progress = std::clamp(
                1.0 - (warning->break_starts_at - mono) / work, 0.0, 1.0);
        } else if (const auto* state = std::get_if<Paused>(&state_)) {
            presentation.phase = PopoverPhase::paused;
            presentation.remaining_seconds = static_cast<int>(
                std::ceil(std::max(0.0, state->until - wall)));
        } else if (const auto* state = std::get_if<Breaking>(&state_)) {
            presentation.phase = PopoverPhase::breaking;
            presentation.remaining_seconds = static_cast<int>(
                std::ceil(state->session.remaining(mono)));
            presentation.progress = state->session.progress(mono);
        } else {
            presentation.phase = PopoverPhase::suspended;
        }
        popover_->update_presentation(presentation);
    }

    if (break_now_) break_now_->setEnabled(!paused && !breaking && !suspended);
    if (resume_) resume_->setEnabled(paused);

    if (work_group_) for (auto* action : work_group_->actions())
        action->setChecked(action->data().toInt() * 60 == schedule_.work_interval);
    if (break_group_) for (auto* action : break_group_->actions())
        action->setChecked(action->data().toInt() == schedule_.break_duration);

    if (work_buttons_) {
        if (auto* checked = work_buttons_->button(static_cast<int>(schedule_.work_interval / 60)))
            checked->setChecked(true);
    }
    if (duration_buttons_) {
        if (auto* checked = duration_buttons_->button(static_cast<int>(schedule_.break_duration)))
            checked->setChecked(true);
    }
    const bool autostart = settings_.autostart_enabled();
    if (autostart_) {
        QSignalBlocker block(autostart_);
        autostart_->setChecked(autostart);
    }
    if (control_autostart_) {
        QSignalBlocker block(control_autostart_);
        control_autostart_->setChecked(autostart);
    }
}

} // namespace blinkrest::linux_ui
