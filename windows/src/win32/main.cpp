#include <blinkrest/core.hpp>

#include "overlay.hpp"
#include "resource.h"
#include "settings.hpp"
#include "startup.hpp"
#include "tray_popover.hpp"
#include "warning.hpp"

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wtsapi32.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace {

using blinkrest::Breaking;
using blinkrest::EventKind;
using blinkrest::Paused;
using blinkrest::Running;
using blinkrest::SessionContext;
using blinkrest::SessionEffect;
using blinkrest::SessionEvent;
using blinkrest::SessionSchedule;
using blinkrest::SessionState;
using blinkrest::Suspended;
using blinkrest::Warning;

constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT_PTR kTickTimer = 1;
constexpr UINT_PTR kSuspensionResumeTimer = 2;
constexpr UINT_PTR kDiagnosticQuitTimer = 3;
constexpr UINT kTrayIconId = 1;

constexpr UINT kCommandTakeBreak = 1001;
constexpr UINT kCommandPause30 = 1002;
constexpr UINT kCommandPause60 = 1003;
constexpr UINT kCommandResume = 1004;
constexpr UINT kCommandWork20 = 1101;
constexpr UINT kCommandWork25 = 1102;
constexpr UINT kCommandWork30 = 1103;
constexpr UINT kCommandWork40 = 1104;
constexpr UINT kCommandBreak30 = 1201;
constexpr UINT kCommandBreak45 = 1202;
constexpr UINT kCommandBreak60 = 1203;
constexpr UINT kCommandBreak90 = 1204;
constexpr UINT kCommandToggleLaunchAtLogin = 1301;
constexpr UINT kCommandQuit = 1099;

double monotonic_now() {
    const auto duration = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration<double>(duration).count();
}

double wall_now() {
    const auto duration = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration<double>(duration).count();
}

int remaining_seconds(const SessionState& state, double monotonic, double wall) {
    if (const auto* running = std::get_if<Running>(&state)) {
        return static_cast<int>(std::ceil(std::max(0.0, running->deadline - monotonic)));
    }
    if (const auto* warning = std::get_if<Warning>(&state)) {
        return static_cast<int>(std::ceil(std::max(0.0, warning->break_starts_at - monotonic)));
    }
    if (const auto* breaking = std::get_if<Breaking>(&state)) {
        return static_cast<int>(std::ceil(breaking->session.remaining(monotonic)));
    }
    if (const auto* paused = std::get_if<Paused>(&state)) {
        return static_cast<int>(std::ceil(std::max(0.0, paused->until - wall)));
    }
    return 0;
}

class App {
public:
    explicit App(HINSTANCE instance)
        : instance_(instance), state_(Suspended{}) {}

    ~App() { cleanup(); }

    bool initialize(bool headless_smoke, bool overlay_smoke) {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(com_result) && com_result != RPC_E_CHANGED_MODE) {
            return false;
        }
        com_initialized_ = SUCCEEDED(com_result);

        if (SUCCEEDED(CoCreateInstance(
                CLSID_VirtualDesktopManager,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&virtual_desktop_manager_)
            ))) {
            refresh_virtual_desktop_baseline();
        }

        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.hInstance = instance_;
        window_class.lpfnWndProc = &App::window_proc;
        window_class.lpszClassName = L"BlinkRest.Win32.Host";
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);

        if (RegisterClassExW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        window_ = CreateWindowExW(
            WS_EX_TOOLWINDOW,
            window_class.lpszClassName,
            L"Blink Rest",
            WS_OVERLAPPED,
            0, 0, 0, 0,
            nullptr, nullptr, instance_, this
        );
        if (!window_) {
            return false;
        }

        taskbar_created_message_ = RegisterWindowMessageW(L"TaskbarCreated");

        const auto stored_settings = settings_store_.load();
        schedule_ = stored_settings.schedule;
        persisted_pause_until_ = stored_settings.pause_until;

        overlay_ = std::make_unique<blinkrest::win32::OverlayManager>(
            instance_,
            [this] { dispatch({EventKind::escape_hold_completed, std::nullopt}); }
        );
        if (!overlay_->initialize()) {
            return false;
        }

        warning_ = std::make_unique<blinkrest::win32::WarningPanel>(instance_);
        if (!warning_->initialize()) {
            return false;
        }

        wts_registered_ = WTSRegisterSessionNotification(window_, NOTIFY_FOR_THIS_SESSION) != FALSE;
        tray_enabled_ = !headless_smoke && !overlay_smoke;
        if (tray_enabled_) {
            tray_popover_ = std::make_unique<blinkrest::win32::TrayPopover>(
                instance_,
                [this] { dispatch({EventKind::start_break_now, std::nullopt}); },
                [this](int seconds) { dispatch(SessionEvent::pause(wall_now() + seconds)); },
                [this] { dispatch({EventKind::resume, std::nullopt}); }
            );
            if (!tray_popover_->initialize()) {
                return false;
            }
        }
        if (tray_enabled_ && !add_tray_icon()) {
            return false;
        }

        dispatch({EventKind::launch, std::nullopt});
        if (overlay_smoke) {
            schedule_.break_duration = 10;
            dispatch({EventKind::start_break_now, std::nullopt});
            SetTimer(window_, kDiagnosticQuitTimer, 12000, nullptr);
        }
        return true;
    }

    int run() {
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }

    void shutdown_smoke() {
        if (window_) {
            DestroyWindow(window_);
        }
    }

private:
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        App* app = nullptr;
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            app = static_cast<App*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
            app->window_ = window;
        } else {
            app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        }

        if (app) {
            return app->handle_message(window, message, wparam, lparam);
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    LRESULT handle_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (taskbar_created_message_ != 0 && message == taskbar_created_message_) {
            if (tray_enabled_) {
                tray_added_ = false;
                add_tray_icon();
                update_tray();
            }
            return 0;
        }

        switch (message) {
        case WM_TIMER:
            if (wparam == kTickTimer) {
                if (std::holds_alternative<Warning>(state_) || std::holds_alternative<Breaking>(state_)) {
                    detect_presentation_context_change();
                }
                dispatch({EventKind::tick, std::nullopt});
                return 0;
            }
            if (wparam == kSuspensionResumeTimer) {
                KillTimer(window, kSuspensionResumeTimer);
                dispatch({EventKind::suspension_resume_debounce_elapsed, std::nullopt});
                return 0;
            }
            if (wparam == kDiagnosticQuitTimer) {
                KillTimer(window, kDiagnosticQuitTimer);
                DestroyWindow(window);
                return 0;
            }
            break;
        case WM_DISPLAYCHANGE:
            dispatch({EventKind::displays_changed, std::nullopt});
            return 0;
        case WM_POWERBROADCAST:
            if (wparam == PBT_APMSUSPEND) {
                dispatch({EventKind::system_will_sleep, std::nullopt});
                return TRUE;
            }
            if (wparam == PBT_APMRESUMEAUTOMATIC || wparam == PBT_APMRESUMESUSPEND) {
                dispatch({EventKind::system_did_wake, std::nullopt});
                return TRUE;
            }
            break;
        case WM_WTSSESSION_CHANGE:
            if (
                wparam == WTS_SESSION_LOCK ||
                wparam == WTS_CONSOLE_DISCONNECT ||
                wparam == WTS_REMOTE_DISCONNECT ||
                wparam == WTS_SESSION_LOGOFF
            ) {
                dispatch({EventKind::session_did_resign_active, std::nullopt});
                return 0;
            }
            if (
                wparam == WTS_SESSION_UNLOCK ||
                wparam == WTS_CONSOLE_CONNECT ||
                wparam == WTS_REMOTE_CONNECT ||
                wparam == WTS_SESSION_LOGON
            ) {
                dispatch({EventKind::session_did_become_active, std::nullopt});
                return 0;
            }
            break;
        case WM_COMMAND:
            handle_command(LOWORD(wparam));
            return 0;
        case kTrayCallbackMessage:
            if (LOWORD(lparam) == WM_RBUTTONUP || LOWORD(lparam) == WM_CONTEXTMENU) {
                if (tray_popover_) tray_popover_->hide();
                show_tray_menu();
                return 0;
            }
            if (
                LOWORD(lparam) == WM_LBUTTONUP ||
                LOWORD(lparam) == NIN_SELECT ||
                LOWORD(lparam) == NIN_KEYSELECT
            ) {
                const ULONGLONG now = GetTickCount64();
                if (now - last_tray_activation_ms_ < 250) {
                    return 0;
                }
                last_tray_activation_ms_ = now;
                toggle_tray_popover();
                return 0;
            }
            if (LOWORD(lparam) == WM_LBUTTONDBLCLK) {
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            if (wts_registered_) {
                WTSUnRegisterSessionNotification(window);
                wts_registered_ = false;
            }
            window_ = nullptr;
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }

        return DefWindowProcW(window, message, wparam, lparam);
    }

    void handle_command(UINT command) {
        switch (command) {
        case kCommandTakeBreak:
            dispatch({EventKind::start_break_now, std::nullopt});
            break;
        case kCommandPause30:
            dispatch(SessionEvent::pause(wall_now() + 30.0 * 60.0));
            break;
        case kCommandPause60:
            dispatch(SessionEvent::pause(wall_now() + 60.0 * 60.0));
            break;
        case kCommandResume:
            dispatch({EventKind::resume, std::nullopt});
            break;
        case kCommandWork20:
        case kCommandWork25:
        case kCommandWork30:
        case kCommandWork40: {
            const double values[] = {1200, 1500, 1800, 2400};
            schedule_.work_interval = values[command - kCommandWork20];
            settings_store_.persist_schedule(schedule_);
            dispatch({EventKind::work_interval_changed, std::nullopt});
            break;
        }
        case kCommandBreak30:
        case kCommandBreak45:
        case kCommandBreak60:
        case kCommandBreak90: {
            const double values[] = {30, 45, 60, 90};
            schedule_.break_duration = values[command - kCommandBreak30];
            settings_store_.persist_schedule(schedule_);
            dispatch({EventKind::break_duration_changed, std::nullopt});
            break;
        }
        case kCommandToggleLaunchAtLogin:
            startup_registration_.set_enabled(!startup_registration_.is_enabled());
            break;
        case kCommandQuit:
            if (window_) {
                DestroyWindow(window_);
            }
            break;
        default:
            break;
        }
    }

    void dispatch(const SessionEvent& event) {
        const SessionContext context{
            monotonic_now(),
            wall_now(),
            schedule_,
            persisted_pause_until_,
        };
        auto transition = blinkrest::reduce_session(state_, event, context);
        state_ = std::move(transition.state);
        perform_effects(transition.effects);
        update_tick_schedule();
        update_tray();
    }

    void perform_effects(const std::vector<SessionEffect>& effects) {
        for (const auto& effect : effects) {
            std::visit([this](const auto& value) { perform_effect(value); }, effect);
        }
    }

    void perform_effect(const blinkrest::ShowWarning& effect) {
        if (warning_) warning_->show(effect.seconds_remaining);
    }

    void perform_effect(const blinkrest::HideWarning&) {
        if (warning_) warning_->hide();
    }

    void perform_effect(const blinkrest::CaptureFrontmostApplication&) {
        const HWND foreground = GetForegroundWindow();
        DWORD process_id = 0;
        if (foreground) {
            GetWindowThreadProcessId(foreground, &process_id);
        }
        previously_foreground_ = process_id != GetCurrentProcessId() ? foreground : nullptr;
        refresh_virtual_desktop_baseline(previously_foreground_ ? previously_foreground_ : foreground);
    }

    void perform_effect(const blinkrest::DiscardFrontmostApplication&) {
        previously_foreground_ = nullptr;
    }

    void perform_effect(const blinkrest::RestoreFrontmostApplication&) {
        if (previously_foreground_ && IsWindow(previously_foreground_)) {
            SetForegroundWindow(previously_foreground_);
        }
        previously_foreground_ = nullptr;
    }

    void perform_effect(const blinkrest::PresentOverlay& effect) {
        if (overlay_) overlay_->present(effect.session, monotonic_now());
    }

    void perform_effect(const blinkrest::UpdateOverlay& effect) {
        if (overlay_) overlay_->update(effect.session, effect.at);
    }

    void perform_effect(const blinkrest::DismissOverlay&) {
        if (overlay_) overlay_->dismiss();
        refresh_virtual_desktop_baseline();
    }

    void perform_effect(const blinkrest::DiscardCachedOverlayWindowsAfterWake&) {
        if (overlay_) overlay_->discard_cached_after_wake();
    }

    void perform_effect(const blinkrest::CancelEscapeHold&) {
        if (overlay_) overlay_->cancel_hold();
    }

    void perform_effect(const blinkrest::ReconcileOverlays&) {
        if (overlay_) overlay_->reconcile();
    }

    void perform_effect(const blinkrest::PersistPauseUntil& effect) {
        persisted_pause_until_ = effect.value;
        settings_store_.persist_pause_until(effect.value);
    }

    void perform_effect(const blinkrest::ScheduleSuspensionResumeDebounce& effect) {
        KillTimer(window_, kSuspensionResumeTimer);
        const auto delay_ms = static_cast<UINT>(std::max(1.0, std::ceil(effect.delay * 1000.0)));
        SetTimer(window_, kSuspensionResumeTimer, delay_ms, nullptr);
    }

    void perform_effect(const blinkrest::CancelSuspensionResumeDebounce&) {
        KillTimer(window_, kSuspensionResumeTimer);
    }

    void update_tick_schedule() {
        UINT desired_interval = 0;
        if (std::holds_alternative<Running>(state_) || std::holds_alternative<Paused>(state_)) {
            desired_interval = 1000;
        } else if (std::holds_alternative<Warning>(state_) || std::holds_alternative<Breaking>(state_)) {
            desired_interval = 100;
        }

        if (desired_interval == tick_interval_ms_) {
            return;
        }

        KillTimer(window_, kTickTimer);
        tick_interval_ms_ = desired_interval;
        if (desired_interval != 0) {
            SetTimer(window_, kTickTimer, desired_interval, nullptr);
        }
    }

    static bool same_guid(const GUID& left, const GUID& right) {
        return InlineIsEqualGUID(left, right) != FALSE;
    }

    std::optional<GUID> desktop_id_for_window(HWND window) const {
        if (!virtual_desktop_manager_ || !window || !IsWindow(window)) {
            return std::nullopt;
        }

        GUID desktop_id{};
        if (FAILED(virtual_desktop_manager_->GetWindowDesktopId(window, &desktop_id))) {
            return std::nullopt;
        }
        return desktop_id;
    }

    void refresh_virtual_desktop_baseline(HWND preferred = nullptr) {
        HWND candidate = preferred;
        if (!candidate) candidate = GetForegroundWindow();
        const auto desktop_id = desktop_id_for_window(candidate);
        if (desktop_id) current_virtual_desktop_ = *desktop_id;
    }

    void detect_presentation_context_change() {
        HWND foreground = GetForegroundWindow();
        if (!foreground) return;

        DWORD process_id = 0;
        GetWindowThreadProcessId(foreground, &process_id);
        if (process_id == GetCurrentProcessId()) {
            return;
        }

        const auto desktop_id = desktop_id_for_window(foreground);
        if (!desktop_id) return;

        if (!current_virtual_desktop_) {
            current_virtual_desktop_ = *desktop_id;
            return;
        }
        if (same_guid(*current_virtual_desktop_, *desktop_id)) {
            return;
        }

        current_virtual_desktop_ = *desktop_id;
        dispatch({EventKind::presentation_context_changed, std::nullopt});
    }

    bool add_tray_icon() {
        tray_data_ = {};
        tray_data_.cbSize = sizeof(tray_data_);
        tray_data_.hWnd = window_;
        tray_data_.uID = kTrayIconId;
        tray_data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        tray_data_.uCallbackMessage = kTrayCallbackMessage;
        tray_data_.hIcon = LoadIconW(
            GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(IDI_BLINKREST)
        );
        if (!tray_data_.hIcon) {
            tray_data_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        }
        wcscpy_s(tray_data_.szTip, L"Blink Rest");

        if (!Shell_NotifyIconW(NIM_ADD, &tray_data_)) {
            return false;
        }
        tray_added_ = true;
        tray_data_.uVersion = NOTIFYICON_VERSION_4;
        tray_version_4_ = Shell_NotifyIconW(NIM_SETVERSION, &tray_data_) != FALSE;
        return true;
    }

    void update_tray() {
        if (!tray_added_) {
            return;
        }

        const int remaining = remaining_seconds(state_, monotonic_now(), wall_now());
        const int minutes = remaining / 60;
        const int seconds = remaining % 60;

        const wchar_t* phase = L"Active";
        if (std::holds_alternative<Warning>(state_)) {
            phase = L"Break soon";
        } else if (std::holds_alternative<Paused>(state_)) {
            phase = L"Paused";
        } else if (std::holds_alternative<Breaking>(state_)) {
            phase = L"Breaking";
        } else if (std::holds_alternative<Suspended>(state_)) {
            phase = L"Suspended";
        }

        wchar_t tooltip[128]{};
        swprintf_s(tooltip, L"Blink Rest - %ls - %02d:%02d", phase, minutes, seconds);
        wcscpy_s(tray_data_.szTip, tooltip);
        tray_data_.uFlags = NIF_TIP;
        Shell_NotifyIconW(NIM_MODIFY, &tray_data_);
        update_tray_popover();
    }

    void update_tray_popover() {
        if (!tray_popover_) return;

        const double monotonic = monotonic_now();
        const double wall = wall_now();
        blinkrest::win32::TrayPopoverPresentation presentation{};
        presentation.remaining_seconds = remaining_seconds(state_, monotonic, wall);

        if (const auto* running = std::get_if<Running>(&state_)) {
            presentation.phase = blinkrest::win32::TrayPopoverPhase::running;
            const double duration = std::max(1.0, schedule_.effective_work_interval());
            presentation.progress = std::clamp(
                1.0 - ((running->deadline - monotonic) / duration), 0.0, 1.0
            );
        } else if (const auto* warning = std::get_if<Warning>(&state_)) {
            presentation.phase = blinkrest::win32::TrayPopoverPhase::warning;
            const double duration = std::max(1.0, schedule_.effective_work_interval());
            presentation.progress = std::clamp(
                1.0 - ((warning->break_starts_at - monotonic) / duration), 0.0, 1.0
            );
        } else if (std::holds_alternative<Paused>(state_) || std::holds_alternative<Suspended>(state_)) {
            presentation.phase = blinkrest::win32::TrayPopoverPhase::paused;
        } else if (const auto* breaking = std::get_if<Breaking>(&state_)) {
            presentation.phase = blinkrest::win32::TrayPopoverPhase::breaking;
            presentation.progress = breaking->session.progress(monotonic);
        }
        tray_popover_->update(presentation);
    }

    void toggle_tray_popover() {
        if (!tray_popover_ || !tray_added_) return;

        NOTIFYICONIDENTIFIER identifier{};
        identifier.cbSize = sizeof(identifier);
        identifier.hWnd = tray_data_.hWnd;
        identifier.uID = tray_data_.uID;
        RECT anchor{};
        if (FAILED(Shell_NotifyIconGetRect(&identifier, &anchor))) {
            POINT cursor{};
            if (!GetCursorPos(&cursor)) return;
            anchor.left = cursor.x;
            anchor.top = cursor.y;
            anchor.right = cursor.x + 1;
            anchor.bottom = cursor.y + 1;
        }
        update_tray_popover();
        tray_popover_->toggle(anchor);
    }

    void show_tray_menu() {
        HMENU menu = CreatePopupMenu();
        if (!menu) {
            return;
        }

        HMENU work_menu = CreatePopupMenu();
        HMENU break_menu = CreatePopupMenu();
        if (!work_menu || !break_menu) {
            if (work_menu) DestroyMenu(work_menu);
            if (break_menu) DestroyMenu(break_menu);
            DestroyMenu(menu);
            return;
        }

        const bool breaking = std::holds_alternative<Breaking>(state_);
        const bool paused = std::holds_alternative<Paused>(state_);
        const UINT disabled = breaking ? MF_GRAYED : 0;

        AppendMenuW(menu, MF_STRING | disabled, kCommandTakeBreak, L"Take a break now");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        if (paused) {
            AppendMenuW(menu, MF_STRING, kCommandResume, L"Resume");
        } else {
            AppendMenuW(menu, MF_STRING | disabled, kCommandPause30, L"Pause for 30 minutes");
            AppendMenuW(menu, MF_STRING | disabled, kCommandPause60, L"Pause for 60 minutes");
        }
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

        const auto checked = [](bool value) -> UINT {
            return MF_STRING | (value ? MF_CHECKED : MF_UNCHECKED);
        };
        AppendMenuW(work_menu, checked(schedule_.work_interval == 1200), kCommandWork20, L"20 minutes");
        AppendMenuW(work_menu, checked(schedule_.work_interval == 1500), kCommandWork25, L"25 minutes");
        AppendMenuW(work_menu, checked(schedule_.work_interval == 1800), kCommandWork30, L"30 minutes");
        AppendMenuW(work_menu, checked(schedule_.work_interval == 2400), kCommandWork40, L"40 minutes");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(work_menu), L"Work interval");

        AppendMenuW(break_menu, checked(schedule_.break_duration == 30), kCommandBreak30, L"30 seconds");
        AppendMenuW(break_menu, checked(schedule_.break_duration == 45), kCommandBreak45, L"45 seconds");
        AppendMenuW(break_menu, checked(schedule_.break_duration == 60), kCommandBreak60, L"60 seconds");
        AppendMenuW(break_menu, checked(schedule_.break_duration == 90), kCommandBreak90, L"90 seconds");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(break_menu), L"Break duration");
        AppendMenuW(
            menu,
            checked(startup_registration_.is_enabled()),
            kCommandToggleLaunchAtLogin,
            L"Launch at login"
        );
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kCommandQuit, L"Quit Blink Rest");

        POINT cursor{};
        GetCursorPos(&cursor);
        SetForegroundWindow(window_);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, cursor.x, cursor.y, 0, window_, nullptr);
        DestroyMenu(menu);
    }

    void cleanup() {
        tray_popover_.reset();
        warning_.reset();
        overlay_.reset();
        if (window_) {
            KillTimer(window_, kTickTimer);
            KillTimer(window_, kSuspensionResumeTimer);
            KillTimer(window_, kDiagnosticQuitTimer);
            if (wts_registered_) {
                WTSUnRegisterSessionNotification(window_);
                wts_registered_ = false;
            }
        }
        if (tray_added_) {
            Shell_NotifyIconW(NIM_DELETE, &tray_data_);
            tray_added_ = false;
        }
        if (virtual_desktop_manager_) {
            virtual_desktop_manager_->Release();
            virtual_desktop_manager_ = nullptr;
        }
        if (com_initialized_) {
            CoUninitialize();
            com_initialized_ = false;
        }
    }

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HWND previously_foreground_ = nullptr;
    SessionSchedule schedule_{};
    SessionState state_;
    std::optional<double> persisted_pause_until_;
    blinkrest::win32::RegistrySettingsStore settings_store_;
    blinkrest::win32::StartupRegistration startup_registration_;
    IVirtualDesktopManager* virtual_desktop_manager_ = nullptr;
    std::optional<GUID> current_virtual_desktop_;
    std::unique_ptr<blinkrest::win32::OverlayManager> overlay_;
    std::unique_ptr<blinkrest::win32::TrayPopover> tray_popover_;
    std::unique_ptr<blinkrest::win32::WarningPanel> warning_;
    NOTIFYICONDATAW tray_data_{};
    UINT tick_interval_ms_ = 0;
    UINT taskbar_created_message_ = 0;
    bool tray_added_ = false;
    bool tray_enabled_ = false;
    bool tray_version_4_ = false;
    ULONGLONG last_tray_activation_ms_ = 0;
    bool wts_registered_ = false;
    bool com_initialized_ = false;
};

bool has_argument(const wchar_t* argument) {
    return wcsstr(GetCommandLineW(), argument) != nullptr;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    const bool resident_smoke = has_argument(L"--resident-smoke");
    const bool headless_smoke = !resident_smoke && has_argument(L"--headless-smoke");
    const bool smoke_initialization = headless_smoke || resident_smoke;
    const bool overlay_smoke =
        !smoke_initialization && has_argument(L"--overlay-smoke");
    App app(instance);
    if (!app.initialize(smoke_initialization, overlay_smoke)) {
        return 1;
    }
    if (headless_smoke) {
        app.shutdown_smoke();
        return 0;
    }
    return app.run();
}
