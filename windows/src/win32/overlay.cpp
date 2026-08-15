#include "overlay.hpp"

#include <d2d1.h>
#include <dwrite.h>
#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <memory>
#include <utility>
#include <vector>

namespace blinkrest::win32 {
namespace {

constexpr wchar_t kOverlayClassName[] = L"BlinkRest.Win32.Overlay";
constexpr UINT_PTR kHoldTimer = 1;
constexpr UINT kHoldDurationMs = 1500;
constexpr float kPointerMaximumDistance = 20.0f;
constexpr float kPi = 3.14159265358979323846f;

constexpr wchar_t kLookFarTitle[] = L"Look at a real distant object";
constexpr wchar_t kLookFarDetail[] = L"Let your focus settle away from the screen.";
constexpr wchar_t kBlinkTitle[] = L"Blink slowly and fully";
constexpr wchar_t kBlinkDetail[] = L"5 slow blinks";
constexpr wchar_t kCloseEyesTitle[] = L"Close your eyes";
constexpr wchar_t kCloseEyesDetail[] = L"The screen will clear when the break is over.";
constexpr wchar_t kSkipHint[] = L"Hold Esc or hold the button to skip";
constexpr wchar_t kSkipButton[] = L"Hold to skip";

double monotonic_now() {
    const auto duration = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration<double>(duration).count();
}

template <typename T>
void release(T*& value) {
    if (value) {
        value->Release();
        value = nullptr;
    }
}

D2D1_COLOR_F color(float r, float g, float b, float a = 1.0f) {
    return D2D1::ColorF(r / 255.0f, g / 255.0f, b / 255.0f, a);
}

const wchar_t* stage_title(BreakStage stage) {
    switch (stage) {
    case BreakStage::look_far: return kLookFarTitle;
    case BreakStage::blink: return kBlinkTitle;
    case BreakStage::close_eyes: return kCloseEyesTitle;
    }
    return kLookFarTitle;
}

const wchar_t* stage_detail(BreakStage stage) {
    switch (stage) {
    case BreakStage::look_far: return kLookFarDetail;
    case BreakStage::blink: return kBlinkDetail;
    case BreakStage::close_eyes: return kCloseEyesDetail;
    }
    return kLookFarDetail;
}

}  // namespace

struct OverlayManager::Impl {
    enum class HoldKind { none, keyboard, pointer };

    struct WindowRecord {
        Impl* owner = nullptr;
        HMONITOR monitor = nullptr;
        HWND window = nullptr;
        ID2D1HwndRenderTarget* target = nullptr;
    };

    struct MonitorSpec {
        HMONITOR monitor = nullptr;
        RECT bounds{};
        bool primary = false;
    };

    HINSTANCE instance = nullptr;
    std::function<void()> on_skip;
    ID2D1Factory* d2d_factory = nullptr;
    IDWriteFactory* dwrite_factory = nullptr;
    IDWriteTextFormat* title_format = nullptr;
    IDWriteTextFormat* detail_format = nullptr;
    IDWriteTextFormat* hint_format = nullptr;
    std::vector<std::unique_ptr<WindowRecord>> windows;
    BreakSession session{};
    MonotonicInstant at = 0;
    HoldKind hold_kind = HoldKind::none;
    double hold_started_at = 0;
    HWND hold_window = nullptr;
    POINT pointer_hold_origin{};
    bool pointer_hold_origin_valid = false;
    HHOOK keyboard_hook = nullptr;
    bool escape_key_down = false;
    bool active = false;
    bool class_registered = false;

    inline static Impl* keyboard_hook_owner = nullptr;

    Impl(HINSTANCE value, std::function<void()> callback)
        : instance(value), on_skip(std::move(callback)) {}

    ~Impl() {
        active = false;
        uninstall_keyboard_hook();
        cancel_hold();
        destroy_windows();
        release(title_format);
        release(detail_format);
        release(hint_format);
        release(dwrite_factory);
        release(d2d_factory);
        if (class_registered) {
            UnregisterClassW(kOverlayClassName, instance);
        }
    }

    bool initialize() {
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory))) {
            return false;
        }
        if (FAILED(DWriteCreateFactory(
                DWRITE_FACTORY_TYPE_SHARED,
                __uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(&dwrite_factory)))) {
            return false;
        }

        if (FAILED(dwrite_factory->CreateTextFormat(
                L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                40.0f, L"en-US", &title_format))) {
            return false;
        }
        if (FAILED(dwrite_factory->CreateTextFormat(
                L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                16.0f, L"en-US", &detail_format))) {
            return false;
        }
        if (FAILED(dwrite_factory->CreateTextFormat(
                L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_MEDIUM,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                12.0f, L"en-US", &hint_format))) {
            return false;
        }

        title_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        title_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        detail_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        detail_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        hint_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        hint_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.hInstance = instance;
        window_class.lpfnWndProc = &Impl::window_proc;
        window_class.lpszClassName = kOverlayClassName;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);

        const ATOM atom = RegisterClassExW(&window_class);
        if (atom == 0) {
            if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
                return false;
            }
        } else {
            class_registered = true;
        }
        return true;
    }

    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        WindowRecord* record = nullptr;
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            record = static_cast<WindowRecord*>(create->lpCreateParams);
            record->window = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(record));
        } else {
            record = reinterpret_cast<WindowRecord*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        }

        if (record && record->owner) {
            return record->owner->handle_message(*record, message, wparam, lparam);
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    static LRESULT CALLBACK low_level_keyboard_proc(
        int code, WPARAM wparam, LPARAM lparam
    ) {
        Impl* self = keyboard_hook_owner;
        if (code == HC_ACTION && self && self->active) {
            const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);
            if (key && key->vkCode == VK_ESCAPE) {
                if (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN) {
                    if (!self->escape_key_down) {
                        self->escape_key_down = true;
                        if (!self->windows.empty() && self->windows.front()->window) {
                            self->start_hold(*self->windows.front(), HoldKind::keyboard);
                        }
                    }
                    return 1;
                }
                if (wparam == WM_KEYUP || wparam == WM_SYSKEYUP) {
                    self->escape_key_down = false;
                    if (self->hold_kind == HoldKind::keyboard) self->cancel_hold();
                    return 1;
                }
            }
        }
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }

    LRESULT handle_message(WindowRecord& record, UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            if (record.target) {
                record.target->Resize(D2D1::SizeU(LOWORD(lparam), HIWORD(lparam)));
            }
            return 0;
        case WM_PAINT:
            paint(record);
            return 0;
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE) {
                const bool was_down = (lparam & (1LL << 30)) != 0;
                if (!was_down) start_hold(record, HoldKind::keyboard);
                return 0;
            }
            break;
        case WM_KEYUP:
            if (wparam == VK_ESCAPE) {
                if (hold_kind == HoldKind::keyboard) cancel_hold();
                return 0;
            }
            break;
        case WM_LBUTTONDOWN: {
            const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            RECT button = skip_button_rect_px(record);
            if (PtInRect(&button, point)) {
                SetCapture(record.window);
                pointer_hold_origin = point;
                pointer_hold_origin_valid = true;
                start_hold(record, HoldKind::pointer);
                return 0;
            }
            break;
        }
        case WM_MOUSEMOVE:
            if (
                hold_kind == HoldKind::pointer &&
                hold_window == record.window &&
                pointer_hold_origin_valid
            ) {
                const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                const long dx = point.x - pointer_hold_origin.x;
                const long dy = point.y - pointer_hold_origin.y;
                const float scale = static_cast<float>(GetDpiForWindow(record.window)) / 96.0f;
                const long maximum = static_cast<long>(std::lround(kPointerMaximumDistance * scale));
                if (dx * dx + dy * dy > maximum * maximum) {
                    cancel_hold();
                }
                return 0;
            }
            break;
        case WM_LBUTTONUP:
            if (hold_kind == HoldKind::pointer) {
                if (GetCapture() == record.window) ReleaseCapture();
                cancel_hold();
                return 0;
            }
            break;
        case WM_CAPTURECHANGED:
            if (hold_kind == HoldKind::pointer && hold_window == record.window) cancel_hold();
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wparam) == WA_INACTIVE && hold_kind == HoldKind::keyboard) cancel_hold();
            break;
        case WM_TIMER:
            if (wparam == kHoldTimer && record.window == hold_window) {
                complete_hold();
                return 0;
            }
            break;
        case WM_SETCURSOR: {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(record.window, &point);
            RECT button = skip_button_rect_px(record);
            if (PtInRect(&button, point)) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        }
        case WM_DESTROY:
            release(record.target);
            record.window = nullptr;
            return 0;
        default:
            break;
        }
        return DefWindowProcW(record.window, message, wparam, lparam);
    }

    void present(const BreakSession& value, MonotonicInstant instant) {
        session = value;
        at = instant;
        active = true;
        reconcile();
        install_keyboard_hook();
    }

    void update(const BreakSession& value, MonotonicInstant instant) {
        session = value;
        at = instant;
        if (!active) return;
        for (const auto& record : windows) {
            if (record->window) {
                SetWindowPos(
                    record->window, HWND_TOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER
                );
                InvalidateRect(record->window, nullptr, FALSE);
            }
        }
    }

    void dismiss() {
        active = false;
        uninstall_keyboard_hook();
        cancel_hold();
        destroy_windows();
    }

    void discard_cached_after_wake() {
        uninstall_keyboard_hook();
        cancel_hold();
        destroy_windows();
    }

    void reconcile() {
        if (!active) return;
        cancel_hold();
        destroy_windows();

        std::vector<MonitorSpec> monitors;
        EnumDisplayMonitors(
            nullptr, nullptr,
            [](HMONITOR monitor, HDC, LPRECT, LPARAM context) -> BOOL {
                auto* values = reinterpret_cast<std::vector<MonitorSpec>*>(context);
                MONITORINFO info{};
                info.cbSize = sizeof(info);
                if (GetMonitorInfoW(monitor, &info)) {
                    values->push_back(MonitorSpec{
                        monitor, info.rcMonitor,
                        (info.dwFlags & MONITORINFOF_PRIMARY) != 0,
                    });
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&monitors)
        );

        WindowRecord* primary = nullptr;
        for (const auto& monitor : monitors) {
            auto record = std::make_unique<WindowRecord>();
            record->owner = this;
            record->monitor = monitor.monitor;
            const int width = monitor.bounds.right - monitor.bounds.left;
            const int height = monitor.bounds.bottom - monitor.bounds.top;
            record->window = CreateWindowExW(
                WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                kOverlayClassName, L"Blink Rest", WS_POPUP,
                monitor.bounds.left, monitor.bounds.top, width, height,
                nullptr, nullptr, instance, record.get()
            );
            if (!record->window) continue;

            SetWindowPos(
                record->window, HWND_TOPMOST,
                monitor.bounds.left, monitor.bounds.top, width, height,
                SWP_SHOWWINDOW | SWP_NOOWNERZORDER
            );
            if (monitor.primary) primary = record.get();
            windows.push_back(std::move(record));
        }

        if (!primary && !windows.empty()) primary = windows.front().get();
        if (primary && primary->window) {
            SetForegroundWindow(primary->window);
            SetFocus(primary->window);
        }
    }

    void install_keyboard_hook() {
        if (keyboard_hook) return;
        keyboard_hook_owner = this;
        keyboard_hook = SetWindowsHookExW(
            WH_KEYBOARD_LL,
            &Impl::low_level_keyboard_proc,
            instance,
            0
        );
        if (!keyboard_hook && keyboard_hook_owner == this) {
            keyboard_hook_owner = nullptr;
        }
    }

    void uninstall_keyboard_hook() {
        if (keyboard_hook) {
            UnhookWindowsHookEx(keyboard_hook);
            keyboard_hook = nullptr;
        }
        if (keyboard_hook_owner == this) keyboard_hook_owner = nullptr;
        escape_key_down = false;
    }

    void destroy_windows() {
        for (auto& record : windows) {
            release(record->target);
            if (record->window) {
                DestroyWindow(record->window);
                record->window = nullptr;
            }
        }
        windows.clear();
    }

    void start_hold(WindowRecord& record, HoldKind kind) {
        if (!active || hold_kind != HoldKind::none) return;
        hold_kind = kind;
        hold_started_at = monotonic_now();
        hold_window = record.window;
        SetTimer(record.window, kHoldTimer, kHoldDurationMs, nullptr);
        invalidate_all();
    }

    void cancel_hold() {
        if (hold_window) KillTimer(hold_window, kHoldTimer);
        if (hold_kind == HoldKind::pointer && hold_window && GetCapture() == hold_window) {
            ReleaseCapture();
        }
        hold_kind = HoldKind::none;
        hold_started_at = 0;
        hold_window = nullptr;
        pointer_hold_origin = {};
        pointer_hold_origin_valid = false;
        invalidate_all();
    }

    void complete_hold() {
        if (!active || hold_kind == HoldKind::none) return;
        const HWND completed_window = hold_window;
        if (completed_window) KillTimer(completed_window, kHoldTimer);
        if (hold_kind == HoldKind::pointer && completed_window && GetCapture() == completed_window) {
            ReleaseCapture();
        }
        hold_kind = HoldKind::none;
        hold_started_at = 0;
        hold_window = nullptr;
        pointer_hold_origin = {};
        pointer_hold_origin_valid = false;
        if (on_skip) on_skip();
    }

    double hold_progress(HoldKind kind) const {
        if (hold_kind != kind || hold_started_at <= 0) return 0;
        return std::clamp((monotonic_now() - hold_started_at) / 1.5, 0.0, 1.0);
    }

    void invalidate_all() {
        for (const auto& record : windows) {
            if (record->window) InvalidateRect(record->window, nullptr, FALSE);
        }
    }

    RECT skip_button_rect_px(const WindowRecord& record) const {
        RECT client{};
        GetClientRect(record.window, &client);
        const float scale = static_cast<float>(GetDpiForWindow(record.window)) / 96.0f;
        const int margin = static_cast<int>(std::lround(30.0f * scale));
        const int width = static_cast<int>(std::lround(156.0f * scale));
        const int height = static_cast<int>(std::lround(36.0f * scale));
        const int top = static_cast<int>(std::lround(62.0f * scale));
        return RECT{client.right - margin - width, top, client.right - margin, top + height};
    }

    bool ensure_target(WindowRecord& record) {
        if (record.target) return true;
        RECT client{};
        GetClientRect(record.window, &client);
        const auto pixel_size = D2D1::SizeU(
            static_cast<UINT32>(std::max(0L, client.right - client.left)),
            static_cast<UINT32>(std::max(0L, client.bottom - client.top))
        );
        if (FAILED(d2d_factory->CreateHwndRenderTarget(
                D2D1::RenderTargetProperties(),
                D2D1::HwndRenderTargetProperties(record.window, pixel_size),
                &record.target))) {
            return false;
        }
        const float dpi = static_cast<float>(GetDpiForWindow(record.window));
        record.target->SetDpi(dpi, dpi);
        return true;
    }

    void draw_progress_arc(
        ID2D1RenderTarget* target, D2D1_POINT_2F center, float radius,
        double progress, ID2D1Brush* brush, float width
    ) {
        if (progress <= 0) return;
        if (progress >= 0.999999) {
            target->DrawEllipse(D2D1::Ellipse(center, radius, radius), brush, width);
            return;
        }

        const float start_angle = -0.5f * kPi;
        const float end_angle = start_angle + static_cast<float>(progress * 2.0 * kPi);
        const auto start = D2D1::Point2F(
            center.x + radius * std::cos(start_angle),
            center.y + radius * std::sin(start_angle)
        );
        const auto end = D2D1::Point2F(
            center.x + radius * std::cos(end_angle),
            center.y + radius * std::sin(end_angle)
        );

        ID2D1PathGeometry* geometry = nullptr;
        ID2D1GeometrySink* sink = nullptr;
        if (FAILED(d2d_factory->CreatePathGeometry(&geometry))) return;
        if (FAILED(geometry->Open(&sink))) {
            release(geometry);
            return;
        }
        sink->BeginFigure(start, D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddArc(D2D1::ArcSegment(
            end, D2D1::SizeF(radius, radius), 0.0f,
            D2D1_SWEEP_DIRECTION_CLOCKWISE,
            progress > 0.5 ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL
        ));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        target->DrawGeometry(geometry, brush, width);
        release(sink);
        release(geometry);
    }

    void draw_stage_icon(
        ID2D1RenderTarget* target, BreakStage stage,
        D2D1_POINT_2F center, ID2D1Brush* brush
    ) {
        ID2D1PathGeometry* eye = nullptr;
        ID2D1GeometrySink* sink = nullptr;
        if (FAILED(d2d_factory->CreatePathGeometry(&eye))) return;
        if (FAILED(eye->Open(&sink))) {
            release(eye);
            return;
        }

        const auto left = D2D1::Point2F(center.x - 34.0f, center.y);
        const auto right = D2D1::Point2F(center.x + 34.0f, center.y);
        sink->BeginFigure(
            left,
            stage == BreakStage::blink
                ? D2D1_FIGURE_BEGIN_FILLED
                : D2D1_FIGURE_BEGIN_HOLLOW
        );
        sink->AddBezier(D2D1::BezierSegment(
            D2D1::Point2F(center.x - 18.0f, center.y - 20.0f),
            D2D1::Point2F(center.x + 18.0f, center.y - 20.0f),
            right
        ));
        sink->AddBezier(D2D1::BezierSegment(
            D2D1::Point2F(center.x + 18.0f, center.y + 20.0f),
            D2D1::Point2F(center.x - 18.0f, center.y + 20.0f),
            left
        ));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();

        if (stage == BreakStage::blink) {
            target->FillGeometry(eye, brush);
        } else {
            target->DrawGeometry(eye, brush, 2.0f);
            target->FillEllipse(D2D1::Ellipse(center, 6.0f, 6.0f), brush);
        }

        if (stage == BreakStage::close_eyes) {
            const auto start = D2D1::Point2F(center.x - 27.0f, center.y - 24.0f);
            const auto end = D2D1::Point2F(center.x + 27.0f, center.y + 24.0f);
            target->DrawLine(start, end, brush, 4.0f);
            target->FillEllipse(D2D1::Ellipse(start, 2.0f, 2.0f), brush);
            target->FillEllipse(D2D1::Ellipse(end, 2.0f, 2.0f), brush);
        }

        release(sink);
        release(eye);
    }

    void draw_skip_controls(
        const WindowRecord& record, D2D1_SIZE_F size,
        ID2D1Brush* primary, ID2D1Brush* secondary, ID2D1Brush* accent,
        ID2D1Brush* track, ID2D1Brush* surface, ID2D1Brush* border
    ) {
        constexpr float margin = 30.0f;
        constexpr float button_width = 156.0f;
        constexpr float button_height = 36.0f;
        const auto button = D2D1::RectF(
            size.width - margin - button_width, 62.0f,
            size.width - margin, 62.0f + button_height
        );
        const D2D1_ROUNDED_RECT rounded{button, 10.0f, 10.0f};
        record.target->FillRoundedRectangle(rounded, surface);
        record.target->DrawRoundedRectangle(rounded, border, 1.0f);

        const auto pointer_center = D2D1::Point2F(button.left + 19.0f, button.top + 18.0f);
        record.target->DrawEllipse(D2D1::Ellipse(pointer_center, 10.0f, 10.0f), track, 2.0f);
        draw_progress_arc(record.target, pointer_center, 10.0f, hold_progress(HoldKind::pointer), accent, 2.0f);
        record.target->FillEllipse(D2D1::Ellipse(pointer_center, 2.0f, 2.0f), primary);

        hint_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        const auto button_text = D2D1::RectF(
            button.left + 38.0f, button.top, button.right - 10.0f, button.bottom
        );
        record.target->DrawTextW(
            kSkipButton, static_cast<UINT32>(wcslen(kSkipButton)), hint_format,
            button_text, primary, D2D1_DRAW_TEXT_OPTIONS_CLIP
        );

        const auto keyboard_center = D2D1::Point2F(size.width - margin - 14.0f, 44.0f);
        record.target->DrawEllipse(D2D1::Ellipse(keyboard_center, 13.0f, 13.0f), track, 2.0f);
        draw_progress_arc(record.target, keyboard_center, 13.0f, hold_progress(HoldKind::keyboard), accent, 2.0f);

        hint_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        const auto hint_rect = D2D1::RectF(
            std::max(20.0f, size.width - 300.0f), 30.0f,
            keyboard_center.x - 23.0f, 58.0f
        );
        record.target->DrawTextW(
            kSkipHint, static_cast<UINT32>(wcslen(kSkipHint)), hint_format,
            hint_rect, secondary, D2D1_DRAW_TEXT_OPTIONS_CLIP
        );
    }

    void paint(WindowRecord& record) {
        PAINTSTRUCT paint_struct{};
        BeginPaint(record.window, &paint_struct);
        if (!ensure_target(record)) {
            EndPaint(record.window, &paint_struct);
            return;
        }

        ID2D1SolidColorBrush* primary = nullptr;
        ID2D1SolidColorBrush* secondary = nullptr;
        ID2D1SolidColorBrush* accent = nullptr;
        ID2D1SolidColorBrush* halo = nullptr;
        ID2D1SolidColorBrush* track = nullptr;
        ID2D1SolidColorBrush* surface = nullptr;
        ID2D1SolidColorBrush* border = nullptr;
        record.target->CreateSolidColorBrush(color(245, 247, 250), &primary);
        record.target->CreateSolidColorBrush(color(166, 175, 188), &secondary);
        record.target->CreateSolidColorBrush(color(119, 169, 255), &accent);
        record.target->CreateSolidColorBrush(color(119, 169, 255, 0.16f), &halo);
        record.target->CreateSolidColorBrush(color(255, 255, 255, 0.12f), &track);
        record.target->CreateSolidColorBrush(color(23, 26, 32), &surface);
        record.target->CreateSolidColorBrush(color(255, 255, 255, 0.14f), &border);

        record.target->BeginDraw();
        record.target->Clear(color(14, 16, 19));

        const D2D1_SIZE_F size = record.target->GetSize();
        const float center_x = size.width * 0.5f;
        const float center_y = size.height * 0.5f - 54.0f;
        constexpr float radius = 94.0f;
        const BreakPhase phase = session.phase(at);
        if (phase.stage == BreakStage::blink) {
            const double wave = (1.0 - std::cos(at * 2.0 * kPi / 1.8)) * 0.5;
            const float halo_radius = radius * static_cast<float>(1.0 + 0.035 * wave);
            record.target->DrawEllipse(
                D2D1::Ellipse(D2D1::Point2F(center_x, center_y), halo_radius, halo_radius),
                halo,
                2.0f
            );
        }
        record.target->DrawEllipse(
            D2D1::Ellipse(D2D1::Point2F(center_x, center_y), radius, radius), track, 4.0f
        );
        const double progress = std::clamp(session.progress(at), 0.0, 1.0);
        draw_progress_arc(record.target, D2D1::Point2F(center_x, center_y), radius, progress, accent, 4.0f);

        draw_stage_icon(record.target, phase.stage, D2D1::Point2F(center_x, center_y), primary);

        const auto title_rect = D2D1::RectF(
            std::max(40.0f, center_x - 310.0f), center_y + radius + 26.0f,
            std::min(size.width - 40.0f, center_x + 310.0f), center_y + radius + 92.0f
        );
        const wchar_t* title = stage_title(phase.stage);
        record.target->DrawTextW(
            title, static_cast<UINT32>(wcslen(title)), title_format,
            title_rect, primary, D2D1_DRAW_TEXT_OPTIONS_CLIP
        );

        const auto detail_rect = D2D1::RectF(
            std::max(40.0f, center_x - 310.0f), center_y + radius + 96.0f,
            std::min(size.width - 40.0f, center_x + 310.0f), center_y + radius + 154.0f
        );
        const wchar_t* detail = stage_detail(phase.stage);
        record.target->DrawTextW(
            detail, static_cast<UINT32>(wcslen(detail)), detail_format,
            detail_rect, secondary, D2D1_DRAW_TEXT_OPTIONS_CLIP
        );

        draw_skip_controls(record, size, primary, secondary, accent, track, surface, border);
        const HRESULT draw_result = record.target->EndDraw();
        if (draw_result == D2DERR_RECREATE_TARGET) release(record.target);

        release(primary);
        release(secondary);
        release(accent);
        release(halo);
        release(track);
        release(surface);
        release(border);
        EndPaint(record.window, &paint_struct);
    }
};

OverlayManager::OverlayManager(HINSTANCE instance, std::function<void()> on_skip)
    : impl_(std::make_unique<Impl>(instance, std::move(on_skip))) {}
OverlayManager::~OverlayManager() = default;
bool OverlayManager::initialize() { return impl_->initialize(); }
void OverlayManager::present(const BreakSession& session, MonotonicInstant at) { impl_->present(session, at); }
void OverlayManager::update(const BreakSession& session, MonotonicInstant at) { impl_->update(session, at); }
void OverlayManager::dismiss() { impl_->dismiss(); }
void OverlayManager::discard_cached_after_wake() { impl_->discard_cached_after_wake(); }
void OverlayManager::cancel_hold() { impl_->cancel_hold(); }
void OverlayManager::reconcile() { impl_->reconcile(); }

}  // namespace blinkrest::win32
