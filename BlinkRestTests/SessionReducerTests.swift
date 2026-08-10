import XCTest
@testable import BlinkRest

final class SessionReducerTests: XCTestCase {
    private let wallEpoch = Date(timeIntervalSince1970: 1_000_000)
    private let schedule = SessionSchedule(
        workInterval: 1_200,
        breakDuration: 20,
        warningDuration: 5
    )

    func testLaunchStartsFullWorkCycle() {
        let transition = reduce(
            .suspended(reasons: [], pauseUntil: nil),
            .launch,
            monotonic: 10
        )

        XCTAssertEqual(
            transition.state,
            .running(deadline: MonotonicInstant(seconds: 1_210))
        )
        XCTAssertTrue(transition.effects.isEmpty)
    }

    func testLaunchRestoresFuturePauseAndClearsExpiredPause() {
        let future = wallEpoch.addingTimeInterval(60)
        let paused = reduce(
            .suspended(reasons: [], pauseUntil: nil),
            .launch,
            pauseUntil: future
        )
        XCTAssertEqual(paused.state, .paused(until: future))

        let expired = wallEpoch.addingTimeInterval(-1)
        let running = reduce(
            .suspended(reasons: [], pauseUntil: nil),
            .launch,
            monotonic: 25,
            pauseUntil: expired
        )
        XCTAssertEqual(
            running.state,
            .running(deadline: MonotonicInstant(seconds: 1_225))
        )
        XCTAssertEqual(running.effects, [.persistPauseUntil(nil)])
    }

    func testRunningEntersWarningAtFiveSecondBoundary() {
        let deadline = MonotonicInstant(seconds: 100)
        let transition = reduce(.running(deadline: deadline), .tick, monotonic: 95)

        XCTAssertEqual(transition.state, .warning(breakStartsAt: deadline))
        XCTAssertEqual(transition.effects, [.showWarning(secondsRemaining: 5)])
    }

    func testDelayedRunningTickAtOrAfterDeadlineStartsBreakWithoutWarning() {
        for now in [100.0, 140.0] {
            let transition = reduce(
                .running(deadline: MonotonicInstant(seconds: 100)),
                .tick,
                monotonic: now
            )

            guard case let .breaking(startedAt, endsAt, duration) = transition.state else {
                return XCTFail("Expected breaking state")
            }
            XCTAssertEqual(startedAt, MonotonicInstant(seconds: now))
            XCTAssertEqual(endsAt, MonotonicInstant(seconds: now + 20))
            XCTAssertEqual(duration, 20)
            XCTAssertFalse(
                transition.effects.contains { effect in
                    if case .showWarning = effect { return true }
                    return false
                }
            )
        }
    }

    func testWarningTickStartsFullBreakAtObservedTime() {
        let transition = reduce(
            .warning(breakStartsAt: MonotonicInstant(seconds: 100)),
            .tick,
            monotonic: 101
        )

        XCTAssertEqual(
            transition.state,
            .breaking(
                startedAt: MonotonicInstant(seconds: 101),
                endsAt: MonotonicInstant(seconds: 121),
                durationSnapshot: 20
            )
        )
        XCTAssertEqual(transition.effects.first, .hideWarning)
    }

    func testBreakingTickFinishesIntoFreshCycle() {
        let state = SessionState.breaking(
            startedAt: MonotonicInstant(seconds: 10),
            endsAt: MonotonicInstant(seconds: 30),
            durationSnapshot: 20
        )
        let transition = reduce(state, .tick, monotonic: 35)

        XCTAssertEqual(
            transition.state,
            .running(deadline: MonotonicInstant(seconds: 1_235))
        )
        XCTAssertEqual(
            transition.effects,
            [.cancelEscapeHold, .dismissOverlay, .restoreFrontmostApplication]
        )
    }

    func testManualBreakAndEscapeCompletionBothStartFreshCycles() {
        let manual = reduce(
            .running(deadline: MonotonicInstant(seconds: 500)),
            .startBreakNow,
            monotonic: 50
        )
        XCTAssertEqual(
            manual.state,
            .breaking(
                startedAt: MonotonicInstant(seconds: 50),
                endsAt: MonotonicInstant(seconds: 70),
                durationSnapshot: 20
            )
        )

        let skipped = reduce(manual.state, .escapeHoldCompleted, monotonic: 51)
        XCTAssertEqual(
            skipped.state,
            .running(deadline: MonotonicInstant(seconds: 1_251))
        )
    }

    func testPauseResumeAndAutomaticExpiryStartFullCycles() {
        let until = wallEpoch.addingTimeInterval(1_800)
        let paused = reduce(
            .running(deadline: MonotonicInstant(seconds: 100)),
            .pause(until: until)
        )
        XCTAssertEqual(paused.state, .paused(until: until))
        XCTAssertEqual(paused.effects, [.hideWarning, .persistPauseUntil(until)])

        let resumed = reduce(paused.state, .resume, monotonic: 10)
        XCTAssertEqual(
            resumed.state,
            .running(deadline: MonotonicInstant(seconds: 1_210))
        )
        XCTAssertEqual(resumed.effects, [.persistPauseUntil(nil)])

        let expired = reduce(
            paused.state,
            .tick,
            monotonic: 20,
            wall: until
        )
        XCTAssertEqual(
            expired.state,
            .running(deadline: MonotonicInstant(seconds: 1_220))
        )
    }

    func testWorkIntervalChangeRestartsButBreakDurationChangePreservesDeadline() {
        let warning = SessionState.warning(
            breakStartsAt: MonotonicInstant(seconds: 100)
        )
        let changedSchedule = SessionSchedule(
            workInterval: 1_800,
            breakDuration: 30
        )

        let workChanged = reduce(
            warning,
            .workIntervalChanged,
            monotonic: 40,
            schedule: changedSchedule
        )
        XCTAssertEqual(
            workChanged.state,
            .running(deadline: MonotonicInstant(seconds: 1_840))
        )
        XCTAssertEqual(workChanged.effects, [.hideWarning])

        let breakChanged = reduce(
            warning,
            .breakDurationChanged,
            monotonic: 40,
            schedule: changedSchedule
        )
        XCTAssertEqual(breakChanged.state, warning)
        XCTAssertTrue(breakChanged.effects.isEmpty)
    }

    func testCurrentBreakKeepsDurationSnapshotAfterSettingsChanges() {
        let state = SessionState.breaking(
            startedAt: MonotonicInstant(seconds: 10),
            endsAt: MonotonicInstant(seconds: 30),
            durationSnapshot: 20
        )
        let transition = reduce(
            state,
            .breakDurationChanged,
            monotonic: 15,
            schedule: SessionSchedule(workInterval: 1_200, breakDuration: 30)
        )

        XCTAssertEqual(transition.state, state)
        XCTAssertEqual(transition.state.breakSession?.durationSnapshot, 20)
    }

    func testOverlappingSuspensionsResumeOnlyAfterEveryReasonClears() {
        let running = SessionState.running(deadline: MonotonicInstant(seconds: 100))
        let slept = reduce(running, .systemWillSleep)
        XCTAssertEqual(
            slept.state,
            .suspended(reasons: [.systemSleep], pauseUntil: nil)
        )
        XCTAssertTrue(slept.effects.contains(.dismissOverlay))

        let both = reduce(slept.state, .screensDidSleep)
        XCTAssertEqual(
            both.state,
            .suspended(reasons: [.systemSleep, .screenSleep], pauseUntil: nil)
        )

        let oneRemaining = reduce(both.state, .systemDidWake)
        XCTAssertEqual(
            oneRemaining.state,
            .suspended(reasons: [.screenSleep], pauseUntil: nil)
        )
        XCTAssertTrue(oneRemaining.effects.isEmpty)

        let pending = reduce(oneRemaining.state, .screensDidWake)
        XCTAssertEqual(
            pending.state,
            .suspended(reasons: [], pauseUntil: nil)
        )
        XCTAssertEqual(
            pending.effects,
            [.scheduleSuspensionResumeDebounce(delay: 0.5)]
        )

        let resumed = reduce(
            pending.state,
            .suspensionResumeDebounceElapsed,
            monotonic: 50
        )
        XCTAssertEqual(
            resumed.state,
            .running(deadline: MonotonicInstant(seconds: 1_250))
        )
    }

    func testValidPauseSurvivesSuspensionAndWakeDebounce() {
        let until = wallEpoch.addingTimeInterval(3_600)
        let suspended = reduce(.paused(until: until), .sessionDidResignActive)
        XCTAssertEqual(
            suspended.state,
            .suspended(reasons: [.inactiveSession], pauseUntil: until)
        )

        let pending = reduce(suspended.state, .sessionDidBecomeActive)
        let resumed = reduce(
            pending.state,
            .suspensionResumeDebounceElapsed,
            wall: wallEpoch.addingTimeInterval(10),
            pauseUntil: until
        )
        XCTAssertEqual(resumed.state, .paused(until: until))
    }

    func testSuspensionDuringWakeDebounceCancelsPendingResume() {
        let pending = SessionState.suspended(reasons: [], pauseUntil: nil)
        let transition = reduce(pending, .sessionDidResignActive)

        XCTAssertEqual(
            transition.state,
            .suspended(reasons: [.inactiveSession], pauseUntil: nil)
        )
        XCTAssertEqual(transition.effects, [.cancelSuspensionResumeDebounce])
    }

    func testSuspendingWarningOrBreakEmitsCleanupOnlyOnce() {
        let warning = SessionState.warning(
            breakStartsAt: MonotonicInstant(seconds: 100)
        )
        let warningSuspended = reduce(warning, .systemWillSleep)
        XCTAssertEqual(
            warningSuspended.effects,
            [
                .hideWarning,
                .cancelEscapeHold,
                .dismissOverlay,
                .discardFrontmostApplication,
                .cancelSuspensionResumeDebounce
            ]
        )
        XCTAssertTrue(reduce(warningSuspended.state, .systemWillSleep).effects.isEmpty)

        let breaking = SessionState.breaking(
            startedAt: .zero,
            endsAt: MonotonicInstant(seconds: 20),
            durationSnapshot: 20
        )
        let breakSuspended = reduce(breaking, .sessionDidResignActive)
        XCTAssertEqual(
            breakSuspended.state,
            .suspended(reasons: [.inactiveSession], pauseUntil: nil)
        )
        XCTAssertEqual(breakSuspended.effects, warningSuspended.effects)
    }

    func testExpiredPauseAtSuspensionResumeIsClearedBeforeFreshCycle() {
        let expired = wallEpoch.addingTimeInterval(5)
        let pending = SessionState.suspended(reasons: [], pauseUntil: expired)
        let transition = reduce(
            pending,
            .suspensionResumeDebounceElapsed,
            monotonic: 30,
            wall: wallEpoch.addingTimeInterval(10),
            pauseUntil: expired
        )

        XCTAssertEqual(
            transition.state,
            .running(deadline: MonotonicInstant(seconds: 1_230))
        )
        XCTAssertEqual(transition.effects, [.persistPauseUntil(nil)])
    }

    func testTimingChangesPreservePausedBreakingAndSuspendedStates() {
        let states: [SessionState] = [
            .paused(until: wallEpoch.addingTimeInterval(60)),
            .breaking(
                startedAt: .zero,
                endsAt: MonotonicInstant(seconds: 20),
                durationSnapshot: 20
            ),
            .suspended(reasons: [.screenSleep], pauseUntil: nil)
        ]

        for state in states {
            let workChanged = reduce(state, .workIntervalChanged, monotonic: 10)
            let breakChanged = reduce(state, .breakDurationChanged, monotonic: 10)
            XCTAssertEqual(workChanged, SessionTransition(state: state))
            XCTAssertEqual(breakChanged, SessionTransition(state: state))
        }
    }

    func testDisplayChangeOnlyReconcilesDuringBreak() {
        let breaking = SessionState.breaking(
            startedAt: .zero,
            endsAt: MonotonicInstant(seconds: 20),
            durationSnapshot: 20
        )
        XCTAssertEqual(
            reduce(breaking, .displaysChanged).effects,
            [.reconcileOverlays]
        )
        XCTAssertTrue(
            reduce(.running(deadline: MonotonicInstant(seconds: 20)), .displaysChanged)
                .effects.isEmpty
        )
    }

    func testActiveSpaceChangeReassertsWarningAndBreakPresentation() {
        let warning = reduce(
            .warning(breakStartsAt: MonotonicInstant(seconds: 105)),
            .activeSpaceDidChange,
            monotonic: 102
        )
        XCTAssertEqual(warning.effects, [.showWarning(secondsRemaining: 3)])

        let breaking = SessionState.breaking(
            startedAt: MonotonicInstant(seconds: 100),
            endsAt: MonotonicInstant(seconds: 120),
            durationSnapshot: 20
        )
        XCTAssertEqual(
            reduce(breaking, .activeSpaceDidChange, monotonic: 105).effects,
            [.reconcileOverlays]
        )

        XCTAssertTrue(
            reduce(
                .running(deadline: MonotonicInstant(seconds: 200)),
                .activeSpaceDidChange,
                monotonic: 105
            ).effects.isEmpty
        )
    }

    func testWallClockChangesDoNotMoveMonotonicWorkDeadline() {
        let state = SessionState.running(deadline: MonotonicInstant(seconds: 100))
        let transition = reduce(
            state,
            .tick,
            monotonic: 50,
            wall: wallEpoch.addingTimeInterval(10_000_000)
        )

        XCTAssertEqual(transition.state, state)

        let breakState = SessionState.breaking(
            startedAt: MonotonicInstant(seconds: 40),
            endsAt: MonotonicInstant(seconds: 60),
            durationSnapshot: 20
        )
        let breakTransition = reduce(
            breakState,
            .tick,
            monotonic: 50,
            wall: wallEpoch.addingTimeInterval(-10_000_000)
        )
        XCTAssertEqual(breakTransition.state, breakState)
        XCTAssertEqual(
            breakTransition.effects,
            [
                .updateOverlay(
                    session: tryBreakSession(breakState),
                    at: MonotonicInstant(seconds: 50)
                )
            ]
        )
    }

    private func tryBreakSession(_ state: SessionState) -> BreakSession {
        guard let session = state.breakSession else {
            XCTFail("Expected a break session")
            return BreakSession(startedAt: .zero, endsAt: .zero, durationSnapshot: 20)
        }
        return session
    }

    private func reduce(
        _ state: SessionState,
        _ event: SessionEvent,
        monotonic: TimeInterval = 0,
        wall: Date? = nil,
        schedule: SessionSchedule? = nil,
        pauseUntil: Date? = nil
    ) -> SessionTransition {
        SessionReducer.reduce(
            state: state,
            event: event,
            context: SessionContext(
                monotonicNow: MonotonicInstant(seconds: monotonic),
                wallNow: wall ?? wallEpoch,
                schedule: schedule ?? self.schedule,
                persistedPauseUntil: pauseUntil
            )
        )
    }
}
