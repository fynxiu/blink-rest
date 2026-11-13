#pragma once

#include <blinkrest/core.hpp>
#include <blinkrest/skip_placement.hpp>

#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <functional>

class QLabel;
class QPushButton;
class QHBoxLayout;
class QScreen;

namespace blinkrest::linux_ui {

class EyeProgressRing;
class HoldProgressRing;

class BreakOverlay final : public QWidget {
public:
    BreakOverlay(QScreen* screen, std::function<void()> skip_callback,
                 SkipCorner corner = SkipCorner::top_right);
    void set_skip_corner(SkipCorner corner);
    SkipCorner skip_corner() const { return skip_corner_; }
    void display(const BreakSession& session, MonotonicInstant now);
    void refresh(MonotonicInstant now);
    void cancel_hold();
    void position_on_screen();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void begin_hold();
    void end_hold();
    void position_skip_panel();

    QPointer<QScreen> screen_;
    std::function<void()> skip_callback_;
    SkipCorner skip_corner_ = SkipCorner::top_right;
    BreakSession session_{};
    QLabel* stage_ = nullptr;
    QLabel* detail_ = nullptr;
    EyeProgressRing* ring_ = nullptr;
    QWidget* skip_panel_ = nullptr;
    QHBoxLayout* skip_row_ = nullptr;
    QHBoxLayout* hint_row_ = nullptr;
    QLabel* hint_ = nullptr;
    QPushButton* skip_ = nullptr;
    HoldProgressRing* keyboard_progress_ = nullptr;
    HoldProgressRing* pointer_progress_ = nullptr;
    QTimer hold_timer_;
    QTimer animation_timer_;
    QElapsedTimer hold_clock_;
    bool key_held_ = false;
    bool button_held_ = false;
};

class WarningToast final : public QWidget {
public:
    WarningToast();
    void display_seconds(int seconds);

private:
    QLabel* label_ = nullptr;
};

class OverlayManager {
public:
    explicit OverlayManager(std::function<void()> skip_callback);
    ~OverlayManager();
    void present(const BreakSession& session, MonotonicInstant now);
    void update(MonotonicInstant now);
    void reconcile(MonotonicInstant now);
    void cancel_holds();
    void dismiss();
    int count() const { return overlays_.size(); }
    SkipCorner current_corner() const { return current_corner_; }

private:
    std::function<void()> skip_callback_;
    QHash<QScreen*, BreakOverlay*> overlays_;
    std::optional<BreakSession> session_;
    std::optional<SkipCorner> last_corner_;
    SkipCorner current_corner_ = SkipCorner::top_right;
};

} // namespace blinkrest::linux_ui
