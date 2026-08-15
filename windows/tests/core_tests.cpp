#include <blinkrest/core.hpp>

#include <cmath>
#include <cstdlib>
#include <limits>
#include <vector>

using namespace blinkrest;

namespace {

constexpr WallInstant wall_epoch = 1'000'000;

void check(bool condition) {
    if (!condition) {
        std::abort();
    }
}

SessionSchedule test_schedule(Seconds work = 1200, Seconds rest = 20, Seconds warning = 5) {
    return {work, rest, warning};
}

SessionContext context(
    MonotonicInstant monotonic = 0,
    WallInstant wall = wall_epoch,
    SessionSchedule schedule = test_schedule(),
    std::optional<WallInstant> pause_until = std::nullopt
) {
    return {monotonic, wall, schedule, pause_until};
}

SessionEvent event(EventKind kind) {
    return {kind, std::nullopt};
}

void test_break_protocol() {
    const std::vector<std::pair<Seconds, std::vector<Seconds>>> cases{
        {10, {2, 5, 3}},
        {20, {3, 9, 8}},
        {30, {3, 12, 15}},
        {45, {4.5, 18, 22.5}},
        {60, {6, 24, 30}},
        {90, {9, 36, 45}},
    };

    for (const auto& [total, durations] : cases) {
        const auto plan = make_break_plan(total);
        check(plan.total_duration == total);
        check(plan.segments.size() == 3);
        check(plan.segments[0].stage == BreakStage::look_far);
        check(plan.segments[1].stage == BreakStage::blink);
        check(plan.segments[2].stage == BreakStage::close_eyes);
        check(plan.segments[0].duration == durations[0]);
        check(plan.segments[1].duration == durations[1]);
        check(plan.segments[2].duration == durations[2]);
    }

    check(make_break_plan(7) == make_break_plan(30));
    check(make_break_plan(std::numeric_limits<double>::quiet_NaN()) == make_break_plan(30));

    const auto ten = make_break_plan(10);
    check(ten.phase(-1).stage == BreakStage::look_far);
    check(ten.phase(1.999).stage == BreakStage::look_far);
    check(ten.phase(2).stage == BreakStage::blink);
    check(ten.phase(6.999).stage == BreakStage::blink);
    check(ten.phase(7).stage == BreakStage::close_eyes);
    check(ten.phase(10).stage == BreakStage::close_eyes);

    const auto phase = make_break_plan(20).phase(7.5);
    check(phase.stage == BreakStage::blink);
    check(std::abs(phase.elapsed - 4.5) < 0.0001);
    check(std::abs(phase.remaining - 4.5) < 0.0001);
    check(std::abs(phase.progress - 0.5) < 0.0001);
}

void test_break_session() {
    const BreakSession session{100, 110, 10};
    check(session.elapsed(90) == 0);
    check(session.remaining(90) == 10);
    check(session.progress(105) == 0.5);
    check(session.elapsed(120) == 10);
    check(session.remaining(120) == 0);
    check(session.progress(120) == 1);
}

void test_launch_and_warning() {
    const SessionState initial = Suspended{};
    const auto launched = reduce_session(initial, event(EventKind::launch), context(10));
    check(launched.state == SessionState{Running{1210}});
    check(launched.effects.empty());

    const auto future = wall_epoch + 60;
    const auto paused = reduce_session(initial, event(EventKind::launch), context(0, wall_epoch, test_schedule(), future));
    check(paused.state == SessionState{Paused{future}});

    const auto expired = wall_epoch - 1;
    const auto cleared = reduce_session(initial, event(EventKind::launch), context(25, wall_epoch, test_schedule(), expired));
    check(cleared.state == SessionState{Running{1225}});
    check(cleared.effects == std::vector<SessionEffect>{PersistPauseUntil{std::nullopt}});

    const auto warning = reduce_session(
        SessionState{Running{100}},
        event(EventKind::tick),
        context(95)
    );
    check(warning.state == SessionState{Warning{100}});
    check(warning.effects == std::vector<SessionEffect>{ShowWarning{5}});
}

void test_break_transitions() {
    const auto delayed = reduce_session(
        SessionState{Running{100}},
        event(EventKind::tick),
        context(140)
    );
    const BreakSession expected{140, 160, 20};
    check(delayed.state == SessionState{Breaking{expected}});
    check(delayed.effects == std::vector<SessionEffect>{
        HideWarning{},
        CaptureFrontmostApplication{},
        PresentOverlay{expected},
    });

    const BreakSession existing{10, 30, 20};
    const auto finished = reduce_session(
        SessionState{Breaking{existing}},
        event(EventKind::tick),
        context(35)
    );
    check(finished.state == SessionState{Running{1235}});
    check(finished.effects == std::vector<SessionEffect>{
        CancelEscapeHold{},
        DismissOverlay{},
        RestoreFrontmostApplication{},
    });

    const auto manual = reduce_session(
        SessionState{Running{500}},
        event(EventKind::start_break_now),
        context(50)
    );
    check(manual.state == SessionState{Breaking{BreakSession{50, 70, 20}}});

    const auto skipped = reduce_session(
        manual.state,
        event(EventKind::escape_hold_completed),
        context(51)
    );
    check(skipped.state == SessionState{Running{1251}});
}

void test_pause_and_setting_changes() {
    const auto until = wall_epoch + 1800;
    const auto paused = reduce_session(
        SessionState{Running{100}},
        SessionEvent::pause(until),
        context()
    );
    check(paused.state == SessionState{Paused{until}});
    check(paused.effects == std::vector<SessionEffect>{
        HideWarning{},
        PersistPauseUntil{until},
    });

    const auto resumed = reduce_session(
        paused.state,
        event(EventKind::resume),
        context(10)
    );
    check(resumed.state == SessionState{Running{1210}});
    check(resumed.effects == std::vector<SessionEffect>{PersistPauseUntil{std::nullopt}});

    const SessionState warning = Warning{100};
    const auto work_changed = reduce_session(
        warning,
        event(EventKind::work_interval_changed),
        context(40, wall_epoch, test_schedule(1800, 30))
    );
    check(work_changed.state == SessionState{Running{1840}});
    check(work_changed.effects == std::vector<SessionEffect>{HideWarning{}});

    const auto break_changed = reduce_session(
        warning,
        event(EventKind::break_duration_changed),
        context(40, wall_epoch, test_schedule(1800, 30))
    );
    check(break_changed.state == warning);
    check(break_changed.effects.empty());

    const SessionState active_break = Breaking{BreakSession{10, 30, 20}};
    const auto active_break_changed = reduce_session(
        active_break,
        event(EventKind::break_duration_changed),
        context(15, wall_epoch, test_schedule(1200, 30))
    );
    check(active_break_changed.state == active_break);
    check(std::get<Breaking>(active_break_changed.state).session.duration_snapshot == 20);
}

void test_suspension_and_presentation_reconciliation() {
    const SessionState running = Running{100};
    const auto slept = reduce_session(
        running,
        event(EventKind::system_will_sleep),
        context()
    );
    check(slept.state == SessionState{Suspended{reason_bit(SuspensionReason::system_sleep), std::nullopt}});
    check(slept.effects == std::vector<SessionEffect>{
        HideWarning{},
        CancelEscapeHold{},
        DismissOverlay{},
        DiscardFrontmostApplication{},
        CancelSuspensionResumeDebounce{},
    });

    const auto both = reduce_session(
        slept.state,
        event(EventKind::screens_did_sleep),
        context()
    );
    const auto both_reasons = add_reason(
        reason_bit(SuspensionReason::system_sleep),
        SuspensionReason::screen_sleep
    );
    check(both.state == SessionState{Suspended{both_reasons, std::nullopt}});

    const auto one_remaining = reduce_session(
        both.state,
        event(EventKind::system_did_wake),
        context()
    );
    check(one_remaining.state == SessionState{Suspended{reason_bit(SuspensionReason::screen_sleep), std::nullopt}});
    check(one_remaining.effects == std::vector<SessionEffect>{DiscardCachedOverlayWindowsAfterWake{}});

    const auto pending = reduce_session(
        one_remaining.state,
        event(EventKind::screens_did_wake),
        context()
    );
    check(pending.state == SessionState{Suspended{0, std::nullopt}});
    check(pending.effects == std::vector<SessionEffect>{
        DiscardCachedOverlayWindowsAfterWake{},
        ScheduleSuspensionResumeDebounce{0.5},
    });

    const auto resumed = reduce_session(
        pending.state,
        event(EventKind::suspension_resume_debounce_elapsed),
        context(50)
    );
    check(resumed.state == SessionState{Running{1250}});

    const SessionState breaking = Breaking{BreakSession{100, 120, 20}};
    const auto desktop_changed = reduce_session(
        breaking,
        event(EventKind::presentation_context_changed),
        context(105)
    );
    check(desktop_changed.effects == std::vector<SessionEffect>{ReconcileOverlays{}});

    const auto displays_changed = reduce_session(
        breaking,
        event(EventKind::displays_changed),
        context(105)
    );
    check(displays_changed.effects == std::vector<SessionEffect>{ReconcileOverlays{}});

    const auto warning_context_changed = reduce_session(
        SessionState{Warning{105}},
        event(EventKind::presentation_context_changed),
        context(102)
    );
    check(warning_context_changed.effects == std::vector<SessionEffect>{ShowWarning{3}});

    const auto running_context_changed = reduce_session(
        SessionState{Running{200}},
        event(EventKind::presentation_context_changed),
        context(105)
    );
    check(running_context_changed.effects.empty());
}

void test_suspension_edge_cases() {
    const auto pause_until = wall_epoch + 3600;
    const auto suspended_pause = reduce_session(
        SessionState{Paused{pause_until}},
        event(EventKind::session_did_resign_active),
        context()
    );
    check(suspended_pause.state == SessionState{
        Suspended{reason_bit(SuspensionReason::inactive_session), pause_until}
    });

    const auto pending_pause = reduce_session(
        suspended_pause.state,
        event(EventKind::session_did_become_active),
        context()
    );
    const auto restored_pause = reduce_session(
        pending_pause.state,
        event(EventKind::suspension_resume_debounce_elapsed),
        context(0, wall_epoch + 10, test_schedule(), pause_until)
    );
    check(restored_pause.state == SessionState{Paused{pause_until}});

    const SessionState pending = Suspended{0, std::nullopt};
    const auto interrupted = reduce_session(
        pending,
        event(EventKind::session_did_resign_active),
        context()
    );
    check(interrupted.state == SessionState{
        Suspended{reason_bit(SuspensionReason::inactive_session), std::nullopt}
    });
    check(interrupted.effects == std::vector<SessionEffect>{
        CancelSuspensionResumeDebounce{}
    });

    const SessionState warning = Warning{100};
    const auto warning_suspended = reduce_session(
        warning,
        event(EventKind::system_will_sleep),
        context()
    );
    const auto duplicate_suspend = reduce_session(
        warning_suspended.state,
        event(EventKind::system_will_sleep),
        context()
    );
    check(duplicate_suspend.effects.empty());

    const SessionState breaking = Breaking{BreakSession{0, 20, 20}};
    const auto break_suspended = reduce_session(
        breaking,
        event(EventKind::session_did_resign_active),
        context()
    );
    check(break_suspended.effects == warning_suspended.effects);

    const auto expired = wall_epoch + 5;
    const SessionState expired_pending = Suspended{0, expired};
    const auto expired_resumed = reduce_session(
        expired_pending,
        event(EventKind::suspension_resume_debounce_elapsed),
        context(30, wall_epoch + 10, test_schedule(), expired)
    );
    check(expired_resumed.state == SessionState{Running{1230}});
    check(expired_resumed.effects == std::vector<SessionEffect>{
        PersistPauseUntil{std::nullopt}
    });
}

void test_timing_changes_preserve_inactive_states() {
    const std::vector<SessionState> states{
        Paused{wall_epoch + 60},
        Breaking{BreakSession{0, 20, 20}},
        Suspended{reason_bit(SuspensionReason::screen_sleep), std::nullopt},
    };

    for (const auto& state : states) {
        const auto work_changed = reduce_session(
            state,
            event(EventKind::work_interval_changed),
            context(10)
        );
        const auto break_changed = reduce_session(
            state,
            event(EventKind::break_duration_changed),
            context(10)
        );
        check(work_changed == SessionTransition{state, {}});
        check(break_changed == SessionTransition{state, {}});
    }
}

void test_wall_clock_does_not_move_monotonic_deadlines() {
    const SessionState running = Running{100};
    const auto running_transition = reduce_session(
        running,
        event(EventKind::tick),
        context(50, wall_epoch + 10'000'000)
    );
    check(running_transition.state == running);
    check(running_transition.effects.empty());

    const BreakSession session{40, 60, 20};
    const SessionState breaking = Breaking{session};
    const auto break_transition = reduce_session(
        breaking,
        event(EventKind::tick),
        context(50, wall_epoch - 10'000'000)
    );
    check(break_transition.state == breaking);
    check(break_transition.effects == std::vector<SessionEffect>{
        UpdateOverlay{session, 50}
    });
}

}  // namespace

int main() {
    test_break_protocol();
    test_break_session();
    test_launch_and_warning();
    test_break_transitions();
    test_pause_and_setting_changes();
    test_suspension_and_presentation_reconciliation();
    test_suspension_edge_cases();
    test_timing_changes_preserve_inactive_states();
    test_wall_clock_does_not_move_monotonic_deadlines();
    return 0;
}
