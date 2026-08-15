#include <blinkrest/core.hpp>
#include <algorithm>
#include <array>
#include <cmath>

namespace blinkrest {
namespace {

constexpr Seconds standard_work_interval = 20 * 60;
constexpr Seconds standard_warning_duration = 5;

int displayed_seconds(Seconds duration) {
    return std::max(1, static_cast<int>(std::ceil(std::max(0.0, duration))));
}

SessionState fresh_running_state(const SessionContext& context) {
    return Running{context.monotonic_now + context.schedule.effective_work_interval()};
}

SessionTransition begin_break(const SessionContext& context) {
    const auto duration = context.schedule.effective_break_duration();
    const BreakSession session{
        context.monotonic_now,
        context.monotonic_now + duration,
        duration,
    };

    return {
        Breaking{session},
        {
            HideWarning{},
            CaptureFrontmostApplication{},
            PresentOverlay{session},
        },
    };
}

SessionTransition finish_break(const SessionContext& context) {
    return {
        fresh_running_state(context),
        {
            CancelEscapeHold{},
            DismissOverlay{},
            RestoreFrontmostApplication{},
        },
    };
}

SessionTransition launch(const SessionContext& context) {
    if (context.persisted_pause_until && *context.persisted_pause_until > context.wall_now) {
        return {Paused{*context.persisted_pause_until}, {}};
    }

    std::vector<SessionEffect> effects;
    if (context.persisted_pause_until) {
        effects.emplace_back(PersistPauseUntil{std::nullopt});
    }
    return {fresh_running_state(context), std::move(effects)};
}

SessionTransition suspend(
    const SessionState& state,
    SuspensionReason reason,
    const SessionContext& context
) {
    if (const auto* suspended = std::get_if<Suspended>(&state)) {
        if (has_reason(suspended->reasons, reason)) {
            return {state, {}};
        }

        auto reasons = add_reason(suspended->reasons, reason);
        std::vector<SessionEffect> effects;
        if (suspended->reasons == 0) {
            effects.emplace_back(CancelSuspensionResumeDebounce{});
        }
        return {Suspended{reasons, suspended->pause_until}, std::move(effects)};
    }

    std::optional<WallInstant> pause_until;
    if (const auto* paused = std::get_if<Paused>(&state); paused && paused->until > context.wall_now) {
        pause_until = paused->until;
    } else if (context.persisted_pause_until && *context.persisted_pause_until > context.wall_now) {
        pause_until = context.persisted_pause_until;
    }

    return {
        Suspended{reason_bit(reason), pause_until},
        {
            HideWarning{},
            CancelEscapeHold{},
            DismissOverlay{},
            DiscardFrontmostApplication{},
            CancelSuspensionResumeDebounce{},
        },
    };
}

SessionTransition end_suspension(const SessionState& state, SuspensionReason reason) {
    const auto* suspended = std::get_if<Suspended>(&state);
    if (!suspended) {
        return {state, {}};
    }

    if (suspended->reasons == 0) {
        return {
            state,
            {ScheduleSuspensionResumeDebounce{0.5}},
        };
    }

    if (!has_reason(suspended->reasons, reason)) {
        return {state, {}};
    }

    const auto reasons = remove_reason(suspended->reasons, reason);
    const SessionState updated = Suspended{reasons, suspended->pause_until};
    if (reasons == 0) {
        return {
            updated,
            {ScheduleSuspensionResumeDebounce{0.5}},
        };
    }
    return {updated, {}};
}

SessionTransition resume_after_suspension(
    const SessionState& state,
    const SessionContext& context
) {
    const auto* suspended = std::get_if<Suspended>(&state);
    if (!suspended || suspended->reasons != 0) {
        return {state, {}};
    }

    if (suspended->pause_until && *suspended->pause_until > context.wall_now) {
        return {Paused{*suspended->pause_until}, {}};
    }

    std::vector<SessionEffect> effects;
    if (suspended->pause_until || context.persisted_pause_until) {
        effects.emplace_back(PersistPauseUntil{std::nullopt});
    }
    return {fresh_running_state(context), std::move(effects)};
}

}  // namespace

Seconds BreakSegment::ends_at() const {
    return starts_at + duration;
}

BreakPhase BreakPlan::phase(Seconds elapsed) const {
    const auto clamped_elapsed = std::clamp(elapsed, 0.0, total_duration);
    const BreakSegment* selected = &segments.back();
    for (const auto& segment : segments) {
        if (clamped_elapsed < segment.ends_at()) {
            selected = &segment;
            break;
        }
    }

    const auto stage_elapsed = std::clamp(
        clamped_elapsed - selected->starts_at,
        0.0,
        selected->duration
    );

    return {
        selected->stage,
        stage_elapsed,
        std::max(0.0, selected->duration - stage_elapsed),
        selected->duration > 0 ? stage_elapsed / selected->duration : 1.0,
    };
}

BreakPlan make_break_plan(Seconds total_duration) {
    std::array<Seconds, 3> durations{};

    if (total_duration == 10) {
        durations = {2, 5, 3};
    } else if (total_duration == 20) {
        durations = {3, 9, 8};
    } else if (
        total_duration == 30 || total_duration == 45 ||
        total_duration == 60 || total_duration == 90
    ) {
        const auto scale = total_duration / 30;
        durations = {3 * scale, 12 * scale, 15 * scale};
    } else {
        durations = {3, 12, 15};
    }

    std::vector<BreakSegment> segments;
    segments.reserve(3);
    Seconds offset = 0;
    const std::array stages{
        BreakStage::look_far,
        BreakStage::blink,
        BreakStage::close_eyes,
    };

    for (std::size_t index = 0; index < stages.size(); ++index) {
        segments.push_back({stages[index], offset, durations[index]});
        offset += durations[index];
    }

    return {offset, std::move(segments)};
}

Seconds SessionSchedule::effective_work_interval() const {
    return std::isfinite(work_interval) && work_interval > 0
        ? work_interval
        : standard_work_interval;
}

Seconds SessionSchedule::effective_break_duration() const {
    return make_break_plan(break_duration).total_duration;
}

Seconds SessionSchedule::effective_warning_duration() const {
    return std::isfinite(warning_duration) && warning_duration >= 0
        ? warning_duration
        : standard_warning_duration;
}

Seconds BreakSession::elapsed(MonotonicInstant now) const {
    return std::clamp(now - started_at, 0.0, duration_snapshot);
}

Seconds BreakSession::remaining(MonotonicInstant now) const {
    return std::clamp(ends_at - now, 0.0, duration_snapshot);
}

double BreakSession::progress(MonotonicInstant now) const {
    if (duration_snapshot <= 0) {
        return 1;
    }
    return std::clamp(elapsed(now) / duration_snapshot, 0.0, 1.0);
}

BreakPhase BreakSession::phase(MonotonicInstant now) const {
    return make_break_plan(duration_snapshot).phase(elapsed(now));
}

bool has_reason(SuspensionReasons reasons, SuspensionReason reason) {
    return (reasons & reason_bit(reason)) != 0;
}

SuspensionReasons add_reason(SuspensionReasons reasons, SuspensionReason reason) {
    return static_cast<SuspensionReasons>(reasons | reason_bit(reason));
}

SuspensionReasons remove_reason(SuspensionReasons reasons, SuspensionReason reason) {
    return static_cast<SuspensionReasons>(reasons & ~reason_bit(reason));
}

SessionEvent SessionEvent::pause(WallInstant until) {
    return {EventKind::pause, until};
}

SessionTransition reduce_session(
    const SessionState& state,
    const SessionEvent& event,
    const SessionContext& context
) {
    switch (event.kind) {
    case EventKind::launch:
        return launch(context);
    case EventKind::system_will_sleep:
        return suspend(state, SuspensionReason::system_sleep, context);
    case EventKind::screens_did_sleep:
        return suspend(state, SuspensionReason::screen_sleep, context);
    case EventKind::session_did_resign_active:
        return suspend(state, SuspensionReason::inactive_session, context);
    case EventKind::system_did_wake: {
        auto transition = end_suspension(state, SuspensionReason::system_sleep);
        transition.effects.insert(
            transition.effects.begin(),
            DiscardCachedOverlayWindowsAfterWake{}
        );
        return transition;
    }
    case EventKind::screens_did_wake: {
        auto transition = end_suspension(state, SuspensionReason::screen_sleep);
        transition.effects.insert(
            transition.effects.begin(),
            DiscardCachedOverlayWindowsAfterWake{}
        );
        return transition;
    }
    case EventKind::session_did_become_active:
        return end_suspension(state, SuspensionReason::inactive_session);
    case EventKind::presentation_context_changed:
        if (const auto* warning = std::get_if<Warning>(&state)) {
            return {
                state,
                {ShowWarning{displayed_seconds(warning->break_starts_at - context.monotonic_now)}},
            };
        }
        if (std::holds_alternative<Breaking>(state)) {
            return {state, {ReconcileOverlays{}}};
        }
        return {state, {}};
    case EventKind::suspension_resume_debounce_elapsed:
        return resume_after_suspension(state, context);
    case EventKind::work_interval_changed:
        if (std::holds_alternative<Running>(state) || std::holds_alternative<Warning>(state)) {
            return {fresh_running_state(context), {HideWarning{}}};
        }
        return {state, {}};
    case EventKind::break_duration_changed:
        return {state, {}};
    case EventKind::displays_changed:
        if (std::holds_alternative<Breaking>(state)) {
            return {state, {ReconcileOverlays{}}};
        }
        return {state, {}};
    case EventKind::tick:
    case EventKind::start_break_now:
    case EventKind::escape_hold_completed:
    case EventKind::pause:
    case EventKind::resume:
        break;
    }

    if (const auto* running = std::get_if<Running>(&state)) {
        if (event.kind == EventKind::tick) {
            if (context.monotonic_now >= running->deadline) {
                return begin_break(context);
            }
            const auto remaining = running->deadline - context.monotonic_now;
            if (remaining <= context.schedule.effective_warning_duration()) {
                return {
                    Warning{running->deadline},
                    {ShowWarning{displayed_seconds(remaining)}},
                };
            }
            return {state, {}};
        }
        if (event.kind == EventKind::start_break_now) {
            return begin_break(context);
        }
        if (event.kind == EventKind::pause && event.pause_until) {
            if (*event.pause_until <= context.wall_now) {
                return {
                    fresh_running_state(context),
                    {HideWarning{}, PersistPauseUntil{std::nullopt}},
                };
            }
            return {
                Paused{*event.pause_until},
                {HideWarning{}, PersistPauseUntil{*event.pause_until}},
            };
        }
    }

    if (const auto* warning = std::get_if<Warning>(&state)) {
        if (event.kind == EventKind::tick) {
            if (context.monotonic_now >= warning->break_starts_at) {
                return begin_break(context);
            }
            return {
                state,
                {ShowWarning{displayed_seconds(warning->break_starts_at - context.monotonic_now)}},
            };
        }
        if (event.kind == EventKind::start_break_now) {
            return begin_break(context);
        }
        if (event.kind == EventKind::pause && event.pause_until) {
            if (*event.pause_until <= context.wall_now) {
                return {
                    fresh_running_state(context),
                    {HideWarning{}, PersistPauseUntil{std::nullopt}},
                };
            }
            return {
                Paused{*event.pause_until},
                {HideWarning{}, PersistPauseUntil{*event.pause_until}},
            };
        }
    }

    if (const auto* breaking = std::get_if<Breaking>(&state)) {
        if (event.kind == EventKind::tick) {
            if (context.monotonic_now >= breaking->session.ends_at) {
                return finish_break(context);
            }
            return {
                state,
                {UpdateOverlay{breaking->session, context.monotonic_now}},
            };
        }
        if (event.kind == EventKind::escape_hold_completed) {
            return finish_break(context);
        }
    }

    if (const auto* paused = std::get_if<Paused>(&state)) {
        if (event.kind == EventKind::tick && context.wall_now >= paused->until) {
            return {
                fresh_running_state(context),
                {PersistPauseUntil{std::nullopt}},
            };
        }
        if (event.kind == EventKind::pause && event.pause_until) {
            if (*event.pause_until <= context.wall_now) {
                return {
                    fresh_running_state(context),
                    {PersistPauseUntil{std::nullopt}},
                };
            }
            return {
                Paused{*event.pause_until},
                {PersistPauseUntil{*event.pause_until}},
            };
        }
        if (event.kind == EventKind::resume) {
            return {
                fresh_running_state(context),
                {PersistPauseUntil{std::nullopt}},
            };
        }
    }

    return {state, {}};
}

}  // namespace blinkrest
