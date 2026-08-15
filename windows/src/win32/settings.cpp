#include "settings.hpp"

#include <array>
#include <cmath>
#include <cwchar>

namespace blinkrest::win32 {
namespace {

constexpr wchar_t kRegistryPath[] = LR"(Software\fynxiu\BlinkRest)";
constexpr wchar_t kSchemaVersion[] = L"settings.schemaVersion";
constexpr wchar_t kWorkInterval[] = L"settings.workIntervalSeconds";
constexpr wchar_t kBreakDuration[] = L"settings.breakDurationSeconds";
constexpr wchar_t kPauseUntil[] = L"runtime.pauseUntilEpochSeconds";
constexpr wchar_t kLegacyLaunchAtLogin[] = L"settings.launchAtLogin";

constexpr DWORD kCurrentSchemaVersion = 2;
constexpr std::array<DWORD, 4> kAllowedWorkIntervals{1200, 1500, 1800, 2400};
constexpr std::array<DWORD, 4> kAllowedBreakDurations{30, 45, 60, 90};

template <std::size_t N>
bool contains(const std::array<DWORD, N>& values, DWORD value) {
    for (const DWORD candidate : values) {
        if (candidate == value) return true;
    }
    return false;
}

std::optional<DWORD> read_dword(HKEY key, const wchar_t* name) {
    DWORD value = 0;
    DWORD size = sizeof(value);
    const LSTATUS status = RegGetValueW(
        key, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size
    );
    if (status != ERROR_SUCCESS || size != sizeof(value)) return std::nullopt;
    return value;
}

void write_dword(HKEY key, const wchar_t* name, DWORD value) {
    RegSetValueExW(
        key, name, 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&value), sizeof(value)
    );
}

std::optional<double> read_double_string(HKEY key, const wchar_t* name) {
    wchar_t buffer[96]{};
    DWORD size = sizeof(buffer);
    const LSTATUS status = RegGetValueW(
        key, nullptr, name, RRF_RT_REG_SZ, nullptr, buffer, &size
    );
    if (status != ERROR_SUCCESS) return std::nullopt;

    wchar_t* end = nullptr;
    const double value = std::wcstod(buffer, &end);
    if (end == buffer || (end && *end != L'\0') || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

void write_double_string(HKEY key, const wchar_t* name, double value) {
    wchar_t buffer[64]{};
    swprintf_s(buffer, L"%.17g", value);
    const DWORD bytes = static_cast<DWORD>((wcslen(buffer) + 1) * sizeof(wchar_t));
    RegSetValueExW(
        key, name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(buffer), bytes
    );
}

}  // namespace

RegistrySettingsStore::RegistrySettingsStore() {
    HKEY key = nullptr;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            kRegistryPath,
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_QUERY_VALUE | KEY_SET_VALUE,
            nullptr,
            &key,
            nullptr
        ) == ERROR_SUCCESS) {
        key_ = key;
    }
}

RegistrySettingsStore::~RegistrySettingsStore() {
    if (key_) RegCloseKey(key_);
}

StoredSettings RegistrySettingsStore::load() {
    StoredSettings result{};
    if (!key_) return result;

    if (const auto work = read_dword(key_, kWorkInterval); work && contains(kAllowedWorkIntervals, *work)) {
        result.schedule.work_interval = static_cast<Seconds>(*work);
    }
    if (const auto duration = read_dword(key_, kBreakDuration); duration && contains(kAllowedBreakDurations, *duration)) {
        result.schedule.break_duration = static_cast<Seconds>(*duration);
    }

    write_dword(key_, kSchemaVersion, kCurrentSchemaVersion);
    write_dword(key_, kWorkInterval, static_cast<DWORD>(result.schedule.effective_work_interval()));
    write_dword(key_, kBreakDuration, static_cast<DWORD>(result.schedule.effective_break_duration()));
    RegDeleteValueW(key_, kLegacyLaunchAtLogin);

    DWORD pause_type = 0;
    DWORD pause_size = 0;
    const LSTATUS pause_status = RegQueryValueExW(
        key_, kPauseUntil, nullptr, &pause_type, nullptr, &pause_size
    );
    if (pause_status == ERROR_SUCCESS) {
        const auto pause = read_double_string(key_, kPauseUntil);
        if (pause) {
            result.pause_until = *pause;
        } else {
            RegDeleteValueW(key_, kPauseUntil);
        }
    }

    return result;
}

void RegistrySettingsStore::persist_schedule(const SessionSchedule& schedule) {
    if (!key_) return;

    const DWORD work = static_cast<DWORD>(schedule.work_interval);
    const DWORD duration = static_cast<DWORD>(schedule.break_duration);
    write_dword(
        key_,
        kWorkInterval,
        contains(kAllowedWorkIntervals, work) ? work : kAllowedWorkIntervals.front()
    );
    write_dword(
        key_,
        kBreakDuration,
        contains(kAllowedBreakDurations, duration) ? duration : kAllowedBreakDurations.front()
    );
}

void RegistrySettingsStore::persist_pause_until(std::optional<WallInstant> value) {
    if (!key_) return;
    if (!value || !std::isfinite(*value)) {
        RegDeleteValueW(key_, kPauseUntil);
        return;
    }
    write_double_string(key_, kPauseUntil, *value);
}

}  // namespace blinkrest::win32
