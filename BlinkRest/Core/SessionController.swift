import Combine
import Foundation
import OSLog

@MainActor
protocol SessionScheduleProviding: AnyObject {
    var sessionSchedule: SessionSchedule { get }
}

@MainActor
protocol PauseUntilPersisting: AnyObject {
    var persistedPauseUntil: Date? { get set }
}

@MainActor
protocol OverlayPresenting: AnyObject {
    func present(session: BreakSession)
    func update(session: BreakSession, at now: MonotonicInstant)
    func dismiss()
    func reconcileScreens()
    func cancelEscapeHold()
}

@MainActor
protocol WarningPresenting: AnyObject {
    func show(secondsRemaining: Int)
    func hide()
}

@MainActor
protocol FrontmostApplicationManaging: AnyObject {
    func captureFrontmostApplication()
    func discardFrontmostApplication()
    func restoreFrontmostApplication()
}

@MainActor
protocol SessionScheduling: AnyObject {
    func scheduleTicks(every interval: TimeInterval, handler: @escaping () -> Void)
    func cancelTicks()
    func scheduleSuspensionResume(after delay: TimeInterval, handler: @escaping () -> Void)
    func cancelSuspensionResume()
}

@MainActor
final class RunLoopSessionScheduler: NSObject, SessionScheduling {
    private var tickTimer: Timer?
    private var suspensionResumeTimer: Timer?
    private var tickHandler: (() -> Void)?
    private var suspensionResumeHandler: (() -> Void)?

    func scheduleTicks(every interval: TimeInterval, handler: @escaping () -> Void) {
        cancelTicks()
        tickHandler = handler

        let timer = Timer(
            timeInterval: interval,
            target: self,
            selector: #selector(tickTimerDidFire),
            userInfo: nil,
            repeats: true
        )
        RunLoop.main.add(timer, forMode: .common)
        tickTimer = timer
    }

    func cancelTicks() {
        tickTimer?.invalidate()
        tickTimer = nil
        tickHandler = nil
    }

    func scheduleSuspensionResume(after delay: TimeInterval, handler: @escaping () -> Void) {
        cancelSuspensionResume()
        suspensionResumeHandler = handler

        let timer = Timer(
            timeInterval: delay,
            target: self,
            selector: #selector(suspensionResumeTimerDidFire),
            userInfo: nil,
            repeats: false
        )
        RunLoop.main.add(timer, forMode: .common)
        suspensionResumeTimer = timer
    }

    func cancelSuspensionResume() {
        suspensionResumeTimer?.invalidate()
        suspensionResumeTimer = nil
        suspensionResumeHandler = nil
    }

    @objc private func tickTimerDidFire() {
        tickHandler?()
    }

    @objc private func suspensionResumeTimerDidFire() {
        suspensionResumeTimer = nil
        let handler = suspensionResumeHandler
        suspensionResumeHandler = nil
        handler?()
    }
}

@MainActor
final class SessionController: ObservableObject {
    @Published private(set) var state: SessionState
    @Published private(set) var currentMonotonicTime: MonotonicInstant
    @Published private(set) var currentWallTime: Date

    private let timeSource: TimeSource
    private let scheduleProvider: SessionScheduleProviding
    private let pausePersistence: PauseUntilPersisting
    private let overlayPresenter: OverlayPresenting
    private let warningPresenter: WarningPresenting
    private let frontmostApplicationManager: FrontmostApplicationManaging
    private let scheduler: SessionScheduling
    private let logger = Logger(
        subsystem: Bundle.main.bundleIdentifier ?? "com.fynxiu.BlinkRest",
        category: "scheduler"
    )

    private var isStarted = false
    private var scheduledTickInterval: TimeInterval?

    init(
        timeSource: TimeSource,
        scheduleProvider: SessionScheduleProviding,
        pausePersistence: PauseUntilPersisting,
        overlayPresenter: OverlayPresenting,
        warningPresenter: WarningPresenting,
        frontmostApplicationManager: FrontmostApplicationManaging,
        scheduler: SessionScheduling,
        automaticallyStarts: Bool = true
    ) {
        self.timeSource = timeSource
        self.scheduleProvider = scheduleProvider
        self.pausePersistence = pausePersistence
        self.overlayPresenter = overlayPresenter
        self.warningPresenter = warningPresenter
        self.frontmostApplicationManager = frontmostApplicationManager
        self.scheduler = scheduler

        let monotonicNow = timeSource.monotonicNow
        let wallNow = timeSource.wallNow
        state = .suspended(
            reasons: [],
            pauseUntil: pausePersistence.persistedPauseUntil
        )
        currentMonotonicTime = monotonicNow
        currentWallTime = wallNow

        if automaticallyStarts {
            start()
        }
    }

    var remainingSeconds: Int {
        state.remainingSeconds(
            monotonicNow: currentMonotonicTime,
            wallNow: currentWallTime
        )
    }

    var currentBreakSession: BreakSession? {
        state.breakSession
    }

    var currentBreakPhase: BreakPhase? {
        state.breakSession?.phase(at: currentMonotonicTime)
    }

    var workCycleProgress: Double {
        let deadline: MonotonicInstant
        switch state {
        case let .running(value), let .warning(value):
            deadline = value
        default:
            return 0
        }

        let duration = scheduleProvider.sessionSchedule.effectiveWorkInterval
        guard duration > 0 else { return 0 }
        let remaining = max(0, currentMonotonicTime.duration(to: deadline))
        return min(max(0, 1 - (remaining / duration)), 1)
    }

    func start() {
        guard !isStarted else { return }
        isStarted = true
        dispatch(.launch)
    }

    func stop() {
        guard isStarted else { return }
        isStarted = false
        scheduler.cancelTicks()
        scheduler.cancelSuspensionResume()
        scheduledTickInterval = nil
        warningPresenter.hide()
        overlayPresenter.cancelEscapeHold()
        overlayPresenter.dismiss()
        frontmostApplicationManager.discardFrontmostApplication()
    }

    func dispatch(_ event: SessionEvent) {
        guard isStarted else { return }

        let context = makeContext()
        currentMonotonicTime = context.monotonicNow
        currentWallTime = context.wallNow

        let transition = SessionReducer.reduce(
            state: state,
            event: event,
            context: context
        )
        let previousState = state
        state = transition.state
        if state != previousState {
            logger.debug("Session state changed to \(self.state.logName, privacy: .public)")
        }
        perform(transition.effects)
        updateTickSchedule()
    }

    func takeBreakNow() {
        dispatch(.startBreakNow)
    }

    func pause(for duration: TimeInterval) {
        dispatch(.pause(until: timeSource.wallNow.addingTimeInterval(duration)))
    }

    func resume() {
        dispatch(.resume)
    }

    func escapeHoldCompleted() {
        dispatch(.escapeHoldCompleted)
    }

    func workIntervalChanged() {
        dispatch(.workIntervalChanged)
    }

    func breakDurationChanged() {
        dispatch(.breakDurationChanged)
    }

    private func makeContext() -> SessionContext {
        SessionContext(
            monotonicNow: timeSource.monotonicNow,
            wallNow: timeSource.wallNow,
            schedule: scheduleProvider.sessionSchedule,
            persistedPauseUntil: pausePersistence.persistedPauseUntil
        )
    }

    private func perform(_ effects: [SessionEffect]) {
        for effect in effects {
            switch effect {
            case let .showWarning(secondsRemaining):
                warningPresenter.show(secondsRemaining: secondsRemaining)
            case .hideWarning:
                warningPresenter.hide()
            case .captureFrontmostApplication:
                frontmostApplicationManager.captureFrontmostApplication()
            case .discardFrontmostApplication:
                frontmostApplicationManager.discardFrontmostApplication()
            case .restoreFrontmostApplication:
                frontmostApplicationManager.restoreFrontmostApplication()
            case let .presentOverlay(session):
                overlayPresenter.present(session: session)
            case let .updateOverlay(session, now):
                overlayPresenter.update(session: session, at: now)
            case .dismissOverlay:
                overlayPresenter.dismiss()
            case .cancelEscapeHold:
                overlayPresenter.cancelEscapeHold()
            case .reconcileOverlays:
                overlayPresenter.reconcileScreens()
            case let .persistPauseUntil(pauseUntil):
                pausePersistence.persistedPauseUntil = pauseUntil
            case let .scheduleSuspensionResumeDebounce(delay):
                scheduler.scheduleSuspensionResume(after: delay) { [weak self] in
                    self?.dispatch(.suspensionResumeDebounceElapsed)
                }
            case .cancelSuspensionResumeDebounce:
                scheduler.cancelSuspensionResume()
            }
        }
    }

    private func updateTickSchedule() {
        let desiredInterval: TimeInterval?
        switch state {
        case .running, .paused:
            desiredInterval = 1
        case .warning, .breaking:
            desiredInterval = 0.1
        case .suspended:
            desiredInterval = nil
        }

        guard desiredInterval != scheduledTickInterval else { return }
        scheduler.cancelTicks()
        scheduledTickInterval = desiredInterval

        if let desiredInterval {
            scheduler.scheduleTicks(every: desiredInterval) { [weak self] in
                self?.dispatch(.tick)
            }
        }
    }
}

private extension SessionState {
    var logName: String {
        switch self {
        case .running:
            "running"
        case .warning:
            "warning"
        case .breaking:
            "breaking"
        case .paused:
            "paused"
        case .suspended:
            "suspended"
        }
    }
}
