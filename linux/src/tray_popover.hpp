#pragma once

#include <QWidget>

#include <functional>

class QLabel;
class QToolButton;
class QPushButton;
class QCloseEvent;
class QPaintEvent;

namespace blinkrest::linux_ui {

// Presentation contract shared with macOS MenuBarPresentation and Windows
// TrayPopoverPresentation. It deliberately contains no platform state.
enum class PopoverPhase { running, warning, paused, breaking, suspended };

struct PopoverPresentation {
    PopoverPhase phase = PopoverPhase::running;
    int remaining_seconds = 0;
    double progress = 0.0;
};

class CountdownRing final : public QWidget {
public:
    explicit CountdownRing(QWidget* parent = nullptr);
    void set_presentation(const PopoverPresentation& presentation);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    PopoverPresentation presentation_;
};

class TrayPopover final : public QWidget {
public:
    using PauseCallback = std::function<void(int)>;

    TrayPopover(std::function<void()> take_break,
                PauseCallback pause,
                std::function<void()> resume,
                std::function<void()> settings,
                std::function<void()> quit);

    void update_presentation(const PopoverPresentation& presentation);
    void show_at(const QPoint& anchor);
    void show_standalone();
    bool is_standalone() const { return standalone_; }
    const PopoverPresentation& presentation() const { return presentation_; }

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    PopoverPresentation presentation_;
    CountdownRing* ring_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* primary_ = nullptr;
    QToolButton* pause_ = nullptr;
    bool standalone_ = false;
    std::function<void()> take_break_;
    std::function<void()> resume_;
};

} // namespace blinkrest::linux_ui
