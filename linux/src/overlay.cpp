#include "overlay.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QEvent>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QKeySequence>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <cmath>

namespace blinkrest::linux_ui {
namespace {
constexpr const char* background = "#0E1013";
constexpr const char* foreground = "#F5F7FA";
constexpr const char* secondary = "#A6AFBC";
constexpr const char* accent = "#77A9FF";
}

BreakOverlay::BreakOverlay(QScreen* screen, std::function<void()> skip_callback)
    : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      screen_(screen),
      skip_callback_(std::move(skip_callback)) {
    setObjectName("breakOverlay");
    setWindowTitle("Blink Rest — Eye break");
    setStyleSheet(QString(
        "QWidget#breakOverlay { background: %1; }"
        "QLabel { color: %2; background: transparent; }"
        "QPushButton { color: %2; border: 1px solid #4B5563;"
        " background: #1B2129; border-radius: 12px; padding: 14px 28px; }"
        "QPushButton:focus { border: 2px solid %3; }"
        "QProgressBar { background: #242B35; border: none; border-radius: 4px; }"
        "QProgressBar::chunk { background: %3; border-radius: 4px; }")
        .arg(background, foreground, accent));
    setFocusPolicy(Qt::StrongFocus);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(40, 40, 40, 40);
    layout->setSpacing(16);
    layout->addStretch(2);

    auto* brand = new QLabel("BLINK REST", this);
    brand->setAlignment(Qt::AlignHCenter);
    brand->setStyleSheet(QString("font-size: 15px; font-weight: 700; color: %1; letter-spacing: 3px;").arg(accent));
    layout->addWidget(brand);

    stage_ = new QLabel(this);
    stage_->setObjectName("breakStage");
    stage_->setAlignment(Qt::AlignHCenter);
    stage_->setWordWrap(true);
    stage_->setStyleSheet("font-size: 52px; font-weight: 600;");
    layout->addSpacing(20);
    layout->addWidget(stage_);

    detail_ = new QLabel(this);
    detail_->setAlignment(Qt::AlignHCenter);
    detail_->setWordWrap(true);
    detail_->setStyleSheet(QString("font-size: 20px; color: %1;").arg(secondary));
    layout->addWidget(detail_);

    remaining_ = new QLabel(this);
    remaining_->setAlignment(Qt::AlignHCenter);
    remaining_->setStyleSheet("font-size: 22px; font-weight: 500;");
    layout->addSpacing(20);
    layout->addWidget(remaining_);

    auto* bar_wrapper = new QWidget(this);
    bar_wrapper->setMaximumWidth(360);
    auto* bar_layout = new QVBoxLayout(bar_wrapper);
    bar_layout->setContentsMargins(0, 0, 0, 0);
    progress_ = new QProgressBar(bar_wrapper);
    progress_->setRange(0, 1000);
    progress_->setValue(0);
    progress_->setTextVisible(false);
    progress_->setFixedHeight(8);
    bar_layout->addWidget(progress_);
    layout->addWidget(bar_wrapper, 0, Qt::AlignHCenter);
    layout->addStretch(2);

    skip_ = new QPushButton("Hold to skip · 1.5 seconds", this);
    skip_->setAccessibleName("Hold to skip the eye break");
    skip_->setAccessibleDescription("Hold for 1.5 seconds. You can also hold Escape.");
    skip_->setFocusPolicy(Qt::StrongFocus);
    layout->addWidget(skip_, 0, Qt::AlignHCenter);

    auto* hint = new QLabel("Hold Escape to skip. For immediate accessibility skip: Ctrl+Shift+S.\nThis screen clears automatically.", this);
    hint->setAlignment(Qt::AlignHCenter);
    hint->setWordWrap(true);
    hint->setStyleSheet(QString("font-size: 13px; color: %1;").arg(secondary));
    layout->addWidget(hint);
    layout->addSpacing(10);

    hold_timer_.setSingleShot(true);
    hold_timer_.setInterval(1500);
    QObject::connect(&hold_timer_, &QTimer::timeout, this, [this] {
        if (key_held_ || button_held_) {
            cancel_hold();
            skip_callback_();
        }
    });
    QObject::connect(skip_, &QPushButton::pressed, this, [this] {
        button_held_ = true;
        begin_hold();
    });
    QObject::connect(skip_, &QPushButton::released, this, [this] {
        button_held_ = false;
        end_hold();
    });
    skip_->installEventFilter(this);
    auto* accessible_skip = new QShortcut(QKeySequence("Ctrl+Shift+S"), this);
    QObject::connect(accessible_skip, &QShortcut::activated, this, [this] {
        cancel_hold();
        skip_callback_();
    });
}

void BreakOverlay::position_on_screen() {
    if (!screen_) return;
    create();
    if (windowHandle()) windowHandle()->setScreen(screen_);
    setGeometry(screen_->geometry());
    showFullScreen();
    raise();
}

void BreakOverlay::display(const BreakSession& session, MonotonicInstant now) {
    session_ = session;
    refresh(now);
    position_on_screen();
}

void BreakOverlay::refresh(MonotonicInstant now) {
    const auto phase = session_.phase(now);
    switch (phase.stage) {
    case BreakStage::look_far:
        stage_->setText("Look into the distance");
        detail_->setText("Focus on a real object far away. Let your eyes relax.");
        break;
    case BreakStage::blink:
        stage_->setText("Blink gently");
        detail_->setText("Blink slowly and completely. Release any tension.");
        break;
    case BreakStage::close_eyes:
        stage_->setText("Close your eyes");
        detail_->setText("Rest. The screen will clear automatically.");
        break;
    }
    remaining_->setText(QString("%1 seconds remaining").arg(
        static_cast<int>(std::ceil(session_.remaining(now)))));
    progress_->setValue(static_cast<int>(std::round(session_.progress(now) * 1000.0)));
}

void BreakOverlay::begin_hold() {
    if (!hold_timer_.isActive()) hold_timer_.start();
}
void BreakOverlay::end_hold() {
    if (!key_held_ && !button_held_) hold_timer_.stop();
}
void BreakOverlay::cancel_hold() {
    key_held_ = false;
    button_held_ = false;
    hold_timer_.stop();
}
void BreakOverlay::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        if (!event->isAutoRepeat() && !key_held_) {
            key_held_ = true;
            begin_hold();
        }
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}
void BreakOverlay::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        if (!event->isAutoRepeat()) {
            key_held_ = false;
            end_hold();
        }
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}
bool BreakOverlay::eventFilter(QObject* watched, QEvent* event) {
    if (watched == skip_ &&
        (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            if (!key->isAutoRepeat()) {
                key_held_ = event->type() == QEvent::KeyPress;
                if (key_held_) begin_hold(); else end_hold();
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
void BreakOverlay::focusOutEvent(QFocusEvent* event) {
    cancel_hold();
    QWidget::focusOutEvent(event);
}
void BreakOverlay::closeEvent(QCloseEvent* event) {
    cancel_hold();
    QWidget::closeEvent(event);
}

WarningToast::WarningToast()
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint |
                       Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus) {
    setAttribute(Qt::WA_ShowWithoutActivating);
    setStyleSheet("background: #242B35; border-radius: 14px;");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 18, 24, 18);
    label_ = new QLabel(this);
    label_->setStyleSheet("color: #F5F7FA; font-size: 17px; background: transparent;");
    layout->addWidget(label_);
}
void WarningToast::display_seconds(int seconds) {
    label_->setText(QString("Eye break in %1 s").arg(seconds));
    adjustSize();
    auto* screen = QGuiApplication::primaryScreen();
    if (screen) {
        const auto area = screen->availableGeometry();
        move(area.center().x() - width() / 2, area.top() + 36);
    }
    show();
    raise();
}

OverlayManager::OverlayManager(std::function<void()> skip_callback)
    : skip_callback_(std::move(skip_callback)) {}

OverlayManager::~OverlayManager() { dismiss(); }

void OverlayManager::present(const BreakSession& session, MonotonicInstant now) {
    session_ = session;
    reconcile(now);
}
void OverlayManager::reconcile(MonotonicInstant now) {
    if (!session_) return;
    const auto screens = QGuiApplication::screens();
    for (auto it = overlays_.begin(); it != overlays_.end();) {
        if (!screens.contains(it.key())) {
            delete it.value();
            it = overlays_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto* screen : screens) {
        if (!overlays_.contains(screen)) {
            auto* overlay = new BreakOverlay(screen, skip_callback_);
            overlays_.insert(screen, overlay);
            overlay->display(*session_, now);
        } else {
            auto* overlay = overlays_.value(screen);
            overlay->refresh(now);
            overlay->position_on_screen();
        }
    }
    if (!screens.isEmpty()) {
        if (auto* primary = overlays_.value(QGuiApplication::primaryScreen(), nullptr))
            primary->activateWindow();
    }
}
void OverlayManager::update(MonotonicInstant now) {
    for (auto* overlay : overlays_) overlay->refresh(now);
}
void OverlayManager::cancel_holds() {
    for (auto* overlay : overlays_) overlay->cancel_hold();
}
void OverlayManager::dismiss() {
    session_.reset();
    for (auto* overlay : overlays_) delete overlay;
    overlays_.clear();
}

} // namespace blinkrest::linux_ui
