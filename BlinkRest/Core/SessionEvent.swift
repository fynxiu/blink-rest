import Foundation

enum SessionEvent: Equatable, Sendable {
    case launch
    case tick
    case startBreakNow
    case escapeHoldCompleted
    case pause(until: Date)
    case resume
    case workIntervalChanged
    case breakDurationChanged
    case systemWillSleep
    case screensDidSleep
    case sessionDidResignActive
    case systemDidWake
    case screensDidWake
    case sessionDidBecomeActive
    case activeSpaceDidChange
    case suspensionResumeDebounceElapsed
    case displaysChanged
}
