import XCTest
@testable import BlinkRest

@MainActor
final class SessionControllerTests: XCTestCase {
    func testStartLaunchesAndSchedulesOneSecondTicks() {
        let harness = makeHarness(monotonic: 10)

        XCTAssertEqual(
            harness.controller.state,
            .running(deadline: MonotonicInstant(seconds: 1_210))
        )
        XCTAssertEqual(harness.scheduler.tickInterval, 1)
        XCTAssertEqual(harness.scheduler.tickScheduleCount, 1)
        XCTAssertEqual(harness.controller.remainingSeconds, 1_200)
    }

    func testTickCrossesWarningAndBreakBoundariesWithoutDrift() {
        let harness = makeHarness(monotonic: 0)

        harness.time.monotonicNow = MonotonicInstant(seconds: 1_195)
        harness.scheduler.fireTick()
        XCTAssertEqual(
            harness.controller.state,
            .warning(breakStartsAt: MonotonicInstant(seconds: 1_200))
        )
        XCTAssertEqual(harness.warning.shownSeconds, [5])
        XCTAssertEqual(harness.scheduler.tickInterval, 0.1)

        harness.time.monotonicNow = MonotonicInstant(seconds: 1_205)
        harness.scheduler.fireTick()
        guard let session = harness.overlay.presentedSession else {
            return XCTFail("Expected an overlay session")
        }
        XCTAssertEqual(session.startedAt, MonotonicInstant(seconds: 1_205))
        XCTAssertEqual(session.endsAt, MonotonicInstant(seconds: 1_225))
        XCTAssertEqual(harness.frontmost.captureCount, 1)
        XCTAssertEqual(harness.scheduler.tickScheduleCount, 2)

        harness.time.monotonicNow = MonotonicInstant(seconds: 1_225)
        harness.scheduler.fireTick()
        XCTAssertEqual(
            harness.controller.state,
            .running(deadline: MonotonicInstant(seconds: 2_425))
        )
        XCTAssertEqual(harness.overlay.dismissCount, 1)
        XCTAssertEqual(harness.overlay.cancelHoldCount, 1)
        XCTAssertEqual(harness.frontmost.restoreCount, 1)
        XCTAssertEqual(harness.scheduler.tickInterval, 1)
    }

    func testManualBreakPresentsSnapshotAndUpdatesOnTicks() {
        let harness = makeHarness(monotonic: 100)

        harness.controller.takeBreakNow()
        XCTAssertEqual(harness.overlay.presentedSession?.durationSnapshot, 20)

        harness.time.monotonicNow = MonotonicInstant(seconds: 105)
        harness.scheduler.fireTick()
        XCTAssertEqual(harness.overlay.updates.count, 1)
        XCTAssertEqual(harness.overlay.updates.first?.now, MonotonicInstant(seconds: 105))

        harness.controller.escapeHoldCompleted()
        XCTAssertNil(harness.controller.currentBreakSession)
        XCTAssertEqual(
            harness.controller.state,
            .running(deadline: MonotonicInstant(seconds: 1_305))
        )
    }

    func testThirtyMinuteAndOneHourPausesPersistAndResume() {
        let harness = makeHarness(monotonic: 10)

        harness.controller.pause(for: 1_800)
        XCTAssertEqual(
            harness.preferences.persistedPauseUntil,
            harness.time.wallNow.addingTimeInterval(1_800)
        )
        XCTAssertEqual(harness.warning.hideCount, 1)

        harness.controller.resume()
        XCTAssertNil(harness.preferences.persistedPauseUntil)

        harness.controller.pause(for: 3_600)
        let expected = harness.time.wallNow.addingTimeInterval(3_600)
        XCTAssertEqual(harness.controller.state, .paused(until: expected))

        harness.time.wallNow = expected
        harness.time.monotonicNow = MonotonicInstant(seconds: 20)
        harness.scheduler.fireTick()
        XCTAssertNil(harness.preferences.persistedPauseUntil)
        XCTAssertEqual(
            harness.controller.state,
            .running(deadline: MonotonicInstant(seconds: 1_220))
        )
    }

    func testDistinctSettingsEventsHaveDistinctTimingSemantics() {
        let harness = makeHarness(monotonic: 0)
        let originalDeadline = MonotonicInstant(seconds: 1_200)

        harness.time.monotonicNow = MonotonicInstant(seconds: 100)
        harness.preferences.sessionSchedule = SessionSchedule(
            workInterval: 1_800,
            breakDuration: 30
        )
        harness.controller.breakDurationChanged()
        XCTAssertEqual(harness.controller.state, .running(deadline: originalDeadline))

        harness.controller.workIntervalChanged()
        XCTAssertEqual(
            harness.controller.state,
            .running(deadline: MonotonicInstant(seconds: 1_900))
        )
    }

    func testWakeDebounceWaitsForAllSuspensionReasonsAndRunsOnce() {
        let harness = makeHarness(monotonic: 0)

        harness.controller.dispatch(.systemWillSleep)
        harness.controller.dispatch(.screensDidSleep)
        XCTAssertNil(harness.scheduler.tickInterval)

        harness.controller.dispatch(.systemDidWake)
        XCTAssertEqual(harness.scheduler.suspensionScheduleCount, 0)

        harness.controller.dispatch(.screensDidWake)
        XCTAssertEqual(harness.scheduler.suspensionScheduleCount, 1)

        harness.controller.dispatch(.screensDidWake)
        XCTAssertEqual(harness.scheduler.suspensionScheduleCount, 2)
        XCTAssertEqual(harness.scheduler.suspensionCancelCount, 3)

        harness.time.monotonicNow = MonotonicInstant(seconds: 50)
        harness.scheduler.fireSuspensionResume()
        XCTAssertEqual(
            harness.controller.state,
            .running(deadline: MonotonicInstant(seconds: 1_250))
        )
        XCTAssertEqual(harness.scheduler.tickInterval, 1)
    }

    func testValidPauseIsRestoredAfterSuspension() {
        let future = Date(timeIntervalSince1970: 10_000)
        let harness = makeHarness(wall: Date(timeIntervalSince1970: 9_000))

        harness.controller.dispatch(.pause(until: future))
        harness.controller.dispatch(.sessionDidResignActive)
        harness.controller.dispatch(.sessionDidBecomeActive)
        harness.time.wallNow = Date(timeIntervalSince1970: 9_100)
        harness.scheduler.fireSuspensionResume()

        XCTAssertEqual(harness.controller.state, .paused(until: future))
        XCTAssertEqual(harness.preferences.persistedPauseUntil, future)
    }

    func testWallClockJumpDoesNotAlterRunningDeadline() {
        let harness = makeHarness(monotonic: 10)
        let originalState = harness.controller.state

        harness.time.wallNow = harness.time.wallNow.addingTimeInterval(1_000_000)
        harness.scheduler.fireTick()

        XCTAssertEqual(harness.controller.state, originalState)
        XCTAssertEqual(harness.controller.remainingSeconds, 1_200)
    }

    func testDisplayChangesReconcileOnlyWhileBreaking() {
        let harness = makeHarness()

        harness.controller.dispatch(.displaysChanged)
        XCTAssertEqual(harness.overlay.reconcileCount, 0)

        harness.controller.takeBreakNow()
        harness.controller.dispatch(.displaysChanged)
        XCTAssertEqual(harness.overlay.reconcileCount, 1)
    }

    func testRepeatedTicksKeepOneTimerPerCadence() {
        let harness = makeHarness(monotonic: 0)
        XCTAssertEqual(harness.scheduler.tickScheduleCount, 1)

        harness.time.monotonicNow = MonotonicInstant(seconds: 100)
        harness.scheduler.fireTick()
        harness.time.monotonicNow = MonotonicInstant(seconds: 200)
        harness.scheduler.fireTick()
        XCTAssertEqual(harness.scheduler.tickScheduleCount, 1)

        harness.time.monotonicNow = MonotonicInstant(seconds: 1_195)
        harness.scheduler.fireTick()
        XCTAssertEqual(harness.scheduler.tickScheduleCount, 2)

        harness.time.monotonicNow = MonotonicInstant(seconds: 1_195.2)
        harness.scheduler.fireTick()
        harness.time.monotonicNow = MonotonicInstant(seconds: 1_200)
        harness.scheduler.fireTick()
        XCTAssertEqual(harness.scheduler.tickScheduleCount, 2)
    }

    func testSuspendingActiveBreakCleansUpAndWaitsForDebouncedResume() {
        let harness = makeHarness(monotonic: 10)
        harness.controller.takeBreakNow()

        harness.controller.dispatch(.sessionDidResignActive)

        XCTAssertEqual(harness.overlay.cancelHoldCount, 1)
        XCTAssertEqual(harness.overlay.dismissCount, 1)
        XCTAssertEqual(harness.frontmost.discardCount, 1)
        XCTAssertNil(harness.scheduler.tickInterval)

        harness.controller.dispatch(.sessionDidBecomeActive)
        XCTAssertNil(harness.scheduler.tickInterval)
        harness.time.monotonicNow = MonotonicInstant(seconds: 50)
        harness.scheduler.fireSuspensionResume()

        XCTAssertEqual(
            harness.controller.state,
            .running(deadline: MonotonicInstant(seconds: 1_250))
        )
        XCTAssertEqual(harness.scheduler.tickInterval, 1)
    }

    func testStopIsIdempotentAndRemovesTransientPresentation() {
        let harness = makeHarness()
        harness.controller.takeBreakNow()

        harness.controller.stop()
        harness.controller.stop()

        XCTAssertNil(harness.scheduler.tickInterval)
        XCTAssertEqual(harness.overlay.dismissCount, 1)
        XCTAssertEqual(harness.overlay.cancelHoldCount, 1)
        XCTAssertEqual(harness.frontmost.discardCount, 1)
    }

    func testEventsAfterStopCannotRestartTimersOrProduceSideEffects() {
        let harness = makeHarness()
        harness.controller.stop()
        let warningHideCount = harness.warning.hideCount
        let overlayDismissCount = harness.overlay.dismissCount

        harness.controller.dispatch(.workIntervalChanged)
        harness.controller.dispatch(.systemWillSleep)
        harness.controller.dispatch(.systemDidWake)
        harness.controller.dispatch(.tick)

        XCTAssertNil(harness.scheduler.tickInterval)
        XCTAssertEqual(harness.scheduler.suspensionScheduleCount, 0)
        XCTAssertEqual(harness.warning.hideCount, warningHideCount)
        XCTAssertEqual(harness.overlay.dismissCount, overlayDismissCount)
    }

    private func makeHarness(
        monotonic: TimeInterval = 0,
        wall: Date = Date(timeIntervalSince1970: 1_000)
    ) -> ControllerHarness {
        let time = FakeTimeSource(monotonic: monotonic, wall: wall)
        let preferences = FakeSessionPreferences()
        let overlay = FakeOverlayPresenter()
        let warning = FakeWarningPresenter()
        let frontmost = FakeFrontmostApplicationManager()
        let scheduler = FakeSessionScheduler()
        let controller = SessionController(
            timeSource: time,
            scheduleProvider: preferences,
            pausePersistence: preferences,
            overlayPresenter: overlay,
            warningPresenter: warning,
            frontmostApplicationManager: frontmost,
            scheduler: scheduler
        )

        return ControllerHarness(
            controller: controller,
            time: time,
            preferences: preferences,
            overlay: overlay,
            warning: warning,
            frontmost: frontmost,
            scheduler: scheduler
        )
    }
}

@MainActor
private struct ControllerHarness {
    let controller: SessionController
    let time: FakeTimeSource
    let preferences: FakeSessionPreferences
    let overlay: FakeOverlayPresenter
    let warning: FakeWarningPresenter
    let frontmost: FakeFrontmostApplicationManager
    let scheduler: FakeSessionScheduler
}

@MainActor
private final class FakeTimeSource: TimeSource {
    var monotonicNow: MonotonicInstant
    var wallNow: Date

    init(monotonic: TimeInterval, wall: Date) {
        monotonicNow = MonotonicInstant(seconds: monotonic)
        wallNow = wall
    }
}

@MainActor
private final class FakeSessionPreferences: SessionScheduleProviding, PauseUntilPersisting {
    var sessionSchedule = SessionSchedule.standard
    var persistedPauseUntil: Date?
}

@MainActor
private final class FakeOverlayPresenter: OverlayPresenting {
    struct Update: Equatable {
        let session: BreakSession
        let now: MonotonicInstant
    }

    var presentedSession: BreakSession?
    var updates: [Update] = []
    var dismissCount = 0
    var reconcileCount = 0
    var cancelHoldCount = 0

    func present(session: BreakSession) {
        presentedSession = session
    }

    func update(session: BreakSession, at now: MonotonicInstant) {
        updates.append(Update(session: session, now: now))
    }

    func dismiss() {
        dismissCount += 1
        presentedSession = nil
    }

    func reconcileScreens() {
        reconcileCount += 1
    }

    func cancelEscapeHold() {
        cancelHoldCount += 1
    }
}

@MainActor
private final class FakeWarningPresenter: WarningPresenting {
    var shownSeconds: [Int] = []
    var hideCount = 0

    func show(secondsRemaining: Int) {
        shownSeconds.append(secondsRemaining)
    }

    func hide() {
        hideCount += 1
    }
}

@MainActor
private final class FakeFrontmostApplicationManager: FrontmostApplicationManaging {
    var captureCount = 0
    var discardCount = 0
    var restoreCount = 0

    func captureFrontmostApplication() {
        captureCount += 1
    }

    func discardFrontmostApplication() {
        discardCount += 1
    }

    func restoreFrontmostApplication() {
        restoreCount += 1
    }
}

@MainActor
private final class FakeSessionScheduler: SessionScheduling {
    var tickInterval: TimeInterval?
    var tickScheduleCount = 0
    var suspensionScheduleCount = 0
    var suspensionCancelCount = 0

    private var tickHandler: (() -> Void)?
    private var suspensionResumeHandler: (() -> Void)?

    func scheduleTicks(every interval: TimeInterval, handler: @escaping () -> Void) {
        tickInterval = interval
        tickHandler = handler
        tickScheduleCount += 1
    }

    func cancelTicks() {
        tickInterval = nil
        tickHandler = nil
    }

    func scheduleSuspensionResume(after delay: TimeInterval, handler: @escaping () -> Void) {
        XCTAssertEqual(delay, 0.5)
        suspensionResumeHandler = handler
        suspensionScheduleCount += 1
        suspensionCancelCount += 1
    }

    func cancelSuspensionResume() {
        suspensionResumeHandler = nil
        suspensionCancelCount += 1
    }

    func fireTick() {
        tickHandler?()
    }

    func fireSuspensionResume() {
        let handler = suspensionResumeHandler
        suspensionResumeHandler = nil
        handler?()
    }
}
