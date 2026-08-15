#include "update_checker.hpp"

#include <blinkrest/update_discovery.hpp>

#include <winhttp.h>

#include <cmath>
#include <cwchar>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace blinkrest::win32 {
namespace {

constexpr wchar_t kRegistryPath[] = LR"(Software\fynxiu\BlinkRest)";
constexpr wchar_t kLastAutomaticCheck[] = L"updates.lastAutomaticCheckEpochSeconds";
constexpr wchar_t kLastPromptedVersion[] = L"updates.lastPromptedVersion";
constexpr double kAutomaticIntervalSeconds = 24.0 * 60.0 * 60.0;
constexpr wchar_t kApiHost[] = L"api.github.com";
constexpr wchar_t kApiPath[] = L"/repos/fynxiu/blink-rest/releases?per_page=100";

std::optional<std::wstring> read_registry_string(const wchar_t* name) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryPath, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    DWORD bytes = 0;
    const LSTATUS size_status = RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, nullptr, &bytes);
    if (size_status != ERROR_SUCCESS || bytes < sizeof(wchar_t)) {
        RegCloseKey(key);
        return std::nullopt;
    }
    std::vector<wchar_t> buffer((bytes + sizeof(wchar_t) - 1) / sizeof(wchar_t));
    const LSTATUS read_status = RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ, nullptr, buffer.data(), &bytes);
    RegCloseKey(key);
    if (read_status != ERROR_SUCCESS) return std::nullopt;
    return std::wstring(buffer.data());
}

void write_registry_string(const wchar_t* name, const std::wstring& value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER, kRegistryPath, 0, nullptr, REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE, nullptr, &key, nullptr
        ) != ERROR_SUCCESS) {
        return;
    }
    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    RegSetValueExW(
        key, name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()), bytes
    );
    RegCloseKey(key);
}

std::wstring widen_utf8(std::string_view input) {
    if (input.empty()) return {};
    const int count = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), nullptr, 0
    );
    if (count <= 0) return {};
    std::wstring output(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()),
            output.data(), count
        ) != count) {
        return {};
    }
    return output;
}

std::optional<std::string> fetch_releases(const std::string& current_version) {
    const std::wstring agent = L"BlinkRest/" + widen_utf8(current_version) + L" Windows";
    HINTERNET session = WinHttpOpen(
        agent.c_str(),
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!session) return std::nullopt;
    WinHttpSetTimeouts(session, 5000, 5000, 10000, 10000);

    HINTERNET connection = WinHttpConnect(session, kApiHost, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) {
        WinHttpCloseHandle(session);
        return std::nullopt;
    }
    HINTERNET request = WinHttpOpenRequest(
        connection, L"GET", kApiPath, nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE
    );
    if (!request) {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return std::nullopt;
    }

    const wchar_t headers[] =
        L"Accept: application/vnd.github+json\r\n"
        L"X-GitHub-Api-Version: 2022-11-28\r\n";
    WinHttpAddRequestHeaders(request, headers, static_cast<DWORD>(-1L), WINHTTP_ADDREQ_FLAG_ADD);

    const bool sent = WinHttpSendRequest(
        request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0
    ) != FALSE;
    const bool received = sent && WinHttpReceiveResponse(request, nullptr) != FALSE;

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    const bool status_ok = received && WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status,
        &status_size,
        WINHTTP_NO_HEADER_INDEX
    ) != FALSE && status >= 200 && status < 300;

    std::string body;
    bool read_ok = status_ok;
    while (read_ok) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            read_ok = false;
            break;
        }
        if (available == 0) break;
        if (body.size() + available > 16 * 1024 * 1024) {
            read_ok = false;
            break;
        }
        const std::size_t offset = body.size();
        body.resize(offset + available);
        DWORD read = 0;
        if (!WinHttpReadData(request, body.data() + offset, available, &read)) {
            read_ok = false;
            break;
        }
        body.resize(offset + read);
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    if (!read_ok) return std::nullopt;
    return body;
}

UpdateCheckResult perform_check(const std::string& current_version, bool manual) {
    UpdateCheckResult result;
    result.manual = manual;
    const auto current = updates::Version::parse(current_version);
    const auto body = fetch_releases(current_version);
    if (!current || !body) return result;

    const auto discovery = updates::discover_latest_compatible_release(*body, "-windows-x64.zip");
    if (!discovery.valid) return result;
    if (!discovery.latest || discovery.latest->version <= *current) {
        result.outcome = UpdateCheckOutcome::up_to_date;
        return result;
    }
    result.outcome = UpdateCheckOutcome::update_available;
    result.version = discovery.latest->version.description();
    result.release_url = widen_utf8(discovery.latest->html_url);
    if (result.release_url.empty()) result.outcome = UpdateCheckOutcome::failed;
    return result;
}

}  // namespace

UpdateChecker::UpdateChecker(std::string current_version)
    : current_version_(std::move(current_version)),
      checking_(std::make_shared<std::atomic_bool>(false)) {}

bool UpdateChecker::check_automatically(
    HWND notify_window,
    UINT completion_message,
    double now_epoch_seconds
) {
    if (!automatic_check_due(now_epoch_seconds)) return false;
    persist_automatic_check_time(now_epoch_seconds);
    return begin_check(notify_window, completion_message, false);
}

bool UpdateChecker::check_manually(HWND notify_window, UINT completion_message) {
    return begin_check(notify_window, completion_message, true);
}

bool UpdateChecker::is_checking() const {
    return checking_->load();
}

bool UpdateChecker::should_prompt_for(const std::string& version) const {
    const auto stored = read_registry_string(kLastPromptedVersion);
    return !stored || *stored != widen_utf8(version);
}

void UpdateChecker::mark_prompted(const std::string& version) const {
    write_registry_string(kLastPromptedVersion, widen_utf8(version));
}

bool UpdateChecker::begin_check(HWND notify_window, UINT completion_message, bool manual) {
    bool expected = false;
    if (!checking_->compare_exchange_strong(expected, true)) return false;
    const std::string current_version = current_version_;
    const auto checking = checking_;
    std::thread([notify_window, completion_message, manual, current_version, checking] {
        auto* result = new UpdateCheckResult(perform_check(current_version, manual));
        checking->store(false);
        if (!PostMessageW(
                notify_window,
                completion_message,
                0,
                reinterpret_cast<LPARAM>(result)
            )) {
            delete result;
        }
    }).detach();
    return true;
}

bool UpdateChecker::automatic_check_due(double now_epoch_seconds) const {
    const auto stored = read_registry_string(kLastAutomaticCheck);
    if (!stored) return true;
    wchar_t* end = nullptr;
    const double previous = std::wcstod(stored->c_str(), &end);
    if (end == stored->c_str() || (end && *end != L'\0') || !std::isfinite(previous)) return true;
    const double elapsed = now_epoch_seconds - previous;
    return elapsed < 0.0 || elapsed >= kAutomaticIntervalSeconds;
}

void UpdateChecker::persist_automatic_check_time(double now_epoch_seconds) const {
    wchar_t buffer[64]{};
    swprintf_s(buffer, L"%.17g", now_epoch_seconds);
    write_registry_string(kLastAutomaticCheck, buffer);
}

}  // namespace blinkrest::win32
