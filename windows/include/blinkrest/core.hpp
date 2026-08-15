#pragma once
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace blinkrest {

using Seconds = double;
using MonotonicInstant = double;
using WallInstant = double;

enum class BreakStage {
    look_far,
    blink,
    close_eyes,
};

struct BreakSegment {
    BreakStage stage;
    Seconds starts_at;
    Seconds duration;

    Seconds ends_at() const;
    bool operator==(const BreakSegment&) const = default;
};

struct BreakPhase {
    BreakStage stage;
    Seconds elapsed;
    Seconds remaining;
    double progress;

    bool operator==(const BreakPhase&) const = default;
};

struct BreakPlan {
    Seconds total_duration;
    std::vector<BreakSegment> segments;

    BreakPhase phase(Seconds elapsed) const;
    bool operator==(const BreakPlan&) const = default;
};

BreakPlan make_break_plan(Seconds total_duration);

struct SessionSchedule {
    Seconds work_interval = 20 * 60;
    Seconds break_duration = 30;
    Seconds warning_duration = 5;

    Seconds effective_work_interval() const;
    Seconds effective_break_duration() const;
    Seconds effective_warning_duration() const;
    bool operator==(const SessionSchedule&) const = default;
};

struct BreakSession {
    MonotonicInstant started_at;
    MonotonicInstant ends_at;
    Seconds duration_snapshot;

    Seconds elapsed(MonotonicInstant now) const;
    Seconds remaining(MonotonicInstant now) const;
    double progress(MonotonicInstant now) const;
    BreakPhase phase(MonotonicInstant now) const;
    bool operator==(const BreakSession&) const = default;
};

enum class SuspensionReason : std::uint8_t {
    system_sleep = 1,
    screen_sleep = 2,
    inactive_session = 4,
};

using SuspensionReasons = std::uint8_t;

constexpr SuspensionReasons reason_bit(SuspensionReason reason) {
    return static_cast<SuspensionReasons>(reason);
}

bool has_reason(SuspensionReasons reasons, SuspensionReason reason);
SuspensionReasons add_reason(SuspensionReasons reasons, SuspensionReason reason);
SuspensionReasons remove_reason(SuspensionReasons reasons, SuspensionReason reason);

struct Running {
    MonotonicInstant deadline;
    bool operator==(const Running&) const = default;
};

struct Warning {
    MonotonicInstant break_starts_at;
    bool operator==(const Warning&) const = default;
};

struct Breaking {
    BreakSession session;
    bool operator==(const Breaking&) const = default;
};

struct Paused {
    WallInstant until;
    bool operator==(const Paused&) const = default;
};

struct Suspended {
    SuspensionReasons reasons = 0;
    std::optional<WallInstant> pause_until;
    bool operator==(const Suspended&) const = default;
};

using SessionState = std::variant<Running, Warning, Breaking, Paused, Suspended>;

enum class EventKind {
    launch,
    tick,
    start_break_now,
    escape_hold_completed,
    pause,
    resume,
    work_interval_changed,
    break_duration_changed,
    system_will_sleep,
    screens_did_sleep,
    session_did_resign_active,
    system_did_wake,
    screens_did_wake,
    session_did_become_active,
    presentation_context_changed,
    suspension_resume_debounce_elapsed,
    displays_changed,
};

struct SessionEvent {
    EventKind kind;
    std::optional<WallInstant> pause_until;

    static SessionEvent pause(WallInstant until);
    bool operator==(const SessionEvent&) const = default;
};

struct SessionContext {
    MonotonicInstant monotonic_now;
    WallInstant wall_now;
    SessionSchedule schedule;
    std::optional<WallInstant> persisted_pause_until;
};

struct ShowWarning {
    int seconds_remaining;
    bool operator==(const ShowWarning&) const = default;
};

struct HideWarning { bool operator==(const HideWarning&) const = default; };
struct CaptureFrontmostApplication { bool operator==(const CaptureFrontmostApplication&) const = default; };
struct DiscardFrontmostApplication { bool operator==(const DiscardFrontmostApplication&) const = default; };
struct RestoreFrontmostApplication { bool operator==(const RestoreFrontmostApplication&) const = default; };

struct PresentOverlay {
    BreakSession session;
    bool operator==(const PresentOverlay&) const = default;
};

struct UpdateOverlay {
    BreakSession session;
    MonotonicInstant at;
    bool operator==(const UpdateOverlay&) const = default;
};

struct DismissOverlay { bool operator==(const DismissOverlay&) const = default; };
struct DiscardCachedOverlayWindowsAfterWake { bool operator==(const DiscardCachedOverlayWindowsAfterWake&) const = default; };
struct CancelEscapeHold { bool operator==(const CancelEscapeHold&) const = default; };
struct ReconcileOverlays { bool operator==(const ReconcileOverlays&) const = default; };

struct PersistPauseUntil {
    std::optional<WallInstant> value;
    bool operator==(const PersistPauseUntil&) const = default;
};

struct ScheduleSuspensionResumeDebounce {
    Seconds delay;
    bool operator==(const ScheduleSuspensionResumeDebounce&) const = default;
};

struct CancelSuspensionResumeDebounce { bool operator==(const CancelSuspensionResumeDebounce&) const = default; };

using SessionEffect = std::variant<
    ShowWarning,
    HideWarning,
    CaptureFrontmostApplication,
    DiscardFrontmostApplication,
    RestoreFrontmostApplication,
    PresentOverlay,
    UpdateOverlay,
    DismissOverlay,
    DiscardCachedOverlayWindowsAfterWake,
    CancelEscapeHold,
    ReconcileOverlays,
    PersistPauseUntil,
    ScheduleSuspensionResumeDebounce,
    CancelSuspensionResumeDebounce
>;

struct SessionTransition {
    SessionState state;
    std::vector<SessionEffect> effects;

    bool operator==(const SessionTransition&) const = default;
};

SessionTransition reduce_session(
    const SessionState& state,
    const SessionEvent& event,
    const SessionContext& context
);

}  // namespace blinkrest
