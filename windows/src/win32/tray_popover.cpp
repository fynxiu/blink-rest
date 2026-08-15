#include "tray_popover.hpp"

#include <d2d1.h>
#include <dwrite.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <utility>

namespace blinkrest::win32 {
namespace {

constexpr wchar_t kClassName[] = L"BlinkRest.Win32.TrayPopover";
constexpr int kLogicalWidth = 304;
constexpr int kLogicalHeight = 318;
constexpr UINT kPause30 = 1;
constexpr UINT kPause60 = 2;
constexpr float kPi = 3.14159265358979323846f;

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

}  // namespace

struct TrayPopover::Impl {
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    ID2D1Factory* d2d_factory = nullptr;
    IDWriteFactory* dwrite_factory = nullptr;
    ID2D1HwndRenderTarget* target = nullptr;
    IDWriteTextFormat* header_format = nullptr;
    IDWriteTextFormat* status_format = nullptr;
    IDWriteTextFormat* countdown_format = nullptr;
    IDWriteTextFormat* caption_format = nullptr;
    IDWriteTextFormat* button_format = nullptr;
    TrayPopoverPresentation presentation{};
    VoidCallback take_break_now;
    PauseCallback pause_for_seconds;
    VoidCallback resume;
    RECT primary_button{};
    RECT pause_button{};

    Impl(
        HINSTANCE value,
        VoidCallback take_break,
        PauseCallback pause,
        VoidCallback resume_callback
    )
        : instance(value),
          take_break_now(std::move(take_break)),
          pause_for_seconds(std::move(pause)),
          resume(std::move(resume_callback)) {}

    ~Impl() {
        if (window) DestroyWindow(window);
        release(button_format);
        release(caption_format);
        release(countdown_format);
        release(status_format);
        release(header_format);
        release(target);
        release(dwrite_factory);
        release(d2d_factory);
    }

    bool create_format(float size, DWRITE_FONT_WEIGHT weight, IDWriteTextFormat** out) {
        const HRESULT result = dwrite_factory->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            weight,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            size,
            L"en-US",
            out
        );
        if (FAILED(result)) return false;
        (*out)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        return true;
    }

    bool initialize() {
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory))) return false;
        if (FAILED(DWriteCreateFactory(
                DWRITE_FACTORY_TYPE_SHARED,
                __uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(&dwrite_factory)))) return false;

        if (!create_format(15.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &header_format)) return false;
        if (!create_format(12.0f, DWRITE_FONT_WEIGHT_MEDIUM, &status_format)) return false;
        if (!create_format(31.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, &countdown_format)) return false;
        if (!create_format(12.0f, DWRITE_FONT_WEIGHT_NORMAL, &caption_format)) return false;
        if (!create_format(14.0f, DWRITE_FONT_WEIGHT_MEDIUM, &button_format)) return false;

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance;
        wc.lpfnWndProc = &Impl::window_proc;
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

        window = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
            kClassName,
            L"Blink Rest",
            WS_POPUP,
            0, 0, kLogicalWidth, kLogicalHeight,
            nullptr, nullptr, instance, this
        );
        return window != nullptr;
    }

    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        Impl* self = nullptr;
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            self = static_cast<Impl*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->window = window;
        } else {
            self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        }
        return self ? self->handle_message(message, wparam, lparam)
                    : DefWindowProcW(window, message, wparam, lparam);
    }

    bool ensure_target() {
        if (target) return true;
        RECT client{};
        GetClientRect(window, &client);
        const auto size = D2D1::SizeU(
            static_cast<UINT32>(std::max(0L, client.right - client.left)),
            static_cast<UINT32>(std::max(0L, client.bottom - client.top))
        );
        if (FAILED(d2d_factory->CreateHwndRenderTarget(
                D2D1::RenderTargetProperties(),
                D2D1::HwndRenderTargetProperties(window, size),
                &target))) return false;
        const float dpi = static_cast<float>(GetDpiForWindow(window));
        target->SetDpi(dpi, dpi);
        return true;
    }

    void position(const RECT& anchor) {
        HMONITOR monitor = MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        GetMonitorInfoW(monitor, &info);

        const UINT dpi = GetDpiForWindow(window);
        const int width = MulDiv(kLogicalWidth, static_cast<int>(dpi), 96);
        const int height = MulDiv(kLogicalHeight, static_cast<int>(dpi), 96);
        int x = anchor.left + ((anchor.right - anchor.left) - width) / 2;
        int y = anchor.top - height - 8;
        if (y < info.rcWork.top) y = anchor.bottom + 8;
        const int work_left = static_cast<int>(info.rcWork.left);
        const int work_top = static_cast<int>(info.rcWork.top);
        const int work_right = static_cast<int>(info.rcWork.right);
        const int work_bottom = static_cast<int>(info.rcWork.bottom);
        x = std::clamp(x, work_left, std::max(work_left, work_right - width));
        y = std::clamp(y, work_top, std::max(work_top, work_bottom - height));
        SetWindowPos(window, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
    }

    const wchar_t* status_text() const {
        switch (presentation.phase) {
        case TrayPopoverPhase::running: return L"Active";
        case TrayPopoverPhase::warning: return L"Break soon";
        case TrayPopoverPhase::paused: return L"Paused";
        case TrayPopoverPhase::breaking: return L"Breaking";
        }
        return L"Active";
    }

    const wchar_t* caption_text() const {
        switch (presentation.phase) {
        case TrayPopoverPhase::running: return L"Until next break";
        case TrayPopoverPhase::warning: return L"Break in";
        case TrayPopoverPhase::paused: return L"Until resume";
        case TrayPopoverPhase::breaking: return L"Break remaining";
        }
        return L"Until next break";
    }

    void draw_progress_arc(D2D1_POINT_2F center, float radius, ID2D1Brush* brush) {
        const double progress = std::clamp(presentation.progress, 0.0, 1.0);
        if (progress <= 0.0) return;
        if (progress >= 0.999999) {
            target->DrawEllipse(D2D1::Ellipse(center, radius, radius), brush, 4.0f);
            return;
        }
        ID2D1PathGeometry* geometry = nullptr;
        ID2D1GeometrySink* sink = nullptr;
        if (FAILED(d2d_factory->CreatePathGeometry(&geometry))) return;
        if (FAILED(geometry->Open(&sink))) { release(geometry); return; }
        const float start_angle = -0.5f * kPi;
        const float end_angle = start_angle + static_cast<float>(progress * 2.0 * kPi);
        const auto start = D2D1::Point2F(center.x + radius * std::cos(start_angle), center.y + radius * std::sin(start_angle));
        const auto end = D2D1::Point2F(center.x + radius * std::cos(end_angle), center.y + radius * std::sin(end_angle));
        sink->BeginFigure(start, D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddArc(D2D1::ArcSegment(
            end,
            D2D1::SizeF(radius, radius),
            0.0f,
            D2D1_SWEEP_DIRECTION_CLOCKWISE,
            progress > 0.5 ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL
        ));
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
        target->DrawGeometry(geometry, brush, 4.0f);
        release(sink);
        release(geometry);
    }

    void paint() {
        PAINTSTRUCT ps{};
        BeginPaint(window, &ps);
        if (!ensure_target()) { EndPaint(window, &ps); return; }

        ID2D1SolidColorBrush* text = nullptr;
        ID2D1SolidColorBrush* secondary = nullptr;
        ID2D1SolidColorBrush* accent = nullptr;
        ID2D1SolidColorBrush* track = nullptr;
        ID2D1SolidColorBrush* surface = nullptr;
        ID2D1SolidColorBrush* border = nullptr;
        ID2D1SolidColorBrush* status = nullptr;
        target->CreateSolidColorBrush(color(31, 31, 31), &text);
        target->CreateSolidColorBrush(color(105, 105, 105), &secondary);
        target->CreateSolidColorBrush(color(0, 120, 215), &accent);
        target->CreateSolidColorBrush(color(210, 210, 210), &track);
        target->CreateSolidColorBrush(color(246, 246, 246), &surface);
        target->CreateSolidColorBrush(color(215, 215, 215), &border);
        if (presentation.phase == TrayPopoverPhase::warning) {
            target->CreateSolidColorBrush(color(196, 95, 0), &status);
        } else if (presentation.phase == TrayPopoverPhase::running) {
            target->CreateSolidColorBrush(color(16, 124, 65), &status);
        } else {
            target->CreateSolidColorBrush(color(90, 90, 90), &status);
        }

        target->BeginDraw();
        target->Clear(color(255, 255, 255));

        header_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        status_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        target->DrawTextW(L"Blink Rest", 10, header_format, D2D1::RectF(20, 18, 170, 45), text);
        const wchar_t* status_label = status_text();
        target->DrawTextW(status_label, static_cast<UINT32>(wcslen(status_label)), status_format, D2D1::RectF(150, 19, 284, 45), status);

        const auto center = D2D1::Point2F(152.0f, 125.0f);
        target->DrawEllipse(D2D1::Ellipse(center, 62.0f, 62.0f), track, 4.0f);
        draw_progress_arc(center, 62.0f, presentation.phase == TrayPopoverPhase::paused ? secondary : accent);

        wchar_t countdown[16]{};
        const int remaining = std::max(0, presentation.remaining_seconds);
        swprintf_s(countdown, L"%02d:%02d", remaining / 60, remaining % 60);
        countdown_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        target->DrawTextW(countdown, static_cast<UINT32>(wcslen(countdown)), countdown_format, D2D1::RectF(86, 99, 218, 136), text);
        caption_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        const wchar_t* caption = caption_text();
        target->DrawTextW(caption, static_cast<UINT32>(wcslen(caption)), caption_format, D2D1::RectF(80, 138, 224, 160), secondary);

        const bool paused = presentation.phase == TrayPopoverPhase::paused;
        const bool breaking = presentation.phase == TrayPopoverPhase::breaking;
        primary_button = {20, 205, 284, 247};
        const D2D1_ROUNDED_RECT primary{D2D1::RectF(20, 205, 284, 247), 6, 6};
        target->FillRoundedRectangle(primary, breaking ? surface : accent);
        target->DrawRoundedRectangle(primary, breaking ? border : accent, 1.0f);
        button_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        const wchar_t* primary_label = paused ? L"Resume" : (breaking ? L"Breaking" : L"Take a break now");
        target->DrawTextW(primary_label, static_cast<UINT32>(wcslen(primary_label)), button_format, D2D1::RectF(20, 205, 284, 247), breaking ? secondary : (paused ? text : surface));

        pause_button = {20, 257, 284, 295};
        if (!paused) {
            const D2D1_ROUNDED_RECT pause_rect{D2D1::RectF(20, 257, 284, 295), 6, 6};
            target->FillRoundedRectangle(pause_rect, surface);
            target->DrawRoundedRectangle(pause_rect, border, 1.0f);
            target->DrawTextW(L"Pause", 5, button_format, D2D1::RectF(20, 257, 284, 295), breaking ? secondary : text);
        } else {
            pause_button = {};
        }

        const HRESULT draw_result = target->EndDraw();
        if (draw_result == D2DERR_RECREATE_TARGET) release(target);
        release(status);
        release(border);
        release(surface);
        release(track);
        release(accent);
        release(secondary);
        release(text);
        EndPaint(window, &ps);
    }

    static bool contains(const RECT& rect, POINT point) {
        return point.x >= rect.left && point.x < rect.right && point.y >= rect.top && point.y < rect.bottom;
    }

    POINT client_pixels_to_dip(POINT point) const {
        const int dpi = static_cast<int>(GetDpiForWindow(window));
        point.x = MulDiv(point.x, 96, dpi);
        point.y = MulDiv(point.y, 96, dpi);
        return point;
    }

    POINT dip_to_client_pixels(POINT point) const {
        const int dpi = static_cast<int>(GetDpiForWindow(window));
        point.x = MulDiv(point.x, dpi, 96);
        point.y = MulDiv(point.y, dpi, 96);
        return point;
    }

    void show_pause_menu() {
        HMENU menu = CreatePopupMenu();
        if (!menu) return;
        AppendMenuW(menu, MF_STRING, kPause30, L"Pause for 30 minutes");
        AppendMenuW(menu, MF_STRING, kPause60, L"Pause for 60 minutes");
        POINT point = dip_to_client_pixels({pause_button.left, pause_button.bottom});
        ClientToScreen(window, &point);
        const UINT command = TrackPopupMenu(
            menu,
            TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN,
            point.x,
            point.y,
            0,
            window,
            nullptr
        );
        DestroyMenu(menu);
        if (command == kPause30 && pause_for_seconds) pause_for_seconds(30 * 60);
        if (command == kPause60 && pause_for_seconds) pause_for_seconds(60 * 60);
    }

    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
        case WM_PAINT:
            paint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_ACTIVATE:
            if (LOWORD(wparam) == WA_INACTIVE && IsWindowVisible(window)) {
                ShowWindow(window, SW_HIDE);
            }
            return 0;
        case WM_LBUTTONUP: {
            POINT point = client_pixels_to_dip({GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
            if (contains(primary_button, point)) {
                if (presentation.phase == TrayPopoverPhase::paused) {
                    if (resume) resume();
                } else if (presentation.phase != TrayPopoverPhase::breaking) {
                    if (take_break_now) take_break_now();
                }
                return 0;
            }
            if (pause_button.right > pause_button.left && contains(pause_button, point)) {
                if (presentation.phase != TrayPopoverPhase::breaking) show_pause_menu();
                return 0;
            }
            break;
        }
        case WM_DPICHANGED: {
            const auto* suggested = reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(
                window,
                nullptr,
                suggested->left,
                suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE
            );
            release(target);
            return 0;
        }
        case WM_DESTROY:
            window = nullptr;
            return 0;
        default:
            break;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }
};

TrayPopover::TrayPopover(
    HINSTANCE instance,
    VoidCallback take_break_now,
    PauseCallback pause_for_seconds,
    VoidCallback resume
) : impl_(std::make_unique<Impl>(
        instance,
        std::move(take_break_now),
        std::move(pause_for_seconds),
        std::move(resume)
    )) {}

TrayPopover::~TrayPopover() = default;
bool TrayPopover::initialize() { return impl_->initialize(); }

void TrayPopover::update(const TrayPopoverPresentation& presentation) {
    impl_->presentation = presentation;
    if (impl_->window && IsWindowVisible(impl_->window)) {
        InvalidateRect(impl_->window, nullptr, FALSE);
    }
}

void TrayPopover::toggle(const RECT& anchor) {
    if (!impl_->window) return;
    if (IsWindowVisible(impl_->window)) {
        ShowWindow(impl_->window, SW_HIDE);
        return;
    }
    impl_->position(anchor);
    ShowWindow(impl_->window, SW_SHOWNORMAL);
    SetForegroundWindow(impl_->window);
    SetFocus(impl_->window);
    InvalidateRect(impl_->window, nullptr, FALSE);
    UpdateWindow(impl_->window);
}

void TrayPopover::hide() {
    if (impl_->window) ShowWindow(impl_->window, SW_HIDE);
}

}  // namespace blinkrest::win32
