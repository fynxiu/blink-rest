#include "warning.hpp"

#include <d2d1.h>
#include <dwrite.h>

#include <algorithm>
#include <cwchar>

namespace blinkrest::win32 {
namespace {

constexpr wchar_t kWarningClassName[] = L"BlinkRest.Win32.Warning";
constexpr float kPanelWidth = 248.0f;
constexpr float kPanelHeight = 48.0f;
constexpr float kTopMargin = 24.0f;
constexpr float kCornerRadius = 16.0f;

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

struct WarningPanel::Impl {
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    ID2D1Factory* d2d_factory = nullptr;
    IDWriteFactory* dwrite_factory = nullptr;
    IDWriteTextFormat* text_format = nullptr;
    ID2D1HwndRenderTarget* target = nullptr;
    int seconds_remaining = 0;
    bool class_registered = false;

    explicit Impl(HINSTANCE value) : instance(value) {}

    ~Impl() {
        if (window) DestroyWindow(window);
        release(target);
        release(text_format);
        release(dwrite_factory);
        release(d2d_factory);
        if (class_registered) UnregisterClassW(kWarningClassName, instance);
    }

    bool initialize() {
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory))) return false;
        if (FAILED(DWriteCreateFactory(
                DWRITE_FACTORY_TYPE_SHARED,
                __uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(&dwrite_factory)))) return false;

        if (FAILED(dwrite_factory->CreateTextFormat(
                L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_MEDIUM,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                14.0f, L"en-US", &text_format))) return false;
        text_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        text_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance;
        wc.lpfnWndProc = &Impl::window_proc;
        wc.lpszClassName = kWarningClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        const ATOM atom = RegisterClassExW(&wc);
        if (atom == 0) {
            if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        } else {
            class_registered = true;
        }

        window = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            kWarningClassName,
            L"Blink Rest warning",
            WS_POPUP,
            0, 0, 1, 1,
            nullptr, nullptr, instance, this
        );
        return window != nullptr;
    }

    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        Impl* self = nullptr;
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            self = static_cast<Impl*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->window = hwnd;
        } else {
            self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }
        if (self) return self->handle_message(hwnd, message, wparam, lparam);
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    LRESULT handle_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_SIZE:
            if (target) target->Resize(D2D1::SizeU(LOWORD(lparam), HIWORD(lparam)));
            return 0;
        case WM_PAINT:
            paint();
            return 0;
        case WM_DPICHANGED: {
            const auto* rect = reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(
                hwnd, HWND_TOPMOST,
                rect->left, rect->top,
                rect->right - rect->left, rect->bottom - rect->top,
                SWP_NOACTIVATE
            );
            return 0;
        }
        case WM_DESTROY:
            release(target);
            window = nullptr;
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }
    }

    bool ensure_target() {
        if (target) return true;
        RECT client{};
        GetClientRect(window, &client);
        const auto pixels = D2D1::SizeU(
            static_cast<UINT32>(std::max(0L, client.right - client.left)),
            static_cast<UINT32>(std::max(0L, client.bottom - client.top))
        );
        if (FAILED(d2d_factory->CreateHwndRenderTarget(
                D2D1::RenderTargetProperties(),
                D2D1::HwndRenderTargetProperties(window, pixels),
                &target))) return false;
        const float dpi = static_cast<float>(GetDpiForWindow(window));
        target->SetDpi(dpi, dpi);
        return true;
    }

    void paint() {
        PAINTSTRUCT ps{};
        BeginPaint(window, &ps);
        if (!ensure_target()) {
            EndPaint(window, &ps);
            return;
        }

        ID2D1SolidColorBrush* surface = nullptr;
        ID2D1SolidColorBrush* border = nullptr;
        ID2D1SolidColorBrush* primary = nullptr;
        ID2D1SolidColorBrush* secondary = nullptr;
        target->CreateSolidColorBrush(color(37, 40, 46), &surface);
        target->CreateSolidColorBrush(color(112, 116, 124, 0.62f), &border);
        target->CreateSolidColorBrush(color(245, 247, 250), &primary);
        target->CreateSolidColorBrush(color(172, 177, 185), &secondary);

        target->BeginDraw();
        target->Clear(color(0, 0, 0, 0));
        const D2D1_SIZE_F size = target->GetSize();
        const D2D1_ROUNDED_RECT pill{
            D2D1::RectF(0.5f, 0.5f, size.width - 0.5f, size.height - 0.5f),
            kCornerRadius,
            kCornerRadius,
        };
        target->FillRoundedRectangle(pill, surface);
        target->DrawRoundedRectangle(pill, border, 1.0f);

        const D2D1_POINT_2F eye_center = D2D1::Point2F(28.0f, size.height * 0.5f);
        target->DrawEllipse(D2D1::Ellipse(eye_center, 8.0f, 5.0f), secondary, 1.5f);
        target->FillEllipse(D2D1::Ellipse(eye_center, 2.3f, 2.3f), secondary);

        wchar_t label[128]{};
        swprintf_s(label, L"Blink Rest, break in %d seconds", std::max(0, seconds_remaining));
        const auto text_rect = D2D1::RectF(48.0f, 0.0f, size.width - 16.0f, size.height);
        target->DrawTextW(
            label,
            static_cast<UINT32>(wcslen(label)),
            text_format,
            text_rect,
            primary,
            D2D1_DRAW_TEXT_OPTIONS_CLIP
        );

        const HRESULT result = target->EndDraw();
        if (result == D2DERR_RECREATE_TARGET) release(target);
        release(surface);
        release(border);
        release(primary);
        release(secondary);
        EndPaint(window, &ps);
    }

    void reposition() {
        POINT cursor{};
        GetCursorPos(&cursor);
        const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info)) return;

        SetWindowPos(window, HWND_TOPMOST, info.rcWork.left, info.rcWork.top, 1, 1, SWP_NOACTIVATE);
        const float scale = static_cast<float>(GetDpiForWindow(window)) / 96.0f;
        const int width = static_cast<int>(kPanelWidth * scale + 0.5f);
        const int height = static_cast<int>(kPanelHeight * scale + 0.5f);
        const int x = info.rcWork.left + ((info.rcWork.right - info.rcWork.left) - width) / 2;
        const int y = info.rcWork.top + static_cast<int>(kTopMargin * scale + 0.5f);
        SetWindowPos(
            window, HWND_TOPMOST, x, y, width, height,
            SWP_NOACTIVATE | SWP_SHOWWINDOW
        );
    }

    void show(int seconds) {
        seconds_remaining = std::max(0, seconds);
        reposition();
        InvalidateRect(window, nullptr, FALSE);
        ShowWindow(window, SW_SHOWNOACTIVATE);
    }

    void hide() {
        if (window) ShowWindow(window, SW_HIDE);
    }
};

WarningPanel::WarningPanel(HINSTANCE instance)
    : impl_(std::make_unique<Impl>(instance)) {}
WarningPanel::~WarningPanel() = default;
bool WarningPanel::initialize() { return impl_->initialize(); }
void WarningPanel::show(int seconds_remaining) { impl_->show(seconds_remaining); }
void WarningPanel::hide() { impl_->hide(); }

}  // namespace blinkrest::win32
