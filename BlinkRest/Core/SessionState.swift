import Foundation

struct SessionSchedule: Equatable, Sendable {
    static let standard = SessionSchedule(
        workInterval: 20 * 60,
        breakDuration: 20,
        warningDuration: 5
    )

    let workInterval: TimeInterval
    let breakDuration: TimeInterval
    let warningDuration: TimeInterval

    init(
        workInterval: TimeInterval,
        breakDuration: TimeInterval,
        warningDuration: TimeInterval = 5
    ) {
        self.workInterval = workInterval
        self.breakDuration = breakDuration
        self.warningDuration = warningDuration
    }

    var effectiveWorkInterval: TimeInterval {
        workInterval.isFinite && workInterval > 0 ? workInterval : Self.standard.workInterval
    }

    var effectiveBreakDuration: TimeInterval {
        BreakProtocol.plan(totalDuration: breakDuration).totalDuration
    }

    var effectiveWarningDuration: TimeInterval {
        warningDuration.isFinite && warningDuration >= 0 ? warningDuration : Self.standard.warningDuration
    }
}

struct BreakSession: Equatable, Sendable {
    let startedAt: MonotonicInstant
    let endsAt: MonotonicInstant
    let durationSnapshot: TimeInterval

    func elapsed(at now: MonotonicInstant) -> TimeInterval {
        min(max(0, startedAt.duration(to: now)), durationSnapshot)
    }

    func remaining(at now: MonotonicInstant) -> TimeInterval {
        min(max(0, now.duration(to: endsAt)), durationSnapshot)
    }

    func progress(at now: MonotonicInstant) -> Double {
        guard durationSnapshot > 0 else { return 1 }
        return min(max(0, elapsed(at: now) / durationSnapshot), 1)
    }

    func phase(at now: MonotonicInstant) -> BreakPhase {
        BreakProtocol.plan(totalDuration: durationSnapshot).phase(at: elapsed(at: now))
    }
}

enum SuspensionReason: Hashable, Sendable {
    case systemSleep
    case screenSleep
    case inactiveSession
}

enum SessionState: Equatable, Sendable {
    case running(deadline: MonotonicInstant)
    case warning(breakStartsAt: MonotonicInstant)
    case breaking(
        startedAt: MonotonicInstant,
        endsAt: MonotonicInstant,
        durationSnapshot: TimeInterval
    )
    case paused(until: Date)
    case suspended(reasons: Set<SuspensionReason>, pauseUntil: Date?)

    var breakSession: BreakSession? {
        guard case let .breaking(startedAt, endsAt, durationSnapshot) = self else {
            return nil
        }
        return BreakSession(
            startedAt: startedAt,
            endsAt: endsAt,
            durationSnapshot: durationSnapshot
        )
    }

    func remainingDuration(monotonicNow: MonotonicInstant, wallNow: Date) -> TimeInterval {
        switch self {
        case let .running(deadline), let .warning(deadline):
            return max(0, monotonicNow.duration(to: deadline))
        case let .breaking(_, endsAt, _):
            return max(0, monotonicNow.duration(to: endsAt))
        case let .paused(until):
            return max(0, until.timeIntervalSince(wallNow))
        case .suspended:
            return 0
        }
    }

    func remainingSeconds(monotonicNow: MonotonicInstant, wallNow: Date) -> Int {
        Int(ceil(remainingDuration(monotonicNow: monotonicNow, wallNow: wallNow)))
    }
}
