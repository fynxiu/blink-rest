import Foundation

struct MonotonicInstant: Equatable, Comparable, Sendable {
    let seconds: TimeInterval

    static let zero = MonotonicInstant(seconds: 0)

    static func < (lhs: MonotonicInstant, rhs: MonotonicInstant) -> Bool {
        lhs.seconds < rhs.seconds
    }

    func advanced(by duration: TimeInterval) -> MonotonicInstant {
        MonotonicInstant(seconds: seconds + duration)
    }

    func duration(to other: MonotonicInstant) -> TimeInterval {
        other.seconds - seconds
    }
}

@MainActor
protocol TimeSource: AnyObject {
    var monotonicNow: MonotonicInstant { get }
    var wallNow: Date { get }
}

@MainActor
final class SystemTimeSource: TimeSource {
    var monotonicNow: MonotonicInstant {
        MonotonicInstant(seconds: ProcessInfo.processInfo.systemUptime)
    }

    var wallNow: Date {
        Date()
    }
}
