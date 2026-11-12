#include "host.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDBusConnection>
#include <QDir>
#include <QEvent>
#include <QDesktopServices>
#include <QUrl>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
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
    tray_.setIcon(QIcon::fromTheme("view-visible", eye_icon()));
    tray_.setToolTip("Blink Rest");
    initialize_menu();
    tray_.setContextMenu(&menu_);
    tray_.show();
    initialize_control_window();
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

    show_window_ = menu_.addAction("Show status and settings");
    connect(show_window_, &QAction::triggered, this, [this] {
        control_window_->show();
        control_window_->raise();
        control_window_->activateWindow();
    });
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
    control_window_->setWindowTitle("Blink Rest");
    control_window_->setWindowIcon(eye_icon());
    control_window_->setMinimumWidth(360);
    control_window_->setAttribute(Qt::WA_DeleteOnClose, false);
    control_window_->installEventFilter(this);

    auto* layout = new QVBoxLayout(control_window_);
    layout->setContentsMargins(28, 28, 28, 28);
    layout->setSpacing(14);
    auto* heading = new QLabel("Blink Rest", control_window_);
    QFont title = heading->font();
    title.setPointSize(20);
    title.setBold(true);
    heading->setFont(title);
    layout->addWidget(heading);

    status_label_ = new QLabel(control_window_);
    status_label_->setWordWrap(true);
    layout->addWidget(status_label_);

    control_break_ = new QPushButton("Take a break now", control_window_);
    connect(control_break_, &QPushButton::clicked, this, [this] {
        dispatch(make_event(EventKind::start_break_now));
    });
    layout->addWidget(control_break_);

    auto* pause = new QPushButton("Pause for 30 minutes", control_window_);
    connect(pause, &QPushButton::clicked, this, [this] {
        dispatch(SessionEvent::pause(wall_now() + 1800));
    });
    layout->addWidget(pause);

    control_resume_ = new QPushButton("Resume reminders", control_window_);
    connect(control_resume_, &QPushButton::clicked, this, [this] {
        dispatch(make_event(EventKind::resume));
    });
    layout->addWidget(control_resume_);

    auto* work_row = new QHBoxLayout();
    work_row->addWidget(new QLabel("Work interval", control_window_));
    control_work_ = new QComboBox(control_window_);
    for (int minutes : {20, 25, 30, 40})
        control_work_->addItem(QString("%1 minutes").arg(minutes), minutes);
    connect(control_work_, &QComboBox::currentIndexChanged, this, [this](int index) {
        set_work_minutes(control_work_->itemData(index).toInt());
    });
    work_row->addWidget(control_work_);
    layout->addLayout(work_row);

    auto* rest_row = new QHBoxLayout();
    rest_row->addWidget(new QLabel("Break duration", control_window_));
    control_duration_ = new QComboBox(control_window_);
    for (int seconds : {30, 45, 60, 90})
        control_duration_->addItem(QString("%1 seconds").arg(seconds), seconds);
    connect(control_duration_, &QComboBox::currentIndexChanged, this, [this](int index) {
        set_break_seconds(control_duration_->itemData(index).toInt());
    });
    rest_row->addWidget(control_duration_);
    layout->addLayout(rest_row);

    control_autostart_ = new QCheckBox("Launch at login", control_window_);
    connect(control_autostart_, &QCheckBox::toggled, this, [this](bool enabled) {
        toggle_autostart(enabled);
    });
    layout->addWidget(control_autostart_);

    auto* exit = new QPushButton("Quit", control_window_);
    connect(exit, &QPushButton::clicked, qApp, &QCoreApplication::quit);
    layout->addWidget(exit);
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

bool AppHost::eventFilter(QObject* watched, QEvent* observed) {
    if (watched == control_window_ && observed->type() == QEvent::Close &&
        !QSystemTrayIcon::isSystemTrayAvailable()) {
        QCoreApplication::quit();
    }
    return QObject::eventFilter(watched, observed);
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
    if (!has_tray && !control_window_->isVisible()) {
        control_window_->show();
        shown_as_fallback_ = true;
    } else if (has_tray && shown_as_fallback_) {
        control_window_->hide();
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
    const auto message = status_text();
    tray_.setToolTip("Blink Rest · " + message);
    if (status_label_) status_label_->setText(message);
    if (break_now_) break_now_->setEnabled(!paused && !breaking && !suspended);
    if (resume_) resume_->setEnabled(paused);
    if (control_break_) control_break_->setEnabled(!paused && !breaking && !suspended);
    if (control_resume_) control_resume_->setEnabled(paused);
    if (work_group_) for (auto* action : work_group_->actions()) {
        action->setChecked(action->data().toInt() * 60 == schedule_.work_interval);
    }
    if (break_group_) for (auto* action : break_group_->actions()) {
        action->setChecked(action->data().toInt() == schedule_.break_duration);
    }
    if (control_work_) {
        QSignalBlocker block(control_work_);
        control_work_->setCurrentIndex(control_work_->findData(static_cast<int>(schedule_.work_interval / 60)));
    }
    if (control_duration_) {
        QSignalBlocker block(control_duration_);
        control_duration_->setCurrentIndex(control_duration_->findData(static_cast<int>(schedule_.break_duration)));
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
