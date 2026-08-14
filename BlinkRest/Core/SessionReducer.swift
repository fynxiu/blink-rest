import Foundation

struct SessionContext: Equatable, Sendable {
    let monotonicNow: MonotonicInstant
    let wallNow: Date
    let schedule: SessionSchedule
    let persistedPauseUntil: Date?
}

enum SessionEffect: Equatable, Sendable {
    case showWarning(secondsRemaining: Int)
    case hideWarning
    case captureFrontmostApplication
    case discardFrontmostApplication
    case restoreFrontmostApplication
    case presentOverlay(session: BreakSession)
    case updateOverlay(session: BreakSession, at: MonotonicInstant)
    case dismissOverlay
    case discardCachedOverlayWindowsAfterWake
    case cancelEscapeHold
    case reconcileOverlays
    case persistPauseUntil(Date?)
    case scheduleSuspensionResumeDebounce(delay: TimeInterval)
    case cancelSuspensionResumeDebounce
}

struct SessionTransition: Equatable, Sendable {
    let state: SessionState
    let effects: [SessionEffect]

    init(state: SessionState, effects: [SessionEffect] = []) {
        self.state = state
        self.effects = effects
    }
}

enum SessionReducer {
    static func reduce(
        state: SessionState,
        event: SessionEvent,
        context: SessionContext
    ) -> SessionTransition {
        switch event {
        case .launch:
            return launch(context: context)
        case .systemWillSleep:
            return suspend(state: state, reason: .systemSleep, context: context)
        case .screensDidSleep:
            return suspend(state: state, reason: .screenSleep, context: context)
        case .sessionDidResignActive:
            return suspend(state: state, reason: .inactiveSession, context: context)
        case .systemDidWake:
            let transition = endSuspension(state: state, reason: .systemSleep)
            return SessionTransition(
                state: transition.state,
                effects: [.discardCachedOverlayWindowsAfterWake] + transition.effects
            )
        case .screensDidWake:
            let transition = endSuspension(state: state, reason: .screenSleep)
            return SessionTransition(
                state: transition.state,
                effects: [.discardCachedOverlayWindowsAfterWake] + transition.effects
            )
        case .sessionDidBecomeActive:
            return endSuspension(state: state, reason: .inactiveSession)
        case .activeSpaceDidChange:
            switch state {
            case let .warning(breakStartsAt):
                let remaining = context.monotonicNow.duration(to: breakStartsAt)
                return SessionTransition(
                    state: state,
                    effects: [.showWarning(secondsRemaining: displayedSeconds(remaining))]
                )
            case .breaking:
                return SessionTransition(state: state, effects: [.reconcileOverlays])
            case .running, .paused, .suspended:
                return SessionTransition(state: state)
            }
        case .suspensionResumeDebounceElapsed:
            return resumeAfterSuspension(state: state, context: context)
        case .workIntervalChanged:
            return workIntervalChanged(state: state, context: context)
        case .breakDurationChanged:
            return SessionTransition(state: state)
        case .displaysChanged:
            if state.breakSession != nil {
                return SessionTransition(state: state, effects: [.reconcileOverlays])
            }
            return SessionTransition(state: state)
        case .tick, .startBreakNow, .escapeHoldCompleted, .pause, .resume:
            break
        }

        switch (state, event) {
        case let (.running(deadline), .tick):
            if context.monotonicNow >= deadline {
                return beginBreak(context: context)
            }

            let remaining = context.monotonicNow.duration(to: deadline)
            if remaining <= context.schedule.effectiveWarningDuration {
                return SessionTransition(
                    state: .warning(breakStartsAt: deadline),
                    effects: [.showWarning(secondsRemaining: displayedSeconds(remaining))]
                )
            }

            return SessionTransition(state: state)

        case let (.warning(breakStartsAt), .tick):
            if context.monotonicNow >= breakStartsAt {
                return beginBreak(context: context)
            }

            let remaining = context.monotonicNow.duration(to: breakStartsAt)
            return SessionTransition(
                state: state,
                effects: [.showWarning(secondsRemaining: displayedSeconds(remaining))]
            )

        case let (.breaking(_, endsAt, _), .tick):
            if context.monotonicNow >= endsAt {
                return finishBreak(context: context)
            }

            guard let session = state.breakSession else {
                return SessionTransition(state: state)
            }
            return SessionTransition(
                state: state,
                effects: [.updateOverlay(session: session, at: context.monotonicNow)]
            )

        case let (.paused(until), .tick):
            if context.wallNow >= until {
                return SessionTransition(
                    state: freshRunningState(context: context),
                    effects: [.persistPauseUntil(nil)]
                )
            }
            return SessionTransition(state: state)

        case (.running, .startBreakNow), (.warning, .startBreakNow):
            return beginBreak(context: context)

        case (.breaking, .escapeHoldCompleted):
            return finishBreak(context: context)

        case (.running, let .pause(until)), (.warning, let .pause(until)):
            guard until > context.wallNow else {
                return SessionTransition(
                    state: freshRunningState(context: context),
                    effects: [.hideWarning, .persistPauseUntil(nil)]
                )
            }
            return SessionTransition(
                state: .paused(until: until),
                effects: [.hideWarning, .persistPauseUntil(until)]
            )

        case (.paused, let .pause(until)):
            guard until > context.wallNow else {
                return SessionTransition(
                    state: freshRunningState(context: context),
                    effects: [.persistPauseUntil(nil)]
                )
            }
            return SessionTransition(
                state: .paused(until: until),
                effects: [.persistPauseUntil(until)]
            )

        case (.paused, .resume):
            return SessionTransition(
                state: freshRunningState(context: context),
                effects: [.persistPauseUntil(nil)]
            )

        default:
            return SessionTransition(state: state)
        }
    }

    private static func launch(context: SessionContext) -> SessionTransition {
        if let pauseUntil = context.persistedPauseUntil, pauseUntil > context.wallNow {
            return SessionTransition(state: .paused(until: pauseUntil))
        }

        var effects: [SessionEffect] = []
        if context.persistedPauseUntil != nil {
            effects.append(.persistPauseUntil(nil))
        }
        return SessionTransition(state: freshRunningState(context: context), effects: effects)
    }

    private static func workIntervalChanged(
        state: SessionState,
        context: SessionContext
    ) -> SessionTransition {
        switch state {
        case .running, .warning:
            return SessionTransition(
                state: freshRunningState(context: context),
                effects: [.hideWarning]
            )
        case .breaking, .paused, .suspended:
            return SessionTransition(state: state)
        }
    }

    private static func beginBreak(context: SessionContext) -> SessionTransition {
        let duration = context.schedule.effectiveBreakDuration
        let session = BreakSession(
            startedAt: context.monotonicNow,
            endsAt: context.monotonicNow.advanced(by: duration),
            durationSnapshot: duration
        )
        return SessionTransition(
            state: .breaking(
                startedAt: session.startedAt,
                endsAt: session.endsAt,
                durationSnapshot: session.durationSnapshot
            ),
            effects: [
                .hideWarning,
                .captureFrontmostApplication,
                .presentOverlay(session: session)
            ]
        )
    }

    private static func finishBreak(context: SessionContext) -> SessionTransition {
        SessionTransition(
            state: freshRunningState(context: context),
            effects: [
                .cancelEscapeHold,
                .dismissOverlay,
                .restoreFrontmostApplication
            ]
        )
    }

    private static func suspend(
        state: SessionState,
        reason: SuspensionReason,
        context: SessionContext
    ) -> SessionTransition {
        if case let .suspended(reasons, pauseUntil) = state {
            guard !reasons.contains(reason) else {
                return SessionTransition(state: state)
            }

            var updatedReasons = reasons
            updatedReasons.insert(reason)
            return SessionTransition(
                state: .suspended(reasons: updatedReasons, pauseUntil: pauseUntil),
                effects: reasons.isEmpty ? [.cancelSuspensionResumeDebounce] : []
            )
        }

        let pauseUntil: Date?
        if case let .paused(until) = state, until > context.wallNow {
            pauseUntil = until
        } else if let persisted = context.persistedPauseUntil, persisted > context.wallNow {
            pauseUntil = persisted
        } else {
            pauseUntil = nil
        }

        return SessionTransition(
            state: .suspended(reasons: [reason], pauseUntil: pauseUntil),
            effects: [
                .hideWarning,
                .cancelEscapeHold,
                .dismissOverlay,
                .discardFrontmostApplication,
                .cancelSuspensionResumeDebounce
            ]
        )
    }

    private static func endSuspension(
        state: SessionState,
        reason: SuspensionReason
    ) -> SessionTransition {
        guard case let .suspended(reasons, pauseUntil) = state else {
            return SessionTransition(state: state)
        }

        if reasons.isEmpty {
            return SessionTransition(
                state: state,
                effects: [.scheduleSuspensionResumeDebounce(delay: 0.5)]
            )
        }

        guard reasons.contains(reason) else {
            return SessionTransition(state: state)
        }

        var updatedReasons = reasons
        updatedReasons.remove(reason)
        let updatedState = SessionState.suspended(
            reasons: updatedReasons,
            pauseUntil: pauseUntil
        )

        if updatedReasons.isEmpty {
            return SessionTransition(
                state: updatedState,
                effects: [.scheduleSuspensionResumeDebounce(delay: 0.5)]
            )
        }
        return SessionTransition(state: updatedState)
    }

    private static func resumeAfterSuspension(
        state: SessionState,
        context: SessionContext
    ) -> SessionTransition {
        guard case let .suspended(reasons, pauseUntil) = state, reasons.isEmpty else {
            return SessionTransition(state: state)
        }

        if let pauseUntil, pauseUntil > context.wallNow {
            return SessionTransition(state: .paused(until: pauseUntil))
        }

        var effects: [SessionEffect] = []
        if pauseUntil != nil || context.persistedPauseUntil != nil {
            effects.append(.persistPauseUntil(nil))
        }
        return SessionTransition(state: freshRunningState(context: context), effects: effects)
    }

    private static func freshRunningState(context: SessionContext) -> SessionState {
        .running(
            deadline: context.monotonicNow.advanced(
                by: context.schedule.effectiveWorkInterval
            )
        )
    }

    private static func displayedSeconds(_ duration: TimeInterval) -> Int {
        max(1, Int(ceil(max(0, duration))))
    }
}
