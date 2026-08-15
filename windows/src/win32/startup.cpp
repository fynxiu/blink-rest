#include "startup.hpp"

#include <windows.h>

#include <optional>
#include <string>
#include <vector>

namespace blinkrest::win32 {
namespace {

constexpr wchar_t kRunKey[] = LR"(Software\Microsoft\Windows\CurrentVersion\Run)";
constexpr wchar_t kValueName[] = L"Blink Rest";

std::optional<std::wstring> executable_command() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size())
    );
    if (length == 0 || length >= buffer.size()) return std::nullopt;
    return L"\"" + std::wstring(buffer.data(), length) + L"\"";
}

std::optional<std::wstring> read_registration() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return std::nullopt;
    }

    DWORD type = 0;
    DWORD bytes = 0;
    const LSTATUS size_status = RegQueryValueExW(
        key, kValueName, nullptr, &type, nullptr, &bytes
    );
    if (size_status != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t)) {
        RegCloseKey(key);
        return std::nullopt;
    }

    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    DWORD read_bytes = bytes;
    const LSTATUS read_status = RegQueryValueExW(
        key,
        kValueName,
        nullptr,
        &type,
        reinterpret_cast<BYTE*>(buffer.data()),
        &read_bytes
    );
    RegCloseKey(key);
    if (read_status != ERROR_SUCCESS || type != REG_SZ) return std::nullopt;
    return std::wstring(buffer.data());
}

}  // namespace

bool StartupRegistration::is_enabled() const {
    const auto expected = executable_command();
    const auto actual = read_registration();
    return expected && actual && *expected == *actual;
}

bool StartupRegistration::set_enabled(bool enabled) const {
    if (!enabled) {
        HKEY key = nullptr;
        const LSTATUS open_status = RegOpenKeyExW(
            HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key
        );
        if (open_status == ERROR_FILE_NOT_FOUND) return true;
        if (open_status != ERROR_SUCCESS) return false;
        const LSTATUS delete_status = RegDeleteValueW(key, kValueName);
        RegCloseKey(key);
        return delete_status == ERROR_SUCCESS || delete_status == ERROR_FILE_NOT_FOUND;
    }

    const auto command = executable_command();
    if (!command) return false;

    HKEY key = nullptr;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            kRunKey,
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE,
            nullptr,
            &key,
            nullptr
        ) != ERROR_SUCCESS) {
        return false;
    }

    const DWORD bytes = static_cast<DWORD>((command->size() + 1) * sizeof(wchar_t));
    const LSTATUS status = RegSetValueExW(
        key,
        kValueName,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(command->c_str()),
        bytes
    );
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

}  // namespace blinkrest::win32
