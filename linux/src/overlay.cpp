#include "overlay.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QEvent>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QKeySequence>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <utility>

namespace blinkrest::linux_ui {
namespace {
constexpr auto background = "#0E1013";
constexpr auto foreground = "#F5F7FA";
constexpr auto secondary = "#A6AFBC";
constexpr auto accent = "#77A9FF";
constexpr double pi = 3.14159265358979323846;

// Same three semantic symbols and geometry as macOS SF Symbols and the
// Windows Direct2D eye glyph. SVG/image assets are intentionally unnecessary.
void draw_eye(QPainter& painter, QPointF center, BreakStage stage,
              QColor ink = QColor(foreground)) {
    QPainterPath eye;
    eye.moveTo(center.x() - 32, center.y());
    eye.cubicTo(center.x() - 18, center.y() - 17,
                center.x() + 18, center.y() - 17,
                center.x() + 32, center.y());
    eye.cubicTo(center.x() + 18, center.y() + 17,
                center.x() - 18, center.y() + 17,
                center.x() - 32, center.y());
    eye.closeSubpath();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(ink, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(eye);
    if (stage == BreakStage::blink) {
        painter.drawEllipse(center, 11, 11);
        painter.setBrush(ink);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(center, 5, 5);
    } else {
        painter.setBrush(ink);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(center, 5.5, 5.5);
    }
    if (stage == BreakStage::close_eyes) {
        painter.setPen(QPen(ink, 3.0, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(center.x() - 28, center.y() - 25),
                         QPointF(center.x() + 28, center.y() + 25));
    }
}

} // namespace

class EyeProgressRing final : public QWidget {
public:
    explicit EyeProgressRing(QWidget* parent = nullptr) : QWidget(parent) {
        setObjectName("eyeProgressRing");
        setFixedSize(208, 208);
        elapsed_.start();
        setAccessibleName("Eye break progress");
    }

    void set_phase(BreakStage stage, double progress) {
        stage_ = stage;
        progress_ = std::clamp(progress, 0.0, 1.0);
        update();
    }

    BreakStage stage() const { return stage_; }
    double progress() const { return progress_; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QPointF center(width() / 2.0, height() / 2.0);
        constexpr double radius = 94;
        const QRectF circle(center.x() - radius, center.y() - radius,
                            radius * 2, radius * 2);
        if (stage_ == BreakStage::blink &&
            !qEnvironmentVariableIsSet("BLINKREST_REDUCE_MOTION")) {
            const double wave = (1 - std::cos((elapsed_.elapsed() / 1000.0) * 2 * pi / 1.8)) / 2;
            const double halo = radius * (1 + 0.035 * wave);
            painter.setPen(QPen(QColor(119, 169, 255, 41), 2));
            painter.drawEllipse(center, halo, halo);
        }
        painter.setPen(QPen(QColor(255, 255, 255, 31), 4, Qt::SolidLine, Qt::RoundCap));
        painter.drawEllipse(circle);
        painter.setPen(QPen(QColor(accent), 4, Qt::SolidLine, Qt::RoundCap));
        if (progress_ >= 1.0) painter.drawEllipse(circle);
        else if (progress_ > 0.0)
            painter.drawArc(circle, 90 * 16,
                            -static_cast<int>(std::round(progress_ * 360 * 16)));
        draw_eye(painter, center, stage_);
    }

private:
    BreakStage stage_ = BreakStage::look_far;
    double progress_ = 0;
    QElapsedTimer elapsed_;
};

class HoldProgressRing final : public QWidget {
public:
    explicit HoldProgressRing(int size, QWidget* parent = nullptr)
        : QWidget(parent) {
        setFixedSize(size, size);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void set_progress(double progress) {
        progress_ = std::clamp(progress, 0.0, 1.0);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF bounds(3, 3, width() - 6, height() - 6);
        painter.setPen(QPen(QColor(255, 255, 255, 38), 2));
        painter.drawEllipse(bounds);
        if (progress_ > 0.0) {
            painter.setPen(QPen(QColor(accent), 2.0, Qt::SolidLine, Qt::RoundCap));
            painter.drawArc(bounds, 90 * 16,
                            -static_cast<int>(std::round(progress_ * 360 * 16)));
        }
        painter.setPen(QPen(QColor(foreground), 1.5));
        const QPointF center(width() / 2.0, height() / 2.0);
        painter.drawEllipse(center, 1.5, 1.5);
    }

private:
    double progress_ = 0;
};

BreakOverlay::BreakOverlay(QScreen* screen, std::function<void()> skip_callback)
    : QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      screen_(screen),
      skip_callback_(std::move(skip_callback)) {
    setObjectName("breakOverlay");
    setWindowTitle("Blink Rest — Eye break");
    setStyleSheet(QString(
        "QWidget#breakOverlay { background: %1; }"
        "QLabel { background: transparent; }")
        .arg(background));
    setFocusPolicy(Qt::StrongFocus);

    // Center stack: macOS 188px circle, 40pt headline, 16pt secondary detail.
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(40, 40, 40, 40);
    layout->setSpacing(0);
    layout->addStretch(1);

    ring_ = new EyeProgressRing(this);
    layout->addWidget(ring_, 0, Qt::AlignHCenter);
    layout->addSpacing(20);

    stage_ = new QLabel(this);
    stage_->setObjectName("breakStage");
    stage_->setAlignment(Qt::AlignCenter);
    stage_->setWordWrap(true);
    stage_->setMaximumWidth(620);
    stage_->setMinimumHeight(64);
    stage_->setStyleSheet(QString("font-size: 40px; font-weight: 600; color: %1;").arg(foreground));
    layout->addWidget(stage_, 0, Qt::AlignHCenter);
    layout->addSpacing(8);

    detail_ = new QLabel(this);
    detail_->setObjectName("breakDetail");
    detail_->setAlignment(Qt::AlignCenter);
    detail_->setWordWrap(true);
    detail_->setMaximumWidth(620);
    detail_->setMinimumHeight(44);
    detail_->setStyleSheet(QString("font-size: 16px; color: %1;").arg(secondary));
    layout->addWidget(detail_, 0, Qt::AlignHCenter);
    layout->addStretch(1);

    // Non-modal top-right skip controls, matching Windows and macOS.
    skip_panel_ = new QWidget(this);
    skip_panel_->setObjectName("skipPanel");
    skip_panel_->setFixedSize(300, 102);
    auto* skip_layout = new QVBoxLayout(skip_panel_);
    skip_layout->setContentsMargins(0, 0, 0, 0);
    skip_layout->setSpacing(7);

    auto* hint_row = new QHBoxLayout;
    hint_row->setSpacing(8);
    auto* hint = new QLabel("Hold Esc or hold the button to skip", skip_panel_);
    hint->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    hint->setWordWrap(true);
    hint->setStyleSheet(QString("font-size: 12px; color: %1;").arg(secondary));
    hint_row->addWidget(hint, 1);
    keyboard_progress_ = new HoldProgressRing(28, skip_panel_);
    hint_row->addWidget(keyboard_progress_);
    skip_layout->addLayout(hint_row);

    skip_ = new QPushButton("Hold to skip", skip_panel_);
    skip_->setObjectName("holdToSkip");
    skip_->setFixedSize(156, 36);
    skip_->setFocusPolicy(Qt::StrongFocus);
    skip_->setCursor(Qt::PointingHandCursor);
    skip_->setAccessibleName("Hold to skip eye break");
    skip_->setAccessibleDescription(
        "Hold for 1.5 seconds. Ctrl+Shift+S immediately skips for accessibility.");
    skip_->setStyleSheet(
        "QPushButton#holdToSkip { text-align: center; padding-left: 22px;"
        " font-size: 12px; font-weight: 500; color: #F5F7FA;"
        " background: #171A20; border: 1px solid #3C3F45; border-radius: 10px; }"
        "QPushButton#holdToSkip:hover { border-color: #77A9FF; }"
        "QPushButton#holdToSkip:focus { border: 2px solid #77A9FF; }");
    auto* skip_row = new QHBoxLayout;
    skip_row->addStretch(1);
    skip_row->addWidget(skip_);
    skip_layout->addLayout(skip_row);
    pointer_progress_ = new HoldProgressRing(24, skip_);
    pointer_progress_->move(7, 6);
    pointer_progress_->raise();

    hold_timer_.setSingleShot(true);
    hold_timer_.setInterval(1500);
    connect(&hold_timer_, &QTimer::timeout, this, [this] {
        if (key_held_ || button_held_) {
            cancel_hold();
            skip_callback_();
        }
    });
    animation_timer_.setInterval(33);
    animation_timer_.setTimerType(Qt::PreciseTimer);
    connect(&animation_timer_, &QTimer::timeout, this, [this] {
        if (ring_->stage() == BreakStage::blink) ring_->update();
        if (hold_timer_.isActive()) {
            const double held = std::clamp(hold_clock_.elapsed() / 1500.0, 0.0, 1.0);
            keyboard_progress_->set_progress(key_held_ ? held : 0.0);
            pointer_progress_->set_progress(button_held_ ? held : 0.0);
        }
    });
    connect(skip_, &QPushButton::pressed, this, [this] {
        button_held_ = true;
        begin_hold();
    });
    connect(skip_, &QPushButton::released, this, [this] {
        button_held_ = false;
        end_hold();
    });
    skip_->installEventFilter(this);
    auto* accessible_skip = new QShortcut(QKeySequence("Ctrl+Shift+S"), this);
    connect(accessible_skip, &QShortcut::activated, this, [this] {
        cancel_hold();
        skip_callback_();
    });
}

void BreakOverlay::resizeEvent(QResizeEvent* event) {
    const int text_width = std::min(620, std::max(180, width() - 80));
    stage_->setFixedWidth(text_width);
    detail_->setFixedWidth(text_width);
    QWidget::resizeEvent(event);
    skip_panel_->move(std::max(0, width() - skip_panel_->width() - 30), 30);
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
    animation_timer_.start();
}

void BreakOverlay::refresh(MonotonicInstant now) {
    const auto phase = session_.phase(now);
    switch (phase.stage) {
    case BreakStage::look_far:
        stage_->setText("Look at a real distant object");
        detail_->setText("Let your focus settle away from the screen.");
        break;
    case BreakStage::blink:
        stage_->setText("Blink slowly and fully");
        detail_->setText("5 slow blinks");
        break;
    case BreakStage::close_eyes:
        stage_->setText("Close your eyes");
        detail_->setText("The screen will clear when the break is over.");
        break;
    }
    ring_->set_phase(phase.stage, session_.progress(now));
    stage_->setAccessibleDescription(
        QString("%1 seconds remain").arg(static_cast<int>(std::ceil(session_.remaining(now)))));
}

void BreakOverlay::begin_hold() {
    if (!hold_timer_.isActive()) {
        hold_clock_.start();
        hold_timer_.start();
    }
}

void BreakOverlay::end_hold() {
    if (!key_held_ && !button_held_) {
        hold_timer_.stop();
        keyboard_progress_->set_progress(0);
        pointer_progress_->set_progress(0);
    }
}

void BreakOverlay::cancel_hold() {
    key_held_ = false;
    button_held_ = false;
    end_hold();
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
                if (key_held_) begin_hold();
                else end_hold();
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
    animation_timer_.stop();
    QWidget::closeEvent(event);
}

WarningToast::WarningToast()
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint |
                       Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus) {
    setAttribute(Qt::WA_ShowWithoutActivating);
    setObjectName("warningPill");
    setStyleSheet(
        "QWidget#warningPill { background: #F1F2F4; border: 1px solid #C9CDD3;"
        " border-radius: 16px; }");
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(8);
    QPixmap icon(24, 24);
    icon.fill(Qt::transparent);
    {
        QPainter painter(&icon);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.translate(12, 12);
        painter.scale(0.3, 0.3);
        draw_eye(painter, QPointF(0, 0), BreakStage::blink, QColor("#696969"));
    }
    auto* eye = new QLabel(this);
    eye->setPixmap(icon);
    eye->setStyleSheet("background: transparent;");
    layout->addWidget(eye);
    label_ = new QLabel(this);
    label_->setStyleSheet("color: #1F1F1F; font-size: 14px; font-weight: 500; background: transparent;");
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
        if (auto* primary = overlays_.value(QGuiApplication::primaryScreen(), nullptr)) {
            primary->activateWindow();
            primary->setFocus(Qt::ActiveWindowFocusReason);
        }
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
