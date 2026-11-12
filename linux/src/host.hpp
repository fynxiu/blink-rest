#pragma once
#include "overlay.hpp"
#include "settings.hpp"
#include "tray_popover.hpp"

#include <blinkrest/core.hpp>

#include <QMenu>
#include <QObject>
#include <QSystemTrayIcon>
#include <memory>
#include <QTimer>

class QAction;
class QActionGroup;
class QCheckBox;
class QLabel;
class QPushButton;
class QButtonGroup;
class QWidget;

namespace blinkrest::linux_ui {

class AppHost final : public QObject {
    Q_OBJECT
public:
    explicit AppHost(bool smoke_mode = false);
    ~AppHost() override;
    void initialize();
    void dispatch(SessionEvent event);
    void set_work_minutes(int minutes);
    void set_break_seconds(int seconds);
    const SessionState& state() const { return state_; }
    int overlay_count() const { return overlays_.count(); }

public slots:
    void on_prepare_for_sleep(bool sleeping);
    void on_screen_saver_active(bool active);

private:
    SessionContext context() const;
    void apply_effect(const SessionEffect& effect);
    void initialize_ui();
    void initialize_lifecycle();
    void initialize_menu();
    void show_settings();
    void initialize_control_window();
    void update_ui();
    void check_tray_availability();
    void screens_changed();
    QString status_text() const;
    void toggle_autostart(bool checked);
    static QIcon eye_icon();

    bool smoke_mode_;
    SettingsStore settings_;
    SessionSchedule schedule_;
    std::optional<WallInstant> pause_until_;
    SessionState state_ = Suspended{};
    WarningToast warning_;
    OverlayManager overlays_;
    QTimer tick_timer_;
    QTimer resume_timer_;
    QTimer tray_check_timer_;
    QMenu menu_;
    QSystemTrayIcon tray_;
    std::unique_ptr<TrayPopover> popover_;

    QAction* break_now_ = nullptr;
    QAction* resume_ = nullptr;
    QAction* autostart_ = nullptr;
    QAction* show_window_ = nullptr;
    QActionGroup* work_group_ = nullptr;
    QActionGroup* break_group_ = nullptr;
    QWidget* control_window_ = nullptr;
    QLabel* status_label_ = nullptr;
    QButtonGroup* work_buttons_ = nullptr;
    QButtonGroup* duration_buttons_ = nullptr;
    QCheckBox* control_autostart_ = nullptr;
    bool shown_as_fallback_ = false;
};

} // namespace blinkrest::linux_ui
