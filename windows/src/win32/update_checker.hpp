#pragma once

#include <windows.h>

#include <atomic>
#include <memory>
#include <string>

namespace blinkrest::win32 {

enum class UpdateCheckOutcome {
    up_to_date,
    update_available,
    failed,
};

struct UpdateCheckResult {
    UpdateCheckOutcome outcome = UpdateCheckOutcome::failed;
    bool manual = false;
    std::string version;
    std::wstring release_url;
};

class UpdateChecker {
public:
    explicit UpdateChecker(std::string current_version);

    bool check_automatically(HWND notify_window, UINT completion_message, double now_epoch_seconds);
    bool check_manually(HWND notify_window, UINT completion_message);
    bool is_checking() const;
    bool should_prompt_for(const std::string& version) const;
    void mark_prompted(const std::string& version) const;

private:
    bool begin_check(HWND notify_window, UINT completion_message, bool manual);
    bool automatic_check_due(double now_epoch_seconds) const;
    void persist_automatic_check_time(double now_epoch_seconds) const;

    std::string current_version_;
    std::shared_ptr<std::atomic_bool> checking_;
};

}  // namespace blinkrest::win32
