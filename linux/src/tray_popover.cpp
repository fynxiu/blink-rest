#include "tray_popover.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QScreen>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

namespace blinkrest::linux_ui {
namespace {

constexpr auto foreground = "#1F1F1F";
constexpr auto muted = "#696969";
constexpr auto accent = "#0078D7";

const char* caption(PopoverPhase phase) {
    switch (phase) {
    case PopoverPhase::running: return "Until next break";
    case PopoverPhase::warning: return "Break in";
    case PopoverPhase::paused: return "Until resume";
    case PopoverPhase::breaking: return "Break remaining";
    case PopoverPhase::suspended: return "Session inactive";
    }
    return "Until next break";
}

const char* status(PopoverPhase phase) {
    switch (phase) {
    case PopoverPhase::running: return "Active";
    case PopoverPhase::warning: return "Break soon";
    case PopoverPhase::paused: return "Paused";
    case PopoverPhase::breaking: return "Breaking";
    case PopoverPhase::suspended: return "Suspended";
    }
    return "Active";
}

QColor status_color(PopoverPhase phase) {
    switch (phase) {
    case PopoverPhase::running: return QColor("#107C41");
    case PopoverPhase::warning: return QColor("#C45F00");
    case PopoverPhase::breaking: return QColor(accent);
    case PopoverPhase::paused:
    case PopoverPhase::suspended: return QColor(muted);
    }
    return QColor(muted);
}

QString countdown(int seconds) {
    const int remaining = std::max(0, seconds);
    return QString("%1:%2").arg(remaining / 60, 2, 10, QLatin1Char('0'))
                            .arg(remaining % 60, 2, 10, QLatin1Char('0'));
}

QPushButton* footer_button(const QString& title, QWidget* parent) {
    auto* button = new QPushButton(title, parent);
    button->setFlat(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setStyleSheet(
        "QPushButton { border: 0; background: transparent; color: #434343;"
        " font-size: 13px; padding: 7px 0; }"
        "QPushButton:hover { color: #0078D7; }"
        "QPushButton:focus { border: 1px solid #0078D7; }");
    return button;
}
} // namespace

CountdownRing::CountdownRing(QWidget* parent) : QWidget(parent) {
    setObjectName("countdownRing");
    setFixedSize(164, 164);
    setAccessibleName("Countdown progress");
}

void CountdownRing::set_presentation(const PopoverPresentation& presentation) {
    presentation_ = presentation;
    update();
}

void CountdownRing::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF circle(20.0, 20.0, 124.0, 124.0);
    QPen track(QColor("#D2D2D2"), 4.0, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(track);
    painter.drawEllipse(circle);

    const double fraction = std::clamp(presentation_.progress, 0.0, 1.0);
    if (fraction > 0.0) {
        QPen progress(
            presentation_.phase == PopoverPhase::paused ||
            presentation_.phase == PopoverPhase::suspended
                ? QColor(muted)
                : QColor(accent),
            4.0, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(progress);
        painter.drawArc(circle, 90 * 16, -static_cast<int>(std::round(fraction * 360 * 16)));
    }

    QFont number = font();
    number.setPixelSize(31);
    number.setWeight(QFont::DemiBold);
    number.setStyleHint(QFont::SansSerif);
    painter.setFont(number);
    painter.setPen(QColor(foreground));
    painter.drawText(QRectF(8, 58, 148, 40), Qt::AlignCenter,
                     countdown(presentation_.remaining_seconds));

    QFont caption_font = font();
    caption_font.setPixelSize(12);
    painter.setFont(caption_font);
    painter.setPen(QColor(muted));
    painter.drawText(QRectF(4, 100, 156, 24), Qt::AlignHCenter | Qt::AlignVCenter,
                     QString::fromLatin1(caption(presentation_.phase)));
}

TrayPopover::TrayPopover(std::function<void()> take_break,
                         PauseCallback pause,
                         std::function<void()> resume,
                         std::function<void()> settings,
                         std::function<void()> quit)
    : QWidget(nullptr, Qt::Popup | Qt::FramelessWindowHint),
      take_break_(std::move(take_break)),
      resume_(std::move(resume)) {
    setObjectName("trayPopover");
    setWindowTitle("Blink Rest");
    setFixedSize(304, 360);
    setStyleSheet(
        "QWidget#trayPopover { background: white; border: 1px solid #D7D7D7;"
        " border-radius: 12px; }"
        "QLabel { background: transparent; }"
        "QPushButton#primaryAction { background: #0078D7; color: white;"
        " border: 1px solid #0078D7; border-radius: 7px;"
        " font-weight: 600; font-size: 14px; }"
        "QPushButton#primaryAction:hover { background: #006ABA; }"
        "QPushButton#primaryAction:disabled { color: #747474; background: #F6F6F6;"
        " border: 1px solid #D7D7D7; }"
        "QToolButton#pauseAction { background: #F6F6F6; color: #1F1F1F;"
        " border: 1px solid #D7D7D7; border-radius: 7px; font-size: 14px; }"
        "QToolButton#pauseAction:disabled { color: #909090; }");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 17, 20, 14);
    layout->setSpacing(0);

    auto* header = new QHBoxLayout;
    auto* name = new QLabel("Blink Rest", this);
    auto title_font = name->font();
    title_font.setPixelSize(15);
    title_font.setWeight(QFont::DemiBold);
    name->setFont(title_font);
    name->setStyleSheet(QString("color: %1;").arg(foreground));
    header->addWidget(name);
    header->addStretch(1);

    status_ = new QLabel("Active", this);
    status_->setObjectName("popoverStatus");
    status_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    status_->setStyleSheet("font-size: 12px; font-weight: 500; color: #107C41;");
    header->addWidget(status_);
    layout->addLayout(header);

    auto* ring_row = new QHBoxLayout;
    ring_row->addStretch(1);
    ring_ = new CountdownRing(this);
    ring_row->addWidget(ring_);
    ring_row->addStretch(1);
    layout->addSpacing(3);
    layout->addLayout(ring_row);
    layout->addSpacing(3);

    primary_ = new QPushButton("Take a break now", this);
    primary_->setObjectName("primaryAction");
    primary_->setCursor(Qt::PointingHandCursor);
    primary_->setFixedHeight(42);
    connect(primary_, &QPushButton::clicked, this, [this] {
        hide();
        if (presentation_.phase == PopoverPhase::paused) resume_();
        else take_break_();
    });
    layout->addWidget(primary_);
    layout->addSpacing(8);

    pause_ = new QToolButton(this);
    pause_->setObjectName("pauseAction");
    pause_->setText("Pause");
    pause_->setFixedHeight(38);
    pause_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    pause_->setCursor(Qt::PointingHandCursor);
    pause_->setPopupMode(QToolButton::InstantPopup);
    auto* pause_menu = new QMenu(pause_);
    auto* thirty = pause_menu->addAction("Pause for 30 minutes");
    auto* sixty = pause_menu->addAction("Pause for 1 hour");
    connect(thirty, &QAction::triggered, this, [this, pause] {
        hide();
        pause(1800);
    });
    connect(sixty, &QAction::triggered, this, [this, pause] {
        hide();
        pause(3600);
    });
    pause_->setMenu(pause_menu);
    layout->addWidget(pause_);
    layout->addSpacing(9);

    auto* divider = new QFrame(this);
    divider->setFixedHeight(1);
    divider->setStyleSheet("background: #E5E5E5; border: 0;");
    layout->addWidget(divider);
    layout->addSpacing(4);

    auto* footer = new QHBoxLayout;
    auto* settings_button = footer_button("Settings", this);
    auto* quit_button = footer_button("Quit", this);
    connect(settings_button, &QPushButton::clicked, this, [this, settings] {
        hide();
        settings();
    });
    connect(quit_button, &QPushButton::clicked, this, [quit] { quit(); });
    footer->addWidget(settings_button);
    footer->addStretch(1);
    footer->addWidget(quit_button);
    layout->addLayout(footer);
    update_presentation({});
}

void TrayPopover::update_presentation(const PopoverPresentation& presentation) {
    presentation_ = presentation;
    ring_->set_presentation(presentation);
    status_->setText(QString::fromLatin1(status(presentation.phase)));
    status_->setStyleSheet(QString("font-size: 12px; font-weight: 600; color: %1;")
                               .arg(status_color(presentation.phase).name()));
    const bool paused = presentation.phase == PopoverPhase::paused;
    const bool breaking = presentation.phase == PopoverPhase::breaking;
    const bool suspended = presentation.phase == PopoverPhase::suspended;
    primary_->setText(paused ? "Resume" :
                      (breaking ? "Breaking" : (suspended ? "Suspended" : "Take a break now")));
    primary_->setEnabled(!breaking && !suspended);
    pause_->setVisible(!paused);
    pause_->setEnabled(!breaking && !suspended);
}

void TrayPopover::show_at(const QPoint& anchor) {
    if (isVisible() && !standalone_) {
        hide();
        return;
    }
    if (standalone_) hide();
    standalone_ = false;
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    QScreen* screen = QGuiApplication::screenAt(anchor);
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect bounds = screen ? screen->availableGeometry() : QRect(0, 0, 1000, 700);
    const int x = std::clamp(anchor.x() - width() / 2, bounds.left(),
                             std::max(bounds.left(), bounds.right() - width() + 1));
    int y = anchor.y() + 10;
    if (y + height() > bounds.bottom()) y = anchor.y() - height() - 10;
    y = std::clamp(y, bounds.top(), std::max(bounds.top(), bounds.bottom() - height() + 1));
    move(x, y);
    show();
    raise();
    activateWindow();
}

void TrayPopover::show_standalone() {
    if (standalone_ && isVisible()) return;
    hide();
    standalone_ = true;
    setWindowFlags(Qt::Window | Qt::WindowCloseButtonHint);
    move(QPoint(100, 100));
    show();
    raise();
    activateWindow();
}

void TrayPopover::closeEvent(QCloseEvent* event) {
    QWidget::closeEvent(event);
    if (standalone_) QApplication::quit();
}

} // namespace blinkrest::linux_ui
