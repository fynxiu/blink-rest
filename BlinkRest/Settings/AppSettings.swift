import Foundation

struct AppSettings: Equatable, Sendable {
    static let schemaVersion = 1

    static let allowedWorkIntervalSeconds: [TimeInterval] = [
        20 * 60,
        30 * 60,
        45 * 60,
        60 * 60,
    ]

    static let allowedBreakDurationSeconds: [TimeInterval] = [10, 20, 30]

    static let defaultWorkIntervalSeconds: TimeInterval = 20 * 60
    static let defaultBreakDurationSeconds: TimeInterval = 20

    let workIntervalSeconds: TimeInterval
    let breakDurationSeconds: TimeInterval

    init(
        workIntervalSeconds: TimeInterval = Self.defaultWorkIntervalSeconds,
        breakDurationSeconds: TimeInterval = Self.defaultBreakDurationSeconds
    ) {
        self.workIntervalSeconds = Self.allowedWorkIntervalSeconds.contains(workIntervalSeconds)
            ? workIntervalSeconds
            : Self.defaultWorkIntervalSeconds
        self.breakDurationSeconds = Self.allowedBreakDurationSeconds.contains(breakDurationSeconds)
            ? breakDurationSeconds
            : Self.defaultBreakDurationSeconds
    }
}
